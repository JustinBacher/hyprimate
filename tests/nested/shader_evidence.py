#!/usr/bin/env python3
"""Verify native shader effects in private virtual KWin + nested Hyprland.

No host socket, compositor configuration, workspace, or installed plugin is touched.
Requires kwin_wayland, dbus-run-session, Hyprland, hyprctl, grim, wayland-info,
Python GI, GTK 3 with gtk-layer-shell/Cairo, and Pillow. The full run also uses
GTK 4 with gtk4-session-lock and gtk4-layer-shell. Retains screenshots and logs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess as sp
import sys
import tempfile
import time

from PIL import Image, ImageChops, ImageStat
import burn_evidence as evidence

ROOT = Path(__file__).resolve().parents[2]
WIDTH, HEIGHT = 440, 300
MARKS = [([245, 20, 30], .24, .27), ([20, 30, 245], .73, .68)]
SCENE_CONFIG = '''
hl.monitor({output="",mode="1280x960@60",position="0x0",scale=1})
hl.config({cursor={invisible=true},general={border_size=0},
    decoration={rounding=0,shadow={enabled=false},blur={enabled=false}}})
hl.animation({leaf="fadeLayers",enabled=false})
hl.animation({leaf="layers",enabled=false})
hl.window_rule({name="shader-test",match={title="^PaperEvidence-.*$"},float=true,move="160 160"})
hl.window_rule({name="shader-side",match={title="^PaperEvidence-reload-close$"},float=true,move="710 160"})
hl.window_rule({name="shader-no-blur",match={title="^PaperEvidence-no-blur$"},no_blur=true})
hl.window_rule({name="shader-private",match={title="^PaperEvidence-protected$"},
    no_screen_share=true,no_blur=true})
'''


def probe_package(root, name, expression):
    directory = root / name
    directory.mkdir()
    parameters = {
        'tint': dict(type='vec3', default=[1, 1, 1], min=0, max=1),
        'seconds': dict(type='float', default=1, min=0, max=6),
    }
    manifest = directory / 'effect.json'
    manifest.write_text(json.dumps(dict(version=1, fragment='probe.frag', parameters=parameters)))
    (directory / 'probe.frag').write_text(f'''
layout(location=0) out vec4 color;
void main() {{
    vec4 source = hypr_sample_source(hypr_window_uv());
    vec3 rgb = abs(hypr_duration - seconds) < 0.01 ? {expression} : vec3(0.35);
    color = vec4(rgb * source.a, source.a);
}}
''')
    return manifest


def selection(path, milliseconds, tint):
    params = json.dumps(dict(tint=tint, seconds=milliseconds / 1000))
    return ('{effect=' + json.dumps(str(path)) + ',effect_duration=' + str(milliseconds) +
            ',effect_parameters=' + json.dumps(params) + '}')


def settings(open_effect, close_effect):
    return ('hl.config({plugin={hyprimate={window={style="liquid",movement={enabled=false},' +
            'open=' + open_effect + ',close=' + close_effect + '}}}})')


def client_box(fixture):
    x, y = fixture['at']
    w, h = fixture['size']
    return x + 20, y + 20, x + w - 20, y + h - 20


def halo_box(fixture):
    x, y = fixture['at']
    w, h = fixture['size']
    left, top = round(x - w * .1), round(y + h * .5)
    return left, top, left + 1, top + 1


def color_bias(image, box, wanted):
    rgb = ImageStat.Stat(image.crop(box)).mean
    if wanted == 'green':
        return rgb[1] - max(rgb[0], rgb[2])
    return min(rgb[0], rgb[2]) - rgb[1]

def lock_fixture():
    import gi
    gi.require_version('Gtk', '4.0')
    gi.require_version('Gtk4SessionLock', '1.0')
    from gi.repository import GLib, Gtk, Gtk4SessionLock
    lock = Gtk4SessionLock.Instance.new()
    loop = GLib.MainLoop()
    windows = []

    def monitor(owner, output):
        window = Gtk.Window()
        drawing = Gtk.DrawingArea()

        def paint(widget, cr, width, height):
            cr.set_source_rgb(.13, .18, .26)
            cr.paint()
        drawing.set_draw_func(paint)
        window.set_child(drawing)
        owner.assign_window_to_monitor(window, output)
        windows.append(window)
        window.present()

    def unlock(*args):
        lock.unlock()
        return GLib.SOURCE_REMOVE

    lock.connect('monitor', monitor)
    lock.connect('locked', lambda *_: print('LOCKED', flush=True))
    lock.connect('failed', lambda *_: loop.quit())
    lock.connect('unlocked', lambda *_: loop.quit())
    GLib.io_add_watch(sys.stdin, GLib.PRIORITY_DEFAULT, GLib.IO_IN | GLib.IO_HUP, unlock)
    if not lock.lock():
        return 1
    loop.run()
    return 0



def run(out, plugin, runtime, processes, profile_only=False, size=(1280, 960), previous=False,
        interactive=False):
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

    def start(name, command, stdin=None):
        with (out / f'{name}.log').open('w') as log:
            process = sp.Popen(command, env=env, stdin=stdin, stdout=log,
                               stderr=sp.STDOUT, start_new_session=True)
        processes.append(process)
        return process

    parent = start('parent', ['dbus-run-session', '--', 'kwin_wayland', '--virtual',
                              '--width', str(size[0]), '--height', str(size[1]), '--no-lockscreen',
                              '--no-global-shortcuts', '--socket', 'shader-parent'])
    evidence.until(lambda: (runtime / 'shader-parent').is_socket(), parent)
    env.update(WAYLAND_DISPLAY='shader-parent', AQ_BACKENDS='wayland')
    def parent_ready():
        try:
            info = sp.run(['wayland-info'], env=env, text=True, capture_output=True, timeout=2)
        except sp.TimeoutExpired:
            return False
        return 'wl_output' in info.stdout

    evidence.until(parent_ready, parent)
    config = runtime / 'hyprland.lua'
    base_config = (ROOT / 'tests/nested/hyprland.lua').read_text() + SCENE_CONFIG.replace(
        '1280x960@60', f'{size[0]}x{size[1]}@60')
    config.write_text(base_config)
    compositor = start('hyprland', ['Hyprland', '-c', config])

    def identify():
        result = sp.run(['hyprctl', 'instances', '-j'], env=env, text=True,
                        capture_output=True, timeout=5).stdout.strip()
        if not result.startswith('['):
            return None
        return next((entry for entry in json.loads(result)
                     if entry['pid'] == compositor.pid and
                     (runtime / 'hypr' / entry['instance'] / '.socket.sock').is_socket()), None)

    instance = evidence.until(identify, compositor)
    env.update(HYPRLAND_INSTANCE_SIGNATURE=instance['instance'], WAYLAND_DISPLAY=instance['wl_socket'])

    def hc(*command):
        result = sp.run(['hyprctl', '-i', instance['instance'], *command], env=env,
                        text=True, capture_output=True, timeout=10)
        reply = result.stdout.strip()
        if result.returncode or (command[0] in ('eval', 'dispatch', 'plugin') and reply != 'ok'):
            raise RuntimeError((command, reply, result.stderr))
        return reply

    def output_ready():
        return next((monitor for monitor in json.loads(hc('-j', 'monitors'))
                     if monitor['name'].startswith('WAYLAND-') and monitor['width'] > 0), None)

    monitor = evidence.until(output_ready, compositor)
    output = monitor['name']

    def snap(name):
        path = out / f'{name}.png'
        sp.run(['grim', '-l', '1', '-o', output, path], env=env, check=True, timeout=8)
        with Image.open(path) as image:
            return image.convert('RGB')

    snap('first-native-frame')
    hc('plugin', 'load', str(plugin))
    hc('dismissnotify', '-1')
    background = start('wallpaper', [sys.executable, __file__, '--wallpaper'])
    evidence.until(lambda: 'burn-evidence' in hc('-j', 'layers'), background)
    scene = start('client', [sys.executable, ROOT / 'tests/nested/paper_scene.py'], sp.PIPE)

    def send(operation, ident, **options):
        scene.stdin.write((json.dumps(dict(op=operation, id=ident, **options)) + '\n').encode())
        scene.stdin.flush()

    def client(ident):
        return next((item for item in json.loads(hc('-j', 'clients'))
                     if item['title'] == f'PaperEvidence-{ident}' and item['mapped']), None)

    def reload_with(open_effect, close_effect, native=''):
        replacement = config.with_suffix('.tmp')
        replacement.write_text(base_config + native + '\n' + settings(open_effect, close_effect) + '\n')
        replacement.replace(config)
        hc('reload')
        errors = hc('configerrors')
        if errors:
            raise RuntimeError(('private Hyprland config reload failed', errors))
        hc('dismissnotify', '-1')

    def open_client(ident, **options):
        send('open', ident, width=WIDTH, height=HEIGHT, **options)
        return evidence.until(lambda: client(ident), scene)

    def close_client(ident):
        send('close', ident)
        evidence.until(lambda: client(ident) is None, scene)

    def difference(left, right, box=None):
        image = ImageChops.difference(left, right)
        return sum(ImageStat.Stat(image.crop(box) if box else image).mean) / 3

    def maximum_difference(left, right, box=None):
        image = ImageChops.difference(left, right)
        return max(high for low, high in (image.crop(box) if box else image).getextrema())

    previous_background = None
    stable_frames = 0

    def stable_background():
        nonlocal previous_background, stable_frames
        current = snap('background')
        unchanged = (previous_background is not None and
                     ImageChops.difference(previous_background, current).getbbox() is None)
        stable_frames = stable_frames + 1 if unchanged else 0
        previous_background = current
        # Includes Hyprland's output-startup zoom, not just layer animations.
        return current if stable_frames >= 3 and max(current.getpixel((200, 200))) > 80 else None

    baseline = evidence.until(stable_background, compositor)
    if profile_only:
        probe = os.environ.get('HYPRIMATE_FRAME_PROBE')
        trace = Path(env['HYPRIMATE_FRAME_TRACE']) if probe else None
        if probe:
            hc('plugin', 'load', str(Path(probe).resolve(strict=True)))
        plain = '{effect="",enabled=false}'
        intervals = {}
        for name in ('native', 'fade', 'diffusion', 'particles'):
            package = ROOT / 'examples/shaders' / name / 'effect.json'
            effect = '{effect=' + json.dumps(str(package)) + ',effect_duration=2200}'
            if previous and name == 'fade':
                legacy = runtime / 'original-fade'
                shutil.copytree(package.parent, legacy)
                manifest = legacy / 'effect.json'
                description = json.loads(manifest.read_text())
                description.pop('blur_mask')
                manifest.write_text(json.dumps(description))
                fragment = legacy / 'fade.frag'
                body = fragment.read_text()
                start, end = body.index('    if (hypr_blur_mask)'), body.index('    color = vec4(source.rgb')
                fragment.write_text(body[:start] + body[end:])
                effect = '{effect=' + json.dumps(str(manifest)) + ',effect_duration=2200}'
            reload_with(plain if name == 'native' else effect, plain if name == 'native' else effect)
            if hc('hyprimate:profile', 'start') != 'ok':
                raise RuntimeError('private profile start failed')
            started = time.monotonic_ns()
            open_client('profile')
            time.sleep(2.45)
            close_client('profile')
            time.sleep(2.45)
            finished = time.monotonic_ns()
            report = json.loads(hc('hyprimate:profile', 'stop'))
            (out / f'{name}-profile.json').write_text(json.dumps(report, indent=2) + '\n')
            intervals[name] = (started, finished)
        if trace:
            # Plugin unload flushes the trace's file buffer before slicing it.
            hc('plugin', 'unload', str(Path(probe).resolve(strict=True)))
            samples = [tuple(map(int, fields)) for line in trace.read_text().splitlines()
                       if len(fields := line.split()) == 2]
            for name, (started, finished) in intervals.items():
                durations = sorted(duration / 1e6 for stamp, duration in samples
                                   if started <= stamp < finished)
                if durations:
                    stats = {f'p{pct}_ms': durations[(len(durations) - 1) * pct // 100]
                             for pct in (50, 95, 99, 100)}
                    stats['frames'] = len(durations)
                    (out / f'{name}-compositor.json').write_text(json.dumps(stats, indent=2) + '\n')
        return
    metrics = {}
    # Establish the existing built-in presentation before testing external
    # preparation. First-window rendering can settle native output sampling.
    initial_background = baseline
    open_client('native-warmup')
    time.sleep(.65)
    close_client('native-warmup')
    time.sleep(.65)
    baseline = evidence.until(stable_background, compositor)
    metrics['native-warmup-background-change'] = difference(initial_background, baseline)
    original = probe_package(runtime, 'original', 'tint')
    replacement = probe_package(runtime, 'replacement', 'tint.brg')
    broken = probe_package(runtime, 'broken', 'not_a_glsl_symbol')
    expanded = probe_package(runtime, 'expanded', 'tint')
    description = json.loads(expanded.read_text())
    description['bounds'] = [.2, .2, .2, .2]
    expanded.write_text(json.dumps(description))
    (expanded.parent / 'probe.frag').write_text(
        'layout(location=0) out vec4 color; void main() { color=vec4(tint,1); }')
    green = [0, 1, 0]
    magenta = [1, 0, 1]
    original_open = selection(original, 4500, green)
    original_close = selection(original, 4500, magenta)
    new_close = selection(replacement, 2600, [1, 0, 0])

    def expect_color(image, box, color, metric):
        metrics[metric] = color_bias(image, box, color)
        if metrics[metric] < 35:
            raise RuntimeError((metric, 'did not render the expected program, parameters and duration',
                                metrics[metric]))

    # Establish that an invalid first request (valid manifest; fragment fails
    # production link/compile) does not take ownership from the compositor.
    invalid = selection(broken, 5000, green)
    reload_with(invalid, invalid)
    native_box = client_box(open_client('invalid-first'))
    time.sleep(.65)
    native = snap('invalid-first-native')
    if difference(baseline, native, native_box) < 12 or color_bias(native, native_box, 'green') > 15:
        raise RuntimeError('initial invalid open did not reach the native presentation')
    close_client('invalid-first')
    time.sleep(.65)
    # Native partial-damage redraws may round an RGBA8 channel by one LSB.
    if maximum_difference(baseline, snap('invalid-first-finished'), native_box) > 1:
        raise RuntimeError('initial invalid close did not retire via the native fade')

    # Two simultaneous owners on different parts of the same output; reload
    # replaces only the valid choice and must never mutate either active instance.
    reload_with(original_open, original_close)
    close_box = client_box(open_client('reload-close'))
    time.sleep(.65)
    close_client('reload-close')
    open_box = client_box(open_client('reload-open'))
    if open_box[2] > close_box[0]:
        raise RuntimeError(('transaction fixtures overlap', open_box, close_box))
    time.sleep(.65)
    before = snap('before-transactional-reload')
    expect_color(before, open_box, 'green', 'old-open-before-reload')
    expect_color(before, close_box, 'magenta', 'old-close-before-reload')
    reload_with(selection(broken, 1300, magenta), new_close)
    time.sleep(.2)
    after = snap('after-transactional-reload')
    expect_color(after, open_box, 'green', 'active-open-pinned')
    expect_color(after, close_box, 'magenta', 'active-close-pinned')

    close_client('reload-open')
    time.sleep(.65)
    expect_color(snap('new-generation-close'), open_box, 'green', 'next-close-valid-replacement')
    time.sleep(2.25)
    next_box = client_box(open_client('reload-next'))
    time.sleep(.65)
    expect_color(snap('last-valid-generation-open'), next_box, 'green', 'next-open-keeps-last-valid')
    close_client('reload-next')
    time.sleep(2.85)

    # Older v1 packages still take native blur fallback. The configuration has
    # a real blur request even for the opaque drawing-area fixture.
    blur_config = ('hl.config({decoration={active_opacity=0.65,inactive_opacity=0.65,'
                   'blur={enabled=true,size=6,passes=2}}})')
    reload_with(original_open, selection(original, 4500, green), blur_config)
    native_box = client_box(open_client('needs-blur'))
    time.sleep(.65)
    blurred = snap('shader-declines-open-blur')
    hc('eval', 'hl.config({plugin={hyprimate={window={open={enabled=false}}}}})')
    time.sleep(.15)
    reference = snap('native-blur-reference')
    metrics['native-blur-fallback-error'] = difference(blurred, reference, native_box)
    if metrics['native-blur-fallback-error'] > 1.0 or difference(baseline, blurred, native_box) < 12:
        raise RuntimeError('shader opening did not preserve native blur presentation')
    close_client('needs-blur')
    time.sleep(.7)
    local = probe_package(runtime, 'shader-blur', 'tint')
    spec = json.loads(local.read_text())
    spec.pop('fragment')
    spec.update(blur_mask=True, bounds=[.25, 0, .25, 0], passes=[dict(
        name='shift', phase='render', vertex='shift.vert', fragment='probe.frag', outputs=['output'])])
    local.write_text(json.dumps(spec))
    (local.parent / 'shift.vert').write_text('''
out vec2 hypr_uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    hypr_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0 + vec2(.28,0), 0.0, 1.0);
}''')
    (local.parent / 'probe.frag').write_text('''
layout(location=0) out vec4 color;
void main() {
    vec2 uv = hypr_window_uv();
    vec4 native = hypr_sample_source(uv);
    float progress = hypr_event == 0 ? hypr_progress : (1.0 - hypr_progress);
    if (hypr_blur_mask) {
        color = vec4(native.r * progress);
    } else {
        // Fully transparent band must still retain its displaced blur coverage.
        float a = (uv.y > .33 && uv.y < .67) ? 0.0 : native.a * progress;
        color = vec4(tint * a, a);
    }
}
''')
    reload_with(selection(local, 1500, green), selection(local, 1600, magenta), blur_config)
    shifted = open_client('shader-blur')
    x, y = shifted['at']
    w, h = shifted['size']
    paint = (x + w // 2, y + 20, x + 3 * w // 4, y + h // 4)
    glass = (x + w // 2, y + 2 * h // 5, x + 3 * w // 4, y + 3 * h // 5)
    old_edge = (x + 4, y + h // 4, x + w // 10, y + 3 * h // 4)
    time.sleep(1.05)
    opening = snap('shader-blur-deformed-open')
    metrics['blur-open-color-bias'] = color_bias(opening, paint, 'green')
    metrics['blur-open-transparent-band'] = difference(opening, baseline, glass)
    metrics['blur-open-original-edge-error'] = maximum_difference(opening, baseline, old_edge)
    if (metrics['blur-open-color-bias'] < 20 or metrics['blur-open-transparent-band'] < 2 or
            metrics['blur-open-original-edge-error'] > 3):
        raise RuntimeError(('deformed blur opening', metrics))
    time.sleep(.6)
    close_client('shader-blur')
    time.sleep(.35)
    closing = snap('shader-blur-deformed-close')
    metrics['blur-close-color-bias'] = color_bias(closing, paint, 'magenta')
    metrics['blur-close-transparent-band'] = difference(closing, baseline, glass)
    if metrics['blur-close-color-bias'] < 20 or metrics['blur-close-transparent-band'] < 2:
        raise RuntimeError(('deformed blur closing', metrics))
    time.sleep(1.45)
    if maximum_difference(snap('shader-blur-retired'), baseline) > 2:
        raise RuntimeError('shader blur left pixels behind after retirement')

    # Keep a settled native blurred window beneath a simultaneously rendered
    # external effect. Its uncovered portion must not be erased by the effect.
    lower_rule = ('\n' + blur_config + '\n' +
                  'hl.window_rule({name="shader-overlap",match={title="^PaperEvidence-blur-lower$"},'
                  'float=true,move="410 160"})')
    reload_with('{effect="",enabled=false}', '{effect="",enabled=false}', lower_rule)
    lower = client_box(open_client('blur-lower'))
    time.sleep(.65)
    native_below = snap('native-blur-below')
    native_only = (lower[2] - 110, lower[1] + 20, lower[2] - 30, lower[1] + 75)
    if difference(native_below, baseline, native_only) < 8:
        raise RuntimeError('overlap fixture did not present its native window')
    reload_with(selection(local, 1500, green), selection(local, 1600, magenta), lower_rule)
    upper = client_box(open_client('blur-upper'))
    if upper[2] <= lower[0]:
        raise RuntimeError('native and external blur fixtures do not overlap')
    overlap = (lower[0] + 25, upper[1] + 20, upper[2] - 20, upper[1] + 75)
    time.sleep(.90)
    combined = snap('shader-blur-over-native')
    expect_color(combined, overlap, 'green', 'blur-over-native-color-bias')
    metrics['blur-native-uncovered-max-error'] = maximum_difference(combined, native_below, native_only)
    if metrics['blur-native-uncovered-max-error'] > 2:
        raise RuntimeError('external draw damaged the uncovered native window')
    close_client('blur-upper')
    close_client('blur-lower')
    time.sleep(1.75)
    reload_with(original_open, selection(original, 4500, green), blur_config)
    opt_in_box = client_box(open_client('no-blur'))
    time.sleep(.65)
    expect_color(snap('no-blur-shader-opt-in'), opt_in_box, 'green', 'no-blur-opening')
    close_client('no-blur')
    time.sleep(.65)
    expect_color(snap('no-blur-shader-close'), opt_in_box, 'green', 'no-blur-closing')
    time.sleep(4.1)
    reload_with('{effect=""}', '{effect=""}')

    for ident, name in enumerate(('fade', 'diffusion', 'particles')):
        path = str(ROOT / 'examples/shaders' / name / 'effect.json')
        parameters = ({'tint': [.1, 1, .1]}, {'tint': [1, .1, 1]}) if name == 'fade' else ({}, {})
        choices = [
            '{effect=' + json.dumps(path) + ',effect_duration=1800,effect_parameters=' +
            json.dumps(json.dumps(values)) + '}'
            for values in parameters
        ]
        # A package and its typed values form one configuration transaction.
        hc('eval', settings(*choices))
        options = dict(construction='light', evidence_marks=MARKS) if name == 'fade' else {}
        mapped = open_client(ident, **options)
        box = client_box(mapped)
        time.sleep(.45)
        opening = snap(name + '-opening')
        time.sleep(1.7)
        settled = snap(name + '-settled')
        metrics[name + '-opening-difference'] = difference(opening, settled, box)
        if metrics[name + '-opening-difference'] < 1:
            raise RuntimeError((name, 'no visible opening shader progression'))
        if name == 'fade':
            rgb = ImageStat.Stat(opening.crop(box)).mean
            metrics['opening-green-bias'] = rgb[1] - (rgb[0] + rgb[2]) / 2
            if metrics['opening-green-bias'] < 8:
                raise RuntimeError('opening parameter override did not tint green')
        send('close', ident)
        evidence.until(lambda: client(ident) is None, scene)
        time.sleep(.4)
        closing = snap(name + '-closing')
        metrics[name + '-closing-visible-after-unmap'] = difference(closing, baseline, box)
        if metrics[name + '-closing-visible-after-unmap'] < 1:
            raise RuntimeError((name, 'native fade ended without a retained shader close'))
        if name == 'fade':
            rgb = ImageStat.Stat(closing.crop(box)).mean
            metrics['closing-magenta-bias'] = (rgb[0] + rgb[2]) / 2 - rgb[1]
            if metrics['closing-magenta-bias'] < 8:
                raise RuntimeError('closing parameter override did not tint magenta')
            # Locate two asymmetric color marks at the same physical positions
            # in the native source and isolated shader crop. Tint alone cannot
            # hide a flipped or offset monitor-space crop.
            x, y = mapped['at']
            w, h = mapped['size']
            for (_, u, v), channel in zip(MARKS, (0, 2)):
                left, top = x + round(w * u), y + round(h * v)
                patch = (left + 2, top + 1, left + 10, top + 8)
                other = 2 - channel
                for image, label in ((settled, 'source'), (closing, 'isolated')):
                    mean = ImageStat.Stat(image.crop(patch)).mean
                    key = f'crop-{label}-{channel}-bias'
                    metrics[key] = mean[channel] - mean[other]
                    if metrics[key] < 40:
                        raise RuntimeError((key, 'close snapshot crop moved or flipped its source marks', mean))
        time.sleep(1.8)
        complete = snap(name + '-completed')
        metrics[name + '-completed-background-error'] = difference(complete, baseline)
        metrics[name + '-completed-background-max-error'] = maximum_difference(complete, baseline)
        if metrics[name + '-completed-background-max-error'] > 1:
            raise RuntimeError((name, 'effect did not retire to the original background'))

    expanded_effect = selection(expanded, 1800, green)
    hc('eval', settings(expanded_effect, expanded_effect))
    halo = halo_box(open_client('expanded'))
    time.sleep(.4)
    metrics['expanded-opening-exterior-green-bias'] = color_bias(snap('expanded-opening'), halo, 'green')
    if metrics['expanded-opening-exterior-green-bias'] < 200:
        raise RuntimeError('opening shader was clipped to the original window')
    time.sleep(1.8)
    metrics['expanded-opening-retirement-max-error'] = maximum_difference(
        baseline, snap('expanded-open-settled'), halo)
    if metrics['expanded-opening-retirement-max-error'] > 1:
        raise RuntimeError('opening shader did not retire its expanded drawing bounds')
    close_client('expanded')
    time.sleep(.4)
    metrics['expanded-closing-exterior-green-bias'] = color_bias(snap('expanded-closing'), halo, 'green')
    if metrics['expanded-closing-exterior-green-bias'] < 200:
        raise RuntimeError('closing shader was clipped to the original window')
    time.sleep(1.8)
    metrics['expanded-completed-background-max-error'] = maximum_difference(
        baseline, snap('expanded-close-completed'))
    if metrics['expanded-completed-background-max-error'] > 1:
        raise RuntimeError('closing shader did not retire its expanded drawing bounds')

    # Exercise the real compositor rule, in addition to lifecycle permission
    # regressions. A protected client must expose neither its pixels nor the
    # selected green external presentation through screencopy.
    private_effect = selection(expanded, 1800, green)
    hc('eval', settings(private_effect, private_effect))
    private_fixture = open_client('protected', construction='light')
    private_box = client_box(private_fixture)
    time.sleep(.65)
    protected = snap('protected-opening')
    metrics['protected-opening-green-bias'] = color_bias(protected, private_box, 'green')
    metrics['protected-opening-brightness'] = sum(ImageStat.Stat(protected.crop(private_box)).mean) / 3
    metrics['protected-exterior-green-bias'] = color_bias(protected, halo_box(private_fixture), 'green')
    if (metrics['protected-opening-green-bias'] > 80 or
            metrics['protected-opening-brightness'] > 140 or
            metrics['protected-exterior-green-bias'] > 80):
        raise RuntimeError('protected content or its shader presentation leaked through screencopy')
    close_client('protected')
    time.sleep(.65)
    metrics['protected-close-background-max-error'] = maximum_difference(
        baseline, snap('protected-close-finished'))
    if metrics['protected-close-background-max-error'] > 1:
        raise RuntimeError('protected close retained an external presentation')

    native = '{effect="",enabled=false}'
    reload_with(native, native)
    motion_manifest = ROOT / 'examples/shaders/motion/effect.json'
    def motion_settings(move, resize):
        hc('eval', 'hl.config({plugin={hyprimate={window={movement={enabled=true},' +
           'move={effect=' + json.dumps(str(move)) + ',effect_duration=650},resize={effect=' +
           json.dumps(str(resize)) + ',effect_duration=650}}}}})')

    motion_settings(motion_manifest, motion_manifest)
    moved = open_client('motion', construction='light')
    selector = 'window="address:' + moved['address'] + '"'
    time.sleep(.6)
    def geometry(operation, x, y):
        hc('eval', f'assert(hl.dispatch(hl.dsp.window.{operation}({{x={x},y={y},{selector}}})).ok)')

    geometry('move', 360, 250)
    time.sleep(.16)
    move_image = snap('motion-active')
    time.sleep(.85)
    move_settled = snap('motion-settled')
    move_box = client_box(client('motion'))
    metrics['external-move-pixel-change'] = difference(move_image, move_settled, move_box)
    for width, height in [(520, 345), (455, 315), (560, 360)]:
        geometry('resize', width, height)
        time.sleep(.10)
    resize_image = snap('resize-active')
    time.sleep(.85)
    resize_settled = snap('resize-settled')
    resize_box = client_box(client('motion'))
    metrics['external-resize-pixel-change'] = difference(resize_image, resize_settled, resize_box)
    # Restrict the comparison to the same interior patch; movement and resize
    # must select different authored tint branches, not just change geometry.
    patch = (390, 280, 530, 455)
    move_red, move_green, _ = ImageStat.Stat(move_image.crop(patch)).mean
    resize_red, resize_green, resize_blue = ImageStat.Stat(resize_image.crop(patch)).mean
    if move_green - move_red < 25 or min(resize_red, resize_blue) - resize_green < 25:
        raise RuntimeError('move and resize did not present their distinct shader tints')

    if interactive:
        import socket
        from construction_motion_evidence import HeldPointer, peer

        # Bind only inside the private fixture. Verify the Wayland socket peer
        # before sending input, even though the socket lives under private XDG.
        hc('eval', blur_config + '; hl.bind("mouse:272", hl.dsp.window.drag(), {mouse=true})')
        initial = client('motion')
        x, y = initial['at']
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            peer(sock, runtime / instance['wl_socket'], compositor.pid)
            pointer = HeldPointer(sock, size, [])
            try:
                pointer.connect()
                pointer.move((x + 105, y + 80))
                time.sleep(.08)
                pointer.button(True)
                pointer.segment((x + 235, y + 120), .34)
                forward = client('motion')['at'][0]
                pointer.segment((x + 150, y + 110), .25)
                px, py = client('motion')['at']
                image = snap('interactive-blur-drag-reversal')
                r, g, _ = ImageStat.Stat(image.crop((px + 80, py + 45, px + 240, py + 125))).mean
                if forward - x < 55 or forward - px < 45 or g - r < 25:
                    raise RuntimeError(('held drag did not reverse with its external shader', x, forward, px, g - r))
                metrics['interactive-drag-reversal-px'] = forward - px
                pointer.button(False)

                time.sleep(.85)
                hc('reload')  # Remove the drag bind, then select resize again after reload.
                motion_settings(motion_manifest, motion_manifest)
                hc('eval', blur_config + '; hl.bind("mouse:272", hl.dsp.window.resize(), {mouse=true})')
                item = client('motion')
                x, y = item['at']
                w, h = item['size']
                grip = (x + w - 12, y + h - 12)
                pointer.move(grip)
                time.sleep(.08)
                pointer.button(True)
                pointer.segment((grip[0] + 105, grip[1] + 40), .34)
                grown = client('motion')['size'][0]
                pointer.segment((grip[0] - 35, grip[1] - 26), .25)
                item = client('motion')
                px, py = item['at']
                image = snap('interactive-blur-resize-reversal')
                r, g, b = ImageStat.Stat(image.crop((px + 80, py + 45, px + 240, py + 125))).mean
                if grown - w < 55 or grown - item['size'][0] < 70 or b - max(r, g) < 18:
                    raise RuntimeError(('held resize did not reverse with its external shader', w, grown,
                                        item['size'], b - max(r, g)))
                metrics['interactive-resize-reversal-px'] = grown - item['size'][0]
            finally:
                pointer.close()
        hc('reload')  # Restore native opacity and clear the private pointer bind.
    close_client('motion')
    time.sleep(.65)

    motion_settings('', '')
    hc('eval', 'hl.config({plugin={hyprimate={window={movement={enabled=false}}}}}); '
       'hl.animation({leaf="windowsIn",enabled=false}); '
       'hl.animation({leaf="windowsMove",enabled=false})')

    # Place visibly different clients on both sides before enabling the effect.
    def workspace(number):
        hc('dispatch', f'hl.dsp.focus({{workspace={json.dumps(str(number))}}})')

    def stable_scene(name):
        # Native workspace offsets and client presentation can still settle
        # after dispatch and map complete. Compare stable, native endpoints.
        last = None
        equal_since = None

        def sample():
            nonlocal last, equal_since
            current = snap(name)
            stamp = time.monotonic()
            if last is None or ImageChops.difference(last, current).getbbox() is not None:
                equal_since = stamp
            last = current
            return current if stamp - equal_since >= .25 else None

        return evidence.until(sample, compositor)

    workspace(2)
    open_client('workspace-two', construction='light')
    time.sleep(.7)
    two = stable_scene('workspace-two')
    workspace(1)
    open_client('workspace-one')
    time.sleep(.7)
    one = stable_scene('workspace-one')
    workspace_manifest = ROOT / 'examples/shaders/workspace/effect.json'
    hc('eval', 'hl.config({plugin={hyprimate={workspace={enabled=true,style="rotation",effect=' +
       json.dumps(str(workspace_manifest)) + ',effect_duration=1400}}}})')
    time.sleep(.25)
    hc('hyprimate:profile', 'start')
    workspace(2)
    time.sleep(.5)
    outward = snap('workspace-shader-outward')
    workspace(1)
    time.sleep(.25)
    reversed_frame = snap('workspace-shader-reversal')
    time.sleep(1.35)
    ws_report = json.loads(hc('hyprimate:profile', 'stop'))
    (out / 'workspace-profile.json').write_text(json.dumps(ws_report, indent=2) + '\n')
    metrics['external-workspace-presentation-frames'] = sum(
        row['drawn_frames'] for row in ws_report['styles']
        if row['domain'] == 'workspace' and row['style'] == 'shader')
    if metrics['external-workspace-presentation-frames'] < 3:
        raise RuntimeError('workspace transition used only the built-in fallback')
    wallpaper_region = (20, 520, 220, 680)
    for frame in (outward, reversed_frame):
        if difference(one, frame, wallpaper_region) > 3:
            raise RuntimeError('workspace shader lost the captured native background')
    current_ws = json.loads(hc('-j', 'activeworkspace'))['id']
    settled = stable_scene('workspace-reversed-settled')
    if current_ws != 1 or difference(one, settled) > .7 or difference(one, two) < .4:
        raise RuntimeError('workspace capture controls were indistinct or reversal did not settle')
    hc('eval', 'hl.config({plugin={hyprimate={workspace={enabled=false}}}})')
    close_client('workspace-one')
    workspace(2)
    close_client('workspace-two')
    workspace(1)
    time.sleep(.65)

    reload_with(selection(expanded, 4500, green), native)
    revoke = open_client('shader-revoked')
    revoke_box = client_box(revoke)
    time.sleep(.65)
    expect_color(snap('before-protection-change'), revoke_box, 'green', 'before-protection-change')
    hc('eval', 'hl.window_rule({name="shader-revocation",match={title="^PaperEvidence-shader-revoked$"},'
       'no_screen_share=true})')
    protected = evidence.until(lambda: (frame := snap('after-protection-change')) and
                               color_bias(frame, revoke_box, 'green') < 80 and frame, compositor)
    if color_bias(protected, halo_box(revoke), 'green') > 80:
        raise RuntimeError('expanded shader pixels survived protected-content change')
    close_client('shader-revoked')
    time.sleep(.65)

    # Obtain a real ext-session-lock after the shader has presented. The
    # private compositor must switch to the lock surface without old pixels.
    reload_with(original_open, original_close)
    lock_window = client_box(open_client('lock-live'))
    time.sleep(.55)
    expect_color(snap('before-live-lock'), lock_window, 'green', 'live-before-lock')
    # GTK's Wayland interposer must load before PyGObject imports libwayland.
    lock = start('session-lock', ['env', 'LD_PRELOAD=libgtk4-layer-shell.so.0',
                                  sys.executable, __file__, '--lock-fixture'], sp.PIPE)
    evidence.until(lambda: 'LOCKED' in (out / 'session-lock.log').read_text(), lock)
    locked = evidence.until(lambda: (image := snap('live-lock')).getpixel((240, 220))[2] in
                            range(63, 73) and image, lock)
    if locked.getpixel((240, 220))[1] not in range(42, 52):
        raise RuntimeError('window or wallpaper pixels escaped to lock presentation')
    lock.stdin.write(b'unlock\n')
    lock.stdin.flush()
    if lock.wait(timeout=5):
        raise RuntimeError('private session unlock failed')
    close_client('lock-live')
    time.sleep(.6)

    # Exercise program/resource teardown while the host renderer still exists.
    hc('plugin', 'unload', str(plugin))
    snap('unloaded')
    hc('plugin', 'load', str(plugin))
    hc('plugin', 'unload', str(plugin))
    metrics['plugin_sha256'] = hashlib.sha256(plugin.read_bytes()).hexdigest()
    metrics['output'] = output
    metrics['compositor_pid'] = compositor.pid
    (out / 'measurements.json').write_text(json.dumps(metrics, indent=2) + '\n')
    print(json.dumps(metrics, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--plugin', type=Path, default=ROOT / 'build/hyprimate.so')
    parser.add_argument('--profile-only', action='store_true')
    parser.add_argument('--interactive', action='store_true', help='exercise private held grabs (requires NumPy)')
    parser.add_argument('--size', type=int, nargs=2, default=(1280, 960), metavar=('WIDTH', 'HEIGHT'))
    parser.add_argument('--previous', action='store_true', help='Profile the original v1 plugin')
    args = parser.parse_args()
    for command in ('kwin_wayland', 'dbus-run-session', 'Hyprland', 'hyprctl', 'grim', 'wayland-info'):
        if not shutil.which(command):
            raise RuntimeError(f'missing evidence dependency: {command}')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    processes = []
    # Keep the compositor's signature + Unix socket below sockaddr_un.sun_path.
    with tempfile.TemporaryDirectory(prefix='hs.', dir='/tmp') as temporary:
        try:
            run(out, args.plugin.resolve(strict=True), Path(temporary), processes,
                args.profile_only, args.size, args.previous, args.interactive)
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(3)
                    except sp.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
            for log in Path(temporary).glob('hypr/*/hyprland.log'):
                shutil.copyfile(log, out / 'private-hyprland.log')
    if args.profile_only:
        return
    bad = []
    copies = set()
    for log in out.glob('*.log'):
        for line in log.read_text(errors='replace').splitlines():
            if any(token in line for token in ('GL_INVALID_', 'OpenGL error', '[shader] failed')):
                bad.append(line)
            receipt = re.search(r'\[hyprimate\] shader (open|close) source copies=(\d+) copy_bytes=(\d+)', line)
            if receipt:
                copies.add((receipt[1], int(receipt[2]), int(receipt[3])))
    if bad:
        raise RuntimeError(('rendering errors in private logs', bad))
    workspace_captures = re.findall(r'workspace shader endpoint capture width=(\d+) height=(\d+)',
                                    (out / 'private-hyprland.log').read_text(errors='replace'))
    if len(workspace_captures) != 2:
        raise RuntimeError(('workspace reversal should use the original two captures', workspace_captures))
    open_copies = {(count, size) for event, count, size in copies if event == 'open'}
    close_copies = {(count, size) for event, count, size in copies if event == 'close'}
    # Hyprland's isolated window capture may include its two-pixel AA margin.
    expected_close = {(1, (WIDTH + padding) * (HEIGHT + padding) * bytes_per_pixel)
                      for padding in (0, 4) for bytes_per_pixel in (4, 8)}
    if open_copies != {(0, 0)} or not close_copies or not close_copies <= expected_close:
        raise RuntimeError(('unbounded or missing native source isolation receipts', sorted(copies)))
    measurements = out / 'measurements.json'
    metrics = json.loads(measurements.read_text())
    metrics['workspace_endpoint_captures'] = [tuple(map(int, extent)) for extent in workspace_captures]
    metrics['source_isolation_receipts'] = [
        dict(event=event, copies=count, copy_bytes=size)
        for event, count, size in sorted(copies)]
    measurements.write_text(json.dumps(metrics, indent=2) + '\n')
    print(json.dumps({'source_isolation_receipts': metrics['source_isolation_receipts']}, indent=2))


if __name__ == '__main__':
    if sys.argv[1:] == ['--wallpaper']:
        evidence.wallpaper()
    elif sys.argv[1:] == ['--lock-fixture']:
        raise SystemExit(lock_fixture())
    else:
        main()
