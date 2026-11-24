#!/usr/bin/env python3
"""Exercise isolated wallpaper sources and native-window environment wakes.

The harness starts its own virtual KWin and nested Hyprland, creates one opaque
SHM BACKGROUND layer per private output, and captures only those private outputs.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess as sp
import sys
import tempfile
import time

from PIL import Image, ImageChops, ImageStat

from burn_evidence import ROOT, until
from construction_motion_evidence import HeldPointer, peer

NAMESPACE = 'environment-evidence-pattern'
PRIMARY_SIZE = (1280, 960)
SECONDARY = 'ENVIRONMENT-FRACTIONAL'
REFRACTION = ROOT / 'examples/shaders/refraction/effect.json'
WATER = ROOT / 'examples/shaders/reactive-water/effect.json'
BURN = ROOT / 'examples/shaders/reactive-burn/effect.json'
EMITTER = ROOT / 'examples/shaders/environment-emitter/effect.json'
SETTLE_MS = 1400

SCENE_CONFIG = '''
hl.monitor({output="",mode="1280x960@60",position="0x0",scale=1})
hl.config({animations={enabled=true},cursor={invisible=true},general={border_size=0},
    decoration={rounding=0,shadow={enabled=false},blur={enabled=false}}})
hl.animation({leaf="layers",enabled=false})
hl.animation({leaf="fadeLayers",enabled=false})
hl.window_rule({name="environment-open",match={title="^PaperEvidence-environment-open$"},
    float=true,move="300 230"})
hl.window_rule({name="environment-move",match={title="^PaperEvidence-environment-move$"},
    float=true,move="880 220"})
hl.window_rule({name="environment-resize",match={title="^PaperEvidence-environment-resize$"},
    float=true,move="300 220"})
hl.window_rule({name="environment-close",match={title="^PaperEvidence-environment-close$"},
    float=true,move="300 230"})
hl.window_rule({name="environment-protected",match={title="^PaperEvidence-environment-protected$"},
    float=true,move="300 230",no_screen_share=true})
hl.window_rule({name="environment-profile",match={title="^PaperEvidence-profile-[12]$"},float=true})
'''


def layers(replacement=False):
    """Create exactly one opaque GTK SHM BACKGROUND toplevel per output."""
    import cairo
    import gi
    gi.require_version('Gtk', '3.0')
    gi.require_version('Gdk', '3.0')
    gi.require_version('GtkLayerShell', '0.1')
    from gi.repository import Gdk, Gtk, GtkLayerShell as Layer

    Gtk.init(None)
    display = Gdk.Display.get_default()
    count = display.get_n_monitors()
    if count != 2:
        raise RuntimeError(f'expected two private outputs for BACKGROUND fixture, got {count}')

    def show(monitor, index):
        window = Gtk.Window()
        window.set_decorated(False)
        window.set_app_paintable(True)
        Layer.init_for_window(window)
        Layer.set_monitor(window, monitor)
        Layer.set_namespace(window, NAMESPACE)
        Layer.set_layer(window, Layer.Layer.BACKGROUND)
        for edge in (Layer.Edge.TOP, Layer.Edge.BOTTOM, Layer.Edge.LEFT, Layer.Edge.RIGHT):
            Layer.set_anchor(window, edge, True)

        def paint(widget, context):
            width, height = widget.get_allocated_width(), widget.get_allocated_height()
            widget.get_window().set_opaque_region(cairo.Region(cairo.RectangleInt(0, 0, width, height)))
            context.set_operator(cairo.OPERATOR_SOURCE)
            context.set_source_rgb(.035, .045, .075)
            context.paint()
            colors = ((.91, .10, .075), (.08, .78, .19), (.08, .18, .91))
            if replacement:
                colors = tuple(reversed(colors))
            for part, color in enumerate(colors):
                left, right = width * part / 3, width * (part + 1) / 3
                context.set_source_rgb(*color)
                context.rectangle(left, 0, right - left, height * .5)
                context.fill()
            lower = (((.18, .07, .35), (.90, .42, .04), (.04, .50, .56)) if index == 0 else
                     ((.18, .28, .04), (.35, .08, .45), (.04, .32, .27)))
            for part, color in enumerate(lower):
                left, right = width * part / 3, width * (part + 1) / 3
                context.set_source_rgb(*color)
                context.rectangle(left, height * .5, right - left, height * .5)
                context.fill()
            # A thin asymmetric grid makes refraction and displacement visible.
            context.set_line_width(2)
            for x in range(48, width, 64):
                context.set_source_rgb(.96, .91, .62)
                context.move_to(x, height * .53)
                context.line_to(x, height)
                context.stroke()
            for y in range(48, height, 64):
                context.set_source_rgb(.96, .91, .62)
                context.move_to(0, y)
                context.line_to(width, y)
                context.stroke()
            # Large, non-symmetric marks identify orientation and output ownership.
            context.set_source_rgb(.99, .83, .04)
            context.move_to(width * .12, height * .68)
            context.line_to(width * .24, height * .68)
            context.line_to(width * .12, height * .82)
            context.close_path()
            context.fill()
            context.set_source_rgb(.95, .95, .91)
            context.arc(width * .83, height * .22, min(width, height) * .035, 0, 6.283185307)
            context.fill()
            return True

        window.connect('draw', paint)
        window.show_all()
        return window

    windows = [show(display.get_monitor(index), index) for index in range(count)]
    Gtk.main()
    for window in windows:
        window.destroy()


def difference_stats(left, right, box):
    delta = ImageChops.difference(left, right).crop(box)
    maximum = max(high for _, high in delta.getextrema())
    gray = delta.convert('L')
    pixels = gray.width * gray.height
    changed = sum(gray.histogram()[4:]) / pixels if pixels else 0.0
    return dict(mean=sum(ImageStat.Stat(delta).mean) / 3, maximum=maximum,
                changed_fraction=changed, pixels=pixels)


def lua_string(value):
    return json.dumps(str(value))


def window_bias(image, fixture, rect):
    x, y = fixture['at']
    left, top, right, bottom = rect
    rgb = ImageStat.Stat(image.crop((x + left, y + top, x + right, y + bottom))).mean
    return rgb[0] - rgb[1], rgb[1] - rgb[0]


def monitor_box(monitor, global_box):
    x, y, right, bottom = global_box
    scale = monitor['scale']
    origin_x, origin_y = monitor['x'], monitor['y']
    return (round((x - origin_x) * scale), round((y - origin_y) * scale),
            round((right - origin_x) * scale), round((bottom - origin_y) * scale))


def outside_box(fixture, margin=5, depth=34):
    x, y = fixture['at']
    width, height = fixture['size']
    return (x + 24, y + height + margin, x + width - 24, y + height + depth)


def verify_layer_roots(hc, outputs):
    state = json.loads(hc('-j', 'layers'))
    surfaces = []
    for output, record in state.items():
        for level, entries in record.get('levels', {}).items():
            for entry in entries:
                surfaces.append((output, str(level), entry))
    expected = set(outputs)
    roots = [(output, level, entry) for output, level, entry in surfaces
             if entry.get('namespace') == NAMESPACE]
    if len(roots) != len(expected) or {output for output, _, _ in roots} != expected:
        raise RuntimeError(('BACKGROUND namespace is not unique per private output', roots, expected))
    if len(surfaces) != len(expected) or any(level != '0' for _, level, _ in surfaces):
        raise RuntimeError(('fixture created a popup, subsurface root, or nonmatching layer', surfaces))


def scene_settings(hc, wallpaper='', duration=0, open_effect='', close_effect='', move_effect='',
                   resize_effect='', open_enabled=False, close_enabled=False, movement=False):
    effect = lambda value: lua_string(value) if value else '""'
    enabled = lambda flag: 'true' if flag else 'false'
    code = ('hl.config({plugin={hyprimate={'
            'wallpaper={effect=' + effect(wallpaper) + ',effect_duration=' + str(duration) +
            ',source_namespace=' + lua_string(NAMESPACE) + '},'
            'window={style="liquid",movement={enabled=' + enabled(movement) + '},'
            'open={effect=' + effect(open_effect) + ',effect_duration=1800,enabled=' + enabled(open_enabled) + '},'
            'close={effect=' + effect(close_effect) + ',effect_duration=1800,enabled=' + enabled(close_enabled) + '},'
            'move={effect=' + effect(move_effect) + ',effect_duration=1800},'
            'resize={effect=' + effect(resize_effect) + ',effect_duration=1800}'
            '}}}})')
    hc('eval', code)
    errors = hc('configerrors')
    if errors:
        raise RuntimeError(('private configuration rejected', errors))


def run(out, plugin, runtime, processes, profile=False, size=PRIMARY_SIZE):
    home = runtime / 'home'
    home.mkdir()
    env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), HOME=str(home),
               XDG_CONFIG_HOME=str(home / 'config'), XDG_CACHE_HOME=str(home / 'cache'),
               XDG_DATA_HOME=str(home / 'data'), XDG_STATE_HOME=str(home / 'state'),
               XDG_SESSION_TYPE='wayland', GDK_BACKEND='wayland', QT_QPA_PLATFORM='wayland',
               NO_AT_BRIDGE='1', HYPRLAND_NO_SD_VARS='1', HYPRLAND_NO_SD_NOTIFY='1')
    for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'HYPRLAND_INSTANCE_SIGNATURE',
                 'DBUS_SESSION_BUS_ADDRESS', 'SESSION_MANAGER', 'XDG_SESSION_ID'):
        env.pop(name, None)
    probe = os.environ.get('HYPRIMATE_FRAME_PROBE') if profile else None
    trace = out / 'frames.tsv'
    if probe:
        probe = str(Path(probe).resolve(strict=True))
        env['HYPRIMATE_FRAME_TRACE'] = str(trace)

    def launch(name, command, stdin=None):
        with (out / f'{name}.log').open('w') as log:
            process = sp.Popen(command, env=env, stdin=stdin, stdout=log,
                               stderr=sp.STDOUT, start_new_session=True)
        processes.append(process)
        return process

    parent = launch('parent', ['dbus-run-session', '--', 'kwin_wayland', '--virtual',
                              '--width', str(size[0]), '--height', str(size[1]), '--no-lockscreen',
                              '--no-global-shortcuts', '--socket', 'environment-parent'])
    until(lambda: (runtime / 'environment-parent').is_socket(), parent)
    env.update(WAYLAND_DISPLAY='environment-parent', AQ_BACKENDS='wayland')
    def parent_ready():
        try:
            info = sp.run(['wayland-info'], env=env, text=True, capture_output=True, timeout=2)
        except sp.TimeoutExpired:
            return False
        return 'wl_output' in info.stdout

    until(parent_ready, parent)

    config = runtime / 'hyprland.lua'
    base_config = (ROOT / 'tests/nested/hyprland.lua').read_text() + SCENE_CONFIG.replace(
        '1280x960@60', f'{size[0]}x{size[1]}@60')
    secondary_rule = (f'hl.monitor({{output={lua_string(SECONDARY)},mode="1280x720@60",'
                      f'position="{size[0]}x0",scale=1.25}})')
    base_config += '\n' + secondary_rule + '\n'
    config.write_text(base_config)
    compositor = launch('hyprland', ['Hyprland', '-c', config])

    def identify():
        result = sp.run(['hyprctl', 'instances', '-j'], env=env, text=True,
                        capture_output=True, timeout=5).stdout.strip()
        if not result.startswith('['):
            return None
        return next((entry for entry in json.loads(result) if entry['pid'] == compositor.pid and
                     (runtime / 'hypr' / entry['instance'] / '.socket.sock').is_socket()), None)

    instance = until(identify, compositor)
    env.update(HYPRLAND_INSTANCE_SIGNATURE=instance['instance'], WAYLAND_DISPLAY=instance['wl_socket'])

    def hc(*command):
        result = sp.run(['hyprctl', '-i', instance['instance'], *command], env=env,
                        text=True, capture_output=True, check=True, timeout=10)
        reply = result.stdout.strip()
        if command[0] in ('eval', 'dispatch', 'plugin', 'output') and reply != 'ok':
            raise RuntimeError((command, reply, result.stderr))
        return reply

    def monitor_list():
        return json.loads(hc('-j', 'monitors'))

    primary = until(lambda: next((m for m in monitor_list()
                                  if m['x'] == 0 and m['width'] > 0), None), compositor)
    hc('eval', secondary_rule)
    if hc('output', 'create', 'wayland', SECONDARY) != 'ok':
        raise RuntimeError('private fractional-DPI output could not be created')
    until(lambda: next((m for m in monitor_list()
                        if m['name'] == SECONDARY and m['width'] > 0), None), compositor)
    # A virtual backend may apply a late fallback size during its first configure.
    time.sleep(.25)
    hc('eval', secondary_rule)
    monitors = monitor_list()
    primary = next(m for m in monitors if m['x'] == 0)
    if profile and (primary['width'], primary['height']) != size:
        raise RuntimeError(('private performance output failed to apply its requested dimensions', primary, size))
    secondary = next(m for m in monitors if m['name'] == SECONDARY)
    if (len(monitors) != 2 or abs(primary['scale'] - 1.0) > .001 or
            abs(secondary['scale'] - 1.25) > .001 or secondary['x'] != size[0]):
        raise RuntimeError(('private mixed-DPI layout was not applied', monitors))
    if (secondary['width'], secondary['height']) != (1280, 720):
        raise RuntimeError(('private fractional output mode changed unexpectedly', secondary))
    pointer_extent = (round(primary['width'] / primary['scale'] + secondary['width'] / secondary['scale']),
                      round(max(primary['height'] / primary['scale'],
                                secondary['height'] / secondary['scale'])))

    hc('plugin', 'load', str(plugin))
    hc('dismissnotify', '-1')
    scene_settings(hc)
    layer_proc = launch('background', [sys.executable, __file__, '--layers'])
    until(lambda: NAMESPACE in hc('-j', 'layers') and len(json.loads(hc('-j', 'layers'))) == 2,
          layer_proc)
    verify_layer_roots(hc, {primary['name'], secondary['name']})
    scene = launch('clients', [sys.executable, ROOT / 'tests/nested/paper_scene.py'], sp.PIPE)

    def client(ident):
        return next((item for item in json.loads(hc('-j', 'clients'))
                     if item['title'] == 'PaperEvidence-' + ident and item['mapped']), None)

    def open_client(ident, width, height):
        hc('eval', f'assert(hl.dispatch(hl.dsp.focus({{monitor={lua_string(primary["name"])}}})).ok)')
        scene.stdin.write((json.dumps(dict(op='open', id=ident, width=width, height=height,
                                           construction='light')) + '\n').encode())
        scene.stdin.flush()
        return until(lambda: client(ident), scene)

    def close_client(ident):
        scene.stdin.write((json.dumps(dict(op='close', id=ident)) + '\n').encode())
        scene.stdin.flush()
        until(lambda: client(ident) is None, scene)

    def snap(output, label):
        path = out / f'{label}-{output}.png'
        sp.run(['grim', '-l', '1', '-o', output, path], env=env, check=True, timeout=8)
        with Image.open(path) as image:
            return image.convert('RGB')

    def capture(label):
        return {monitor['name']: snap(monitor['name'], label) for monitor in monitors}

    def stable_background(label):
        previous = None
        equal = 0

        def sample():
            nonlocal previous, equal
            current = capture(label)
            same = previous is not None and all(
                ImageChops.difference(previous[name], current[name]).getbbox() is None
                for name in current)
            equal = equal + 1 if same else 0
            previous = current
            return current if equal >= 3 else None

        return until(sample, compositor)

    metrics = {'outputs': [{'name': m['name'], 'position': [m['x'], m['y']],
                            'mode_pixels': [m['width'], m['height']], 'scale': m['scale']}
                           for m in monitors],
               'background_namespace': NAMESPACE, 'source_package': str(REFRACTION)}
    if profile:
        if probe:
            hc('plugin', 'load', probe)
        intervals = {}

        def stage(label, work):
            hc('hyprimate:profile', 'start')
            started = time.monotonic_ns()
            work()
            intervals[label] = (started, time.monotonic_ns())
            data = json.loads(hc('hyprimate:profile', 'stop'))
            (out / (label + '-profile.json')).write_text(json.dumps(data, indent=2) + '\n')

        def update(items):
            for step in range(28):
                for index, item in enumerate(items):
                    x = int(size[0] * (.05 + index * .19 + step * .014))
                    y = int(size[1] * (.14 + index * .17))
                    selector = 'address:' + item['address']
                    hc('eval', f'assert(hl.dispatch(hl.dsp.window.move({{x={x},y={y},window={lua_string(selector)}}})).ok)')
                time.sleep(.025)

        clients = [open_client('environment-open' if i == 0 else 'profile-' + str(i),
                               size[0] // 4, size[1] // 4) for i in range(3)]
        time.sleep(.85)
        stage('disabled', lambda: update(clients[:1]))
        for name in ('environment-open', 'profile-1', 'profile-2'):
            close_client(name)
        scene_settings(hc, wallpaper=str(WATER), duration=1)
        time.sleep(1.35)
        stage('configured-idle', lambda: time.sleep(.6))
        clients = [open_client('environment-open' if i == 0 else 'profile-' + str(i),
                               size[0] // 4, size[1] // 4) for i in range(3)]
        time.sleep(1.55)
        metrics['workload_client_sizes'] = [client(name)['size']
                                           for name in ('environment-open', 'profile-1', 'profile-2')]
        expected = [size[0] // 4, size[1] // 4]
        if any(actual != expected for actual in metrics['workload_client_sizes']):
            raise RuntimeError(('profile did not get comparable floating clients', metrics['workload_client_sizes']))
        stage('one', lambda: update(clients[:1]))
        stage('multiple', lambda: update(clients))
        metrics['workload_client_positions'] = [client(name)['at']
                                               for name in ('environment-open', 'profile-1', 'profile-2')]
        stage('persisting-tail', lambda: time.sleep(.75))
        time.sleep(1.65)
        stage('settled', lambda: time.sleep(.6))
        scene_settings(hc, wallpaper=str(WATER), duration=0)
        time.sleep(.22)
        stage('continuous', lambda: time.sleep(.75))
        scene_settings(hc)
        if probe:
            hc('plugin', 'unload', probe)
            frames = [tuple(map(int, line.split())) for line in trace.read_text().splitlines()]
            for label, (start, end) in intervals.items():
                durations = sorted(ns * 1e-6 for stamp, ns in frames if start <= stamp < end)
                metrics[label] = {'frames': len(durations), 'interval_s': (end - start) * 1e-9}
                if durations:
                    metrics[label].update({f'p{p}_ms': durations[(len(durations) - 1) * p // 100]
                                           for p in (50, 95, 99, 100)})
        (out / 'measurements.json').write_text(json.dumps(metrics, indent=2) + '\n')
        hc('plugin', 'unload', str(plugin))
        return

    pristine = stable_background('pristine')

    def compare_pristine(image, reference, label, box=None, *, tolerance=1):
        if box is None:
            box = (0, 0, image.width, image.height)
        stats = difference_stats(reference, image, box)
        metrics[label] = stats
        if stats['maximum'] > tolerance:
            raise RuntimeError((label, 'native layer pixels did not restore', stats))

    # With wallpaper processing disabled, refraction must visibly sample both
    # color bands from the selected original layer-shell BACKGROUND.
    scene_settings(hc)
    control = open_client('environment-open', 420, 240)
    time.sleep(.15)
    native_control = snap(primary['name'], 'native-refraction-control')
    close_client('environment-open')
    scene_settings(hc, open_effect=str(REFRACTION), open_enabled=True)
    refracted_fixture = open_client('environment-open', 420, 240)
    time.sleep(.72)
    refracted = snap(primary['name'], 'original-refraction-active')
    red_control, _ = window_bias(native_control, control, (18, 4, 100, 16))
    _, green_control = window_bias(native_control, control, (190, 4, 272, 16))
    red_refraction, _ = window_bias(refracted, refracted_fixture, (18, 4, 100, 16))
    _, green_refraction = window_bias(refracted, refracted_fixture, (190, 4, 272, 16))
    metrics['original_refraction_pattern_bias'] = {
        'red_patch_delta': red_refraction - red_control,
        'green_patch_delta': green_refraction - green_control}
    if red_refraction - red_control < 8 or green_refraction - green_control < 8:
        raise RuntimeError(('refraction did not sample both original pattern colors',
                            metrics['original_refraction_pattern_bias']))
    close_client('environment-open')
    time.sleep(2.35)
    compare_pristine(snap(primary['name'], 'refraction-finished'), pristine[primary['name']],
                     'refraction_teardown_max_error')

    def select_environment(package, external=''):
        scene_settings(hc, wallpaper=package, duration=12000,
                       open_effect=external, close_effect=external,
                       move_effect=external, resize_effect=external,
                       open_enabled=bool(external), close_enabled=bool(external), movement=True)
        time.sleep(.2)
        return stable_background('environment-' + Path(package).parent.name)

    def verify_trace(package_name, event_name, frame, reference, roi, output_name):
        box = monitor_box(next(m for m in monitors if m['name'] == output_name), roi)
        stats = difference_stats(reference[output_name], frame[output_name], box)
        metrics[f'{package_name}_{event_name}_trace'] = stats
        if stats['maximum'] < 8 or stats['changed_fraction'] < .002:
            raise RuntimeError((package_name, event_name, 'no visible environment trace outside the window',
                                roi, stats))

    def reset_package(package_name, fixtures=()):
        scene_settings(hc)
        cleared = capture(package_name + '-reset')
        for monitor in monitors:
            delta = ImageChops.difference(cleared[monitor['name']], pristine[monitor['name']])
            for fixture in fixtures:
                x, y = fixture['at']
                width, height = fixture['size']
                box = monitor_box(monitor, (x, y, x + width, y + height))
                box = (max(0, box[0]), max(0, box[1]),
                       min(delta.width, box[2]), min(delta.height, box[3]))
                if box[2] > box[0] and box[3] > box[1]:
                    delta.paste((0, 0, 0), box)
            stats = dict(mean=sum(ImageStat.Stat(delta).mean) / 3,
                         maximum=max(high for _, high in delta.getextrema()))
            metrics[f'{package_name}_reset_{monitor["name"]}'] = stats
            if stats['maximum'] > 1:
                raise RuntimeError((package_name, 'wallpaper did not restore outside mapped clients',
                                    monitor['name'], stats))

    def assert_refraction_mix(frame, fixture, label):
        red, _ = window_bias(frame, fixture, (18, 4, 100, 16))
        _, green = window_bias(frame, fixture, (190, 4, 272, 16))
        result = {'red_patch_bias': red - red_control, 'green_patch_bias': green - green_control}
        metrics[label] = result
        if result['red_patch_bias'] < 8 or result['green_patch_bias'] < 8:
            raise RuntimeError((label, 'external refraction did not coexist with native wallpaper', result))

    # Exercise all lifecycle events. Both finite packages preserve an evolving
    # tail and then restore the original wallpaper.
    for package_path, package_name in ((WATER, 'water'), (BURN, 'burn')):
        baseline = select_environment(str(package_path), str(REFRACTION))
        open_fx = open_client('environment-open', 420, 240)
        time.sleep(.62)
        opened = capture(package_name + '-open-active')
        assert_refraction_mix(opened[primary['name']], open_fx, package_name + '_native_external_mix')
        roi = outside_box(open_fx)
        verify_trace(package_name, 'open', opened, baseline, roi, primary['name'])
        if package_name == 'water':
            time.sleep(SETTLE_MS / 1000 + .35)
            settled = capture('water-open-restored')
            compare_pristine(settled[primary['name']], baseline[primary['name']],
                             'water_open_restored_perimeter_max_error', monitor_box(primary, roi))
        else:
            time.sleep(.28)
            burn_settled = capture('burn-open-tail')
            stats = difference_stats(pristine[primary['name']], burn_settled[primary['name']],
                                     monitor_box(primary, roi))
            metrics['burn_open_settled_trace'] = stats
            if stats['maximum'] < 5 or stats['changed_fraction'] < .001:
                raise RuntimeError(('burn open trace did not persist outside the client', stats))
        reset_package(package_name, [open_fx])
        close_client('environment-open')
        time.sleep(2.35)

        # Map before selecting the wallpaper package, so this scenario emits a
        # move wake only. Keep one HeldPointer button-down across the DPI seam.
        move_fx = open_client('environment-move', 320, 220)
        scene_settings(hc, wallpaper=str(package_path), duration=12000,
                       move_effect=str(REFRACTION), resize_effect=str(REFRACTION), movement=True)
        capture(package_name + '-move-start')
        hc('eval', 'hl.unbind("mouse:272"); hl.bind("mouse:272",hl.dsp.window.drag(),{mouse=true})')
        pointer_trace = []
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            pointer_peer = peer(sock, runtime / instance['wl_socket'], compositor.pid)
            pointer = HeldPointer(sock, pointer_extent, pointer_trace)
            try:
                pointer.connect()
                start = (move_fx['at'][0] + move_fx['size'][0] // 2,
                         move_fx['at'][1] + move_fx['size'][1] // 2)
                pointer.move(start)
                time.sleep(.08)
                pointer.button(True)
                pointer.segment((start[0] + 600, start[1] + 30), 1.0)
                moved_frame = capture(package_name + '-move-held')
                pointer.button(False)
            finally:
                pointer.close()
        moved = client('environment-move')
        if not moved or moved['at'][0] < 1280 or moved['size'] != move_fx['size']:
            raise RuntimeError(('held drag did not cross to the fractional-DPI output',
                                move_fx, moved, pointer_peer))
        metrics[f'{package_name}_mixed_dpi_drag'] = {
            'from': move_fx['at'], 'to': moved['at'], 'from_scale': primary['scale'],
            'to_scale': secondary['scale'], 'pointer_extent': pointer_extent,
            'pointer_events': len(pointer_trace), 'peer': pointer_peer}
        # Both boxes exclude the current window body; they contain only wallpaper.
        move_roi = (1170, 448, 1274, 530)
        destination_roi = (1290, 472, 1465, 530)
        verify_trace(package_name, 'move', moved_frame, pristine, move_roi, primary['name'])
        verify_trace(package_name, 'move_destination', moved_frame, pristine,
                     destination_roi, secondary['name'])
        if package_name == 'water':
            time.sleep(SETTLE_MS / 1000 + .35)
            settled = capture('water-move-restored')
            compare_pristine(settled[primary['name']], pristine[primary['name']],
                             'water_move_restored_corridor_max_error', monitor_box(primary, move_roi))
        else:
            time.sleep(.45)
            settled = capture('burn-move-tail')
            stats = difference_stats(pristine[primary['name']], settled[primary['name']],
                                     monitor_box(primary, move_roi))
            metrics['burn_move_settled_trace'] = stats
            if stats['maximum'] < 5 or stats['changed_fraction'] < .001:
                raise RuntimeError(('burn move trace did not persist outside the client', stats))
        reset_package(package_name, [moved])
        close_client('environment-move')
        time.sleep(2.35)

        # Use the same held native resize grab as the existing motion fixture.
        resize_fx = open_client('environment-resize', 360, 240)
        scene_settings(hc, wallpaper=str(package_path), duration=12000,
                       resize_effect=str(REFRACTION), movement=True)
        pointer_trace = []
        hc('eval', 'hl.unbind("mouse:272"); hl.bind("mouse:272",hl.dsp.window.resize(),{mouse=true})')
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            resize_peer = peer(sock, runtime / instance['wl_socket'], compositor.pid)
            pointer = HeldPointer(sock, pointer_extent, pointer_trace)
            try:
                pointer.connect()
                start = (resize_fx['at'][0] + 18, resize_fx['at'][1] + 18)
                pointer.move(start)
                time.sleep(.08)
                pointer.button(True)
                pointer.segment((start[0] + 112, start[1] + 64), .55)
                resized_frame = capture(package_name + '-resize-held')
                pointer.button(False)
            finally:
                pointer.close()
        resized = client('environment-resize')
        if not resized or resized['size'] == resize_fx['size']:
            raise RuntimeError(('native held resize did not change client geometry', resize_fx, resized,
                                resize_peer))
        resize_roi = outside_box(resized)
        verify_trace(package_name, 'resize', resized_frame, pristine, resize_roi, primary['name'])
        if package_name == 'water':
            time.sleep(SETTLE_MS / 1000 + .35)
            settled = capture('water-resize-restored')
            compare_pristine(settled[primary['name']], pristine[primary['name']],
                             'water_resize_restored_perimeter_max_error', monitor_box(primary, resize_roi))
        else:
            time.sleep(.28)
            settled = capture('burn-resize-tail')
            stats = difference_stats(pristine[primary['name']], settled[primary['name']],
                                     monitor_box(primary, resize_roi))
            metrics['burn_resize_settled_trace'] = stats
            if stats['maximum'] < 5 or stats['changed_fraction'] < .001:
                raise RuntimeError(('burn resize trace did not persist outside the client', stats))
        reset_package(package_name, [resized])
        close_client('environment-resize')
        time.sleep(2.35)

        # Close admission uses the compositor's real close lifecycle signal.
        close_fx = open_client('environment-close', 420, 240)
        scene_settings(hc, wallpaper=str(package_path), duration=12000,
                       close_effect=str(REFRACTION), close_enabled=True)
        scene.stdin.write((json.dumps(dict(op='close', id='environment-close')) + '\n').encode())
        scene.stdin.flush()
        until(lambda: client('environment-close') is None, scene)
        time.sleep(.20)
        closed = capture(package_name + '-close-active')
        close_roi = outside_box(close_fx)
        verify_trace(package_name, 'close', closed, pristine, close_roi, primary['name'])
        if package_name == 'water':
            time.sleep(SETTLE_MS / 1000 + .55)
            settled = capture('water-close-restored')
            compare_pristine(settled[primary['name']], pristine[primary['name']],
                             'water_close_restored_full_max_error')
        else:
            time.sleep(.32)
            settled = capture('burn-close-tail')
            stats = difference_stats(pristine[primary['name']], settled[primary['name']],
                                     monitor_box(primary, close_roi))
            metrics['burn_close_settled_trace'] = stats
            if stats['maximum'] < 3 or stats['mean'] < .035:
                raise RuntimeError(('burn close trace did not persist outside the client', stats))
            time.sleep(SETTLE_MS / 1000 + .45)
            healed = capture('burn-close-healed')
            compare_pristine(healed[primary['name']], pristine[primary['name']], 'burn_healed_max_error')
        reset_package(package_name)

    # Two independently selected native windows must leave distinct disturbances
    # in the same image while the shared simulation is active.
    concurrent_background = select_environment(str(WATER))
    left = open_client('environment-open', 420, 240)
    right = open_client('environment-move', 320, 220)
    time.sleep(.20)
    simultaneous = capture('native-simultaneous-wakes')
    verify_trace('concurrent', 'left_window', simultaneous, concurrent_background,
                 outside_box(left), primary['name'])
    x, y = right['at']
    # The test pattern is flat blue below this window; use the grid line above.
    right_contour = (x, y - 54, x + right['size'][0], y - 5)
    verify_trace('concurrent', 'right_window', simultaneous, concurrent_background,
                 right_contour, primary['name'])
    primary_workspace = next(m for m in monitor_list() if m['name'] == primary['name'])['activeWorkspace']['id']
    close_client('environment-open')
    close_client('environment-move')
    hc('dispatch', 'hl.dsp.focus({workspace="5"})')
    until(lambda: next(m for m in monitor_list() if m['name'] == primary['name'])
          ['activeWorkspace']['id'] == 5, compositor)
    switched = stable_background('workspace-switch-after-close')
    compare_pristine(switched[primary['name']], pristine[primary['name']], 'new_workspace_no_old_trail')
    hc('dispatch', f'hl.dsp.focus({{workspace={lua_string(primary_workspace)}}})')
    returned = stable_background('workspace-return-after-close')
    compare_pristine(returned[primary['name']], pristine[primary['name']], 'old_workspace_no_restored_trail')
    reset_package('concurrent')

    # The rule-protected client must neither leak through screencopy nor be
    # admitted as an environmental interaction.
    protected_baseline = select_environment(str(WATER))
    protected = open_client('environment-protected', 420, 240)
    time.sleep(.22)
    protected_frame = capture('protected-window')
    protected_roi = outside_box(protected)
    protected_stats = difference_stats(protected_baseline[primary['name']],
                                       protected_frame[primary['name']],
                                       monitor_box(primary, protected_roi))
    metrics['protected_no_perturbation'] = protected_stats
    if protected_stats['maximum'] > 1:
        raise RuntimeError(('protected window perturbed reactive wallpaper', protected_stats))
    protected_box = (protected['at'][0] + 20, protected['at'][1] + 20,
                     protected['at'][0] + protected['size'][0] - 20,
                     protected['at'][1] + protected['size'][1] - 20)
    brightness = sum(ImageStat.Stat(protected_frame[primary['name']].crop(protected_box)).mean) / 3
    metrics['protected_capture_brightness'] = brightness
    if brightness > 140:
        raise RuntimeError(('protected client pixels reached private screencopy', brightness))
    reset_package('protected', [protected])
    close_client('environment-protected')
    time.sleep(2.35)
    # Exercise real producer and consumer, then revoke admission during the tail.
    scene_settings(hc, wallpaper=str(BURN), duration=12000, move_effect=str(EMITTER), movement=True)
    coordinated = open_client('environment-open', 420, 240)
    time.sleep(1.65)
    for x in range(310, 651, 20):
        selector = lua_string('address:' + coordinated['address'])
        hc('eval', f'assert(hl.dispatch(hl.dsp.window.move({{x={x},y=230,window={selector}}})).ok)')
        time.sleep(.05)
    before_revoke = capture('coordinated-burn-active')
    roi = (320, 475, 620, 514)
    verify_trace('coordinated', 'gpu_producer', before_revoke, pristine, roi, primary['name'])
    active_workspace = next(m for m in monitor_list() if m['name'] == primary['name'])['activeWorkspace']['id']
    hc('eval', f'assert(hl.dispatch(hl.dsp.window.move({{workspace="6",follow=false,window={selector}}})).ok)')
    until(lambda: (hidden := client('environment-open')) and hidden['workspace']['id'] == 6, compositor)
    time.sleep(.12)
    verify_trace('coordinated', 'hidden_window_tail', capture('coordinated-hidden-tail'),
                 pristine, roi, primary['name'])
    if next(m for m in monitor_list() if m['name'] == primary['name'])['activeWorkspace']['id'] != active_workspace:
        raise RuntimeError('silent migration changed the presenting workspace')
    hc('eval', 'assert(hl.dispatch(hl.dsp.window.set_prop({prop="no_screen_share",value="1",'
       f'window={selector}}})).ok)')
    # A shader tail can settle by itself; require revocation while it is active.
    revoke_started = time.monotonic()
    until(lambda: difference_stats(pristine[primary['name']], snap(primary['name'], 'environment-revoked'),
                                  monitor_box(primary, roi))['maximum'] <= 1, compositor)
    revoke_elapsed = time.monotonic() - revoke_started
    if revoke_elapsed >= .47:
        raise RuntimeError(('privacy revocation waited for simulation decay', revoke_elapsed))
    metrics['hidden_window_revoke_ms'] = revoke_elapsed * 1000
    close_client('environment-open')

    # Lock during a real disturbance and check both output images before unlocking.
    scene_settings(hc, wallpaper=str(WATER), duration=12000)
    unlocked_window = open_client('environment-close', 420, 240)
    time.sleep(.25)
    verify_trace('lock', 'before', capture('environment-before-lock'), pristine,
                 outside_box(unlocked_window), primary['name'])
    lock = launch('session-lock', ['env', 'LD_PRELOAD=libgtk4-layer-shell.so.0', sys.executable,
                                 ROOT / 'tests/nested/shader_evidence.py', '--lock-fixture'], sp.PIPE)
    until(lambda: 'LOCKED' in (out / 'session-lock.log').read_text(), lock)
    for monitor in monitors:
        locked = until(lambda: (image := snap(monitor['name'], 'environment-locked')) and
                       image.getpixel((240, 220))[2] in range(63, 73) and image, lock)
        colors = locked.getextrema()
        if any(high - low > 1 for low, high in colors):
            raise RuntimeError(('original or reactive pixels reached the lock surface', monitor, colors))
    lock.stdin.write(b'unlock\n')
    lock.stdin.flush()
    if lock.wait(timeout=5):
        raise RuntimeError('private session unlock failed')
    stale_roi = monitor_box(primary, outside_box(unlocked_window))
    until(lambda: difference_stats(pristine[primary['name']], snap(primary['name'], 'environment-unlocked'),
                                  stale_roi)['maximum'] <= 1, compositor)
    close_client('environment-close')
    scene_settings(hc)
    time.sleep(.6)

    # Replace the eligible background with a distinct root and color order.
    layer_proc.terminate()
    layer_proc.wait(timeout=5)
    replacement = launch('replacement-background', [sys.executable, __file__, '--layers-replacement'])
    until(lambda: NAMESPACE in hc('-j', 'layers'), replacement)
    replaced = stable_background('replaced-background')
    if difference_stats(pristine[primary['name']], replaced[primary['name']], (40, 44, 400, 190))['mean'] < 60:
        raise RuntimeError('replacement root did not update the wallpaper content')
    scene_settings(hc, open_effect=str(REFRACTION), open_enabled=True)
    refracted_replacement = open_client('environment-resize', 360, 240)
    time.sleep(.72)
    after_replacement = snap(primary['name'], 'replacement-refraction-active')
    x, y = refracted_replacement['at']
    rr, _, rb = ImageStat.Stat(after_replacement.crop((x + 22, y + 5, x + 100, y + 16))).mean
    if rb - rr < 8:
        raise RuntimeError(('active window refraction sampled a retired wallpaper lease', rr, rb))
    metrics['replacement_blue_bias'] = rb - rr
    close_client('environment-resize')
    time.sleep(.6)
    pristine = replaced

    hc('plugin', 'unload', str(plugin))
    unloaded = capture('plugin-unloaded')
    for monitor in monitors:
        compare_pristine(unloaded[monitor['name']], pristine[monitor['name']],
                         f'teardown_{monitor["name"]}_max_error', tolerance=2)
    hc('plugin', 'load', str(plugin))
    hc('plugin', 'unload', str(plugin))
    receipts = (runtime / 'hypr' / instance['instance'] / 'hyprland.log').read_text().splitlines()
    mailbox = [line for line in receipts if 'environment mailbox storage=' in line]
    copied = max((int(line.rsplit('copied=', 1)[1]) for line in mailbox), default=0)
    if not copied:
        raise RuntimeError(('no previous-frame GPU mailbox contribution was consumed', mailbox))
    metrics['coordinated_mailbox_copied_bytes'] = copied
    metrics['wallpaper_render_paths'] = {
        path: sum('shader wallpaper final=' + path in line for line in receipts)
        for path in ('direct', 'pooled')}
    if not all(metrics['wallpaper_render_paths'].values()):
        raise RuntimeError(('native direct and publication paths were not both drawn',
                            metrics['wallpaper_render_paths']))
    metrics['plugin'] = str(plugin)
    metrics['compositor_pid'] = compositor.pid
    (out / 'measurements.json').write_text(json.dumps(metrics, indent=2) + '\n')
    print(json.dumps(metrics, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--plugin', type=Path, default=ROOT / 'build/hyprimate.so')
    parser.add_argument('--profile', action='store_true')
    parser.add_argument('--size', type=int, nargs=2, default=PRIMARY_SIZE, metavar=('WIDTH', 'HEIGHT'))
    args = parser.parse_args()
    if not args.profile and tuple(args.size) != PRIMARY_SIZE:
        parser.error('--size applies to --profile only')
    for command in ('kwin_wayland', 'dbus-run-session', 'Hyprland', 'hyprctl', 'grim', 'wayland-info'):
        if not shutil.which(command):
            raise RuntimeError(f'missing evidence dependency: {command}')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    processes = []
    with tempfile.TemporaryDirectory(prefix='he.', dir='/tmp') as temp:
        try:
            run(out, args.plugin.resolve(strict=True), Path(temp), processes, args.profile, tuple(args.size))
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(3)
                    except sp.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
            for log in Path(temp).glob('hypr/*/hyprland.log'):
                shutil.copyfile(log, out / 'private-hyprland.log')


if __name__ == '__main__':
    if sys.argv[1:] in (['--layers'], ['--layers-replacement']):
        layers(sys.argv[1] == '--layers-replacement')
    else:
        main()
