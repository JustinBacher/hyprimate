#!/usr/bin/env python3
"""Verify native shader effects in private virtual KWin + nested Hyprland.

No host socket, compositor configuration, workspace, or installed plugin is touched.
Requires kwin_wayland, dbus-run-session, Hyprland, hyprctl, grim, wayland-info,
Python GI/GTK3/gtk-layer-shell/Cairo, and Pillow. Retains screenshots and logs.
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


def run(out, plugin, runtime, processes):
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
                              '--width', '1280', '--height', '960', '--no-lockscreen',
                              '--no-global-shortcuts', '--socket', 'shader-parent'])
    evidence.until(lambda: (runtime / 'shader-parent').is_socket(), parent)
    env.update(WAYLAND_DISPLAY='shader-parent', AQ_BACKENDS='wayland')
    evidence.until(lambda: 'wl_output' in sp.run(['wayland-info'], env=env,
                   text=True, capture_output=True, timeout=5).stdout, parent)
    config = runtime / 'hyprland.lua'
    base_config = (ROOT / 'tests/nested/hyprland.lua').read_text() + SCENE_CONFIG
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

    # Keep backdrop blur enabled and make a real blur request with opacity.
    # A neighboring no_blur rule opts into the same package instead.
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
    hc('eval', 'hl.config({plugin={hyprimate={window={open={enabled=true}}}}})')
    close_client('needs-blur')
    time.sleep(.65)
    declined_close = snap('shader-declines-close-blur')
    # Compare against the same native blur path, not a blur-disabled output.
    hc('eval', 'hl.config({plugin={hyprimate={window={open={enabled=false},close={enabled=false}}}}})')
    open_client('needs-blur')
    time.sleep(.65)
    close_client('needs-blur')
    time.sleep(.65)
    native_close = snap('native-blur-close-reference')
    metrics['native-blur-close-fallback-error'] = maximum_difference(
        declined_close, native_close, native_box)
    # Two independently rounded native blur composites can differ by two LSBs.
    if metrics['native-blur-close-fallback-error'] > 2:
        raise RuntimeError('shader close changed native blurred snapshot retirement')
    hc('eval', 'hl.config({plugin={hyprimate={window={open={enabled=true},close={enabled=true}}}}})')
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
            run(out, args.plugin.resolve(strict=True), Path(temporary), processes)
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
    open_copies = {(count, size) for event, count, size in copies if event == 'open'}
    close_copies = {(count, size) for event, count, size in copies if event == 'close'}
    # Hyprland's isolated window capture may include its two-pixel AA margin.
    expected_close = {(1, (WIDTH + padding) * (HEIGHT + padding) * bytes_per_pixel)
                      for padding in (0, 4) for bytes_per_pixel in (4, 8)}
    if open_copies != {(0, 0)} or not close_copies or not close_copies <= expected_close:
        raise RuntimeError(('unbounded or missing native source isolation receipts', sorted(copies)))
    measurements = out / 'measurements.json'
    metrics = json.loads(measurements.read_text())
    metrics['source_isolation_receipts'] = [
        dict(event=event, copies=count, copy_bytes=size)
        for event, count, size in sorted(copies)]
    measurements.write_text(json.dumps(metrics, indent=2) + '\n')
    print(json.dumps({'source_isolation_receipts': metrics['source_isolation_receipts']}, indent=2))


if __name__ == '__main__':
    evidence.wallpaper() if sys.argv[1:] == ['--wallpaper'] else main()
