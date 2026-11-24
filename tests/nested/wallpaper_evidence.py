#!/usr/bin/env python3
"""Check external wallpaper against real native layers in private nested Hyprland.

Requires virtual KWin, Hyprland/hyprctl, grim, Python GI/GTK3, GtkLayerShell,
Cairo, and Pillow. Never uses the active compositor or its config.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess as sp
import sys
import tempfile
import time

from PIL import Image

from burn_evidence import ROOT, until


def layers():
    import gi
    gi.require_version('Gtk', '3.0')
    gi.require_version('GtkLayerShell', '0.1')
    from gi.repository import Gtk, GtkLayerShell as Layer

    def show(plane, name, edges, rgb, size=None):
        window = Gtk.Window()
        Layer.init_for_window(window)
        Layer.set_namespace(window, name)
        Layer.set_layer(window, plane)
        for edge in edges:
            Layer.set_anchor(window, edge, True)
        if size:
            window.set_default_size(*size)
            window.set_size_request(*size)

        def paint(_, ctx):
            ctx.set_source_rgb(*rgb)
            ctx.paint()
            return True

        window.connect('draw', paint)
        window.show_all()
        return window

    background = show(Layer.Layer.BACKGROUND, 'wallpaper-evidence-background',
                      [Layer.Edge.TOP, Layer.Edge.BOTTOM, Layer.Edge.LEFT, Layer.Edge.RIGHT],
                      (0.06, 0.12, 0.78))
    bottom = show(Layer.Layer.BOTTOM, 'wallpaper-evidence-bottom',
                  [Layer.Edge.TOP, Layer.Edge.LEFT], (0.75, 0.08, 0.65), (96, 80))
    Gtk.main()
    background.destroy()
    bottom.destroy()


def package(root):
    manifest = root / 'effect.json'
    manifest.write_text(json.dumps(dict(version=1, events=['wallpaper'], fragment='wallpaper.frag')))
    (root / 'wallpaper.frag').write_text('''
layout(location=0) out vec4 color;
void main() {
    vec2 uv = hypr_window_uv();
    if (hypr_event != 5 || any(notEqual(hypr_sample_source(vec2(0.5)), vec4(0.0)))) {
        color = vec4(1.0, 1.0, 0.0, 1.0);
    } else if (uv.x > 0.5) {
        color = vec4(0.0); // Native background must survive transparent output.
    } else if (uv.y > 0.5) {
        color = vec4(0.9, 0.03, 0.03, 1.0);
    } else {
        float level = hypr_duration > 0.0 ? hypr_progress : fract(hypr_elapsed);
        color = vec4(0.03, 0.22 + 0.68 * level, 0.03, 1.0);
    }
}
''')
    return manifest


def verify(out, plugin, runtime, processes):
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
    probe = os.environ.get('HYPRIMATE_FRAME_PROBE')
    trace = out / 'frame-trace.tsv'
    if probe:
        probe = str(Path(probe).resolve(strict=True))
        env['HYPRIMATE_FRAME_TRACE'] = str(trace)

    def start(name, argv):
        with (out / f'{name}.log').open('w') as log:
            process = sp.Popen(argv, env=env, stdout=log, stderr=sp.STDOUT, start_new_session=True)
        processes.append(process)
        return process

    parent = start('parent', ['dbus-run-session', '--', 'kwin_wayland', '--virtual',
                              '--width', '800', '--height', '600', '--no-lockscreen',
                              '--no-global-shortcuts', '--socket', 'wallpaper-parent'])
    until(lambda: (runtime / 'wallpaper-parent').is_socket(), parent)
    env.update(WAYLAND_DISPLAY='wallpaper-parent', AQ_BACKENDS='wayland')
    until(lambda: 'wl_output' in sp.run(['wayland-info'], env=env, text=True,
          capture_output=True, timeout=5).stdout, parent)
    config = runtime / 'hyprland.lua'
    config.write_text((ROOT / 'tests/nested/hyprland.lua').read_text() + '''
hl.monitor({output="",mode="800x600@60",position="0x0",scale=1})
hl.config({animations={enabled=false},cursor={invisible=true},decoration={blur={enabled=false}}})
''')
    compositor = start('hyprland', ['Hyprland', '-c', config])

    def identify():
        reply = sp.run(['hyprctl', 'instances', '-j'], env=env, text=True,
                       capture_output=True, timeout=5).stdout.strip()
        if not reply.startswith('['):
            return None
        return next((entry for entry in json.loads(reply) if entry['pid'] == compositor.pid and
                     (runtime / 'hypr' / entry['instance'] / '.socket.sock').is_socket()), None)

    instance = until(identify, compositor)
    env.update(HYPRLAND_INSTANCE_SIGNATURE=instance['instance'], WAYLAND_DISPLAY=instance['wl_socket'])

    def hc(*command):
        result = sp.run(['hyprctl', '-i', instance['instance'], *command], env=env,
                        text=True, capture_output=True, check=True, timeout=8)
        reply = result.stdout.strip()
        if command[0] in ('eval', 'plugin') and reply != 'ok':
            raise RuntimeError((command, reply, result.stderr))
        return reply

    def output_ready():
        return next((monitor['name'] for monitor in json.loads(hc('-j', 'monitors'))
                     if monitor['name'].startswith('WAYLAND-') and monitor['width'] > 0), None)

    output = until(output_ready, compositor)
    hc('plugin', 'load', str(plugin))
    if probe:
        hc('plugin', 'load', probe)
    scene = start('layers', [sys.executable, __file__, '--layers'])
    until(lambda: 'wallpaper-evidence-bottom' in hc('layers') and
          'wallpaper-evidence-background' in hc('layers'), scene)

    positions = {'outside': (620, 140), 'upper': (180, 140), 'lower': (180, 435), 'bottom': (32, 32)}

    def sample(name):
        path = out / f'{name}.png'
        sp.run(['grim', '-l', '1', '-o', output, path], env=env, check=True, timeout=8)
        with Image.open(path) as image:
            rgb = image.convert('RGB')
            return {key: rgb.getpixel(at) for key, at in positions.items()}

    def select(path, ms):
        hc('eval', 'hl.config({plugin={hyprimate={wallpaper={effect=' + json.dumps(str(path)) +
           ',effect_duration=' + str(ms) + '}}}})')
        hc('dismissnotify', '-1')
        errors = hc('configerrors')
        if errors:
            raise RuntimeError(errors)

    def matching(name, predicate):
        def observe():
            candidate = sample(name)
            return candidate if predicate(candidate) else None
        return until(observe, compositor)

    hc('dismissnotify', '-1')
    baseline = matching('native', lambda rgb: rgb['outside'][2] > 160 and rgb['bottom'][0] > 140)
    manifest = package(runtime)
    select(manifest, 420)
    terminal = matching('finite-foreground', lambda rgb: rgb['lower'][0] > 210 and
                        abs(rgb['upper'][1] - rgb['lower'][0]) <= 1)
    if (terminal['outside'], terminal['bottom']) != (baseline['outside'], baseline['bottom']):
        raise RuntimeError(('wallpaper alpha or native BOTTOM layer was overwritten', baseline, terminal))
    def quiet_interval():
        start = time.monotonic_ns()
        time.sleep(.35)
        return start, time.monotonic_ns()

    finite_idle = quiet_interval()
    if sample('finite-held') != terminal:
        raise RuntimeError('the terminal wallpaper did not remain stable')

    select(runtime / 'missing.json', 0)
    if sample('invalid-replacement') != terminal:
        raise RuntimeError('an invalid replacement displaced the valid wallpaper')

    select(manifest, 0)
    running_a = matching('continuous-a', lambda rgb: rgb['upper'][1] < 120 and rgb['lower'][0] > 210)
    running_b = matching('continuous-b', lambda rgb: rgb['upper'][1] > 170 and rgb['lower'][0] > 210)
    for state in (running_a, running_b):
        if (state['outside'], state['bottom']) != (baseline['outside'], baseline['bottom']):
            raise RuntimeError('animated wallpaper changed transparent or foreground pixels')

    continuous_idle = quiet_interval()
    select('', 0)
    restored = matching('disabled', lambda rgb: rgb == baseline)
    select(manifest, 0)
    matching('before-unload', lambda rgb: rgb['lower'][0] > 210)
    secondary = 'SHADER-ROTATED'
    hc('eval', 'hl.monitor({output="SHADER-ROTATED",mode="800x600@60",position="800x0",'
       'scale=1.25,transform=1})')
    if hc('output', 'create', 'wayland', secondary) != 'ok':
        raise RuntimeError('private Wayland output could not be added')

    def output_state():
        return next((m for m in json.loads(hc('-j', 'monitors')) if m['name'] == secondary), None)

    rotated = until(lambda: (monitor := output_state()) and monitor['width'] > 0 and monitor,
                    compositor)
    if rotated['transform'] != 1 or abs(rotated['scale'] - 1.25) > .01:
        raise RuntimeError(('rotated fractional output config not applied', rotated))

    def secondary_frame(label):
        path = out / f'{label}.png'
        sp.run(['grim', '-l', '1', '-o', secondary, path], env=env, check=True, timeout=8)
        with Image.open(path) as frame:
            image = frame.convert('RGB')
            pixel = image.getpixel((image.width // 4, image.height * 3 // 4))
            return pixel if pixel[0] > 150 and pixel[0] > 3 * max(pixel[1], pixel[2]) else None

    until(lambda: secondary_frame('rotated-fractional'), compositor)
    hc('eval', 'hl.monitor({output="SHADER-ROTATED",mode="640x480@60",position="800x0",'
       'scale=1.25,transform=1})')
    until(lambda: (monitor := output_state()) and monitor['width'] == 640, compositor)
    until(lambda: secondary_frame('rotated-resized'), compositor)
    if hc('output', 'remove', secondary) != 'ok':
        raise RuntimeError('private output did not retire')
    until(lambda: not output_state(), compositor)
    hc('plugin', 'unload', str(plugin))
    matching('unloaded', lambda rgb: rgb == baseline)
    results = dict(native=baseline, terminal=terminal, continuous=[running_a, running_b], disabled=restored)
    if probe:
        hc('plugin', 'unload', probe)
        frames = [tuple(map(int, line.split())) for line in trace.read_text().splitlines()]
        for name, (start, end) in [('finite', finite_idle), ('continuous', continuous_idle)]:
            durations = sorted(ns / 1e6 for stamp, ns in frames if start <= stamp <= end)
            results[name + '_idle'] = dict(frames=len(durations),
                                           renderer_cpu_p95_ms=(durations[int(.95 * (len(durations) - 1))]
                                                                if durations else None))
        if results['finite_idle']['frames'] > 2 or results['continuous_idle']['frames'] < 8:
            raise RuntimeError(('unexpected idle frame scheduling', results))
    (out / 'measurements.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))


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
    with tempfile.TemporaryDirectory(prefix='hw.', dir='/tmp') as temp:
        try:
            verify(out, args.plugin.resolve(strict=True), Path(temp), processes)
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
    layers() if sys.argv[1:] == ['--layers'] else main()
