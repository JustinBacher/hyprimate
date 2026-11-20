#!/usr/bin/env python3
"""Capture Burn on actual Wayland clients, in private virtual KWin + Hyprland.

/usr/bin/python tests/nested/burn_evidence.py build/burn-evidence
Needs kwin_wayland, Hyprland/hyprctl, grim, GTK3 + gtk-layer-shell GI, Cairo, PIL.
PNG frames are at native resolution. No host socket, config or wallpaper is used.
"""
import argparse
import json
import hashlib
import shutil
import os
from pathlib import Path
import signal
import subprocess as sp
import sys
import tempfile
import time

from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[2]


def wallpaper():
    import gi
    gi.require_version('Gtk', '3.0')
    gi.require_version('GtkLayerShell', '0.1')
    from gi.repository import Gtk, GtkLayerShell as Layer
    window = Gtk.Window()
    Layer.init_for_window(window)
    Layer.set_layer(window, Layer.Layer.BACKGROUND)
    Layer.set_namespace(window, 'burn-evidence')
    for edge in (Layer.Edge.TOP, Layer.Edge.RIGHT, Layer.Edge.BOTTOM, Layer.Edge.LEFT):
        Layer.set_anchor(window, edge, True)

    def paint(widget, ctx):
        # A coloured, printed contour map. Both image contrast and position are
        # legible through the char; a uniform dark overlay is easy to spot.
        import math
        w, h = widget.get_allocated_width(), widget.get_allocated_height()
        ctx.set_source_rgb(.18, .43, .48)
        ctx.paint()
        for y in range(-64, h + 64, 10):
            ctx.set_source_rgb(.39, .64, .59) if y % 50 else ctx.set_source_rgb(.66, .76, .60)
            for x in range(0, w + 4, 4):
                offset = 17 * math.sin(x / 145 + y / 77) + 9 * math.cos(x / 64 - y / 113)
                (ctx.line_to if x else ctx.move_to)(x, y + offset)
            ctx.stroke()
        ctx.set_source_rgb(.84, .91, .79)
        ctx.set_font_size(14)
        for y in range(50, h, 97):
            for x in range(16, w, 180):
                ctx.move_to(x, y)
                ctx.show_text(f'{x:04d} E / {y:03d} N')
        return True

    window.connect('draw', paint)
    window.show_all()
    Gtk.main()


def until(fn, process):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline and process.poll() is None:
        result = fn()
        if result:
            return result
        time.sleep(.05)
    raise RuntimeError(f'private process not ready (exit {process.poll()}); see logs')


def run(out, plugin, runtime, procs, record, single_output):
    home = runtime / 'home'
    home.mkdir()
    env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), HOME=str(home),
               GDK_BACKEND='wayland', QT_QPA_PLATFORM='wayland', NO_AT_BRIDGE='1',
               XDG_SESSION_TYPE='wayland', XDG_CONFIG_HOME=str(home / 'config'),
               XDG_CACHE_HOME=str(home / 'cache'), XDG_DATA_HOME=str(home / 'data'),
               XDG_STATE_HOME=str(home / 'state'), HYPRLAND_NO_SD_VARS='1', HYPRLAND_NO_SD_NOTIFY='1')
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'HYPRLAND_INSTANCE_SIGNATURE',
                'DBUS_SESSION_BUS_ADDRESS', 'SESSION_MANAGER'):
        env.pop(key, None)

    def start(name, command, stdin=None):
        with (out / f'{name}.log').open('w') as log:
            process = sp.Popen(command, env=env, stdin=stdin, stdout=log, stderr=sp.STDOUT, start_new_session=True)
        procs.append(process)
        return process

    parent = start('parent', ['dbus-run-session', '--', 'kwin_wayland', '--virtual',
                              '--width', '1280', '--height', '960', '--no-lockscreen',
                              '--no-global-shortcuts', '--socket', 'burn-parent'])
    until(lambda: (runtime / 'burn-parent').is_socket(), parent)
    env.update(WAYLAND_DISPLAY='burn-parent', AQ_BACKENDS='wayland')
    # A published socket can precede the parent's usable wl_output. Complete
    # a registry round trip before Aquamarine negotiates its first swapchain.
    until(lambda: 'wl_output' in sp.check_output(['wayland-info'], env=env, text=True), parent)
    compositor = start('hyprland', ['Hyprland', '-c', ROOT / 'tests/nested/hyprland.lua'])

    def identify():
        reply = sp.run(['hyprctl', 'instances', '-j'], env=env, text=True, capture_output=True).stdout.strip()
        return next((v for v in json.loads(reply) if v['pid'] == compositor.pid and
                     (runtime / 'hypr' / v['instance'] / '.socket.sock').is_socket()), None) if reply.startswith('[') else None

    instance = until(identify, compositor)
    env.update(HYPRLAND_INSTANCE_SIGNATURE=instance['instance'], WAYLAND_DISPLAY=instance['wl_socket'])

    def hc(*cmd):
        result = sp.run(['hyprctl', '-i', instance['instance'], *cmd], env=env, text=True, capture_output=True)
        reply = result.stdout.strip()
        if result.returncode:
            raise RuntimeError((cmd, reply, result.stderr))
        if cmd[0] in ('eval', 'dispatch', 'plugin') and reply != 'ok':
            raise RuntimeError((cmd, reply))
        return reply

    def ready_outputs():
        return [m for m in json.loads(hc('-j', 'monitors')) if m['name'] != 'FALLBACK'
                and m['width'] > 0 and m['height'] > 0]

    monitor = until(lambda: next((m for m in ready_outputs() if m['name'].startswith('WAYLAND-')), None), compositor)
    output = monitor['name']
    # IPC comes up before framebuffer formats and the first presentation. Do not
    # trigger a plugin config refresh on an unconfigured nested output.
    sp.run(['grim', '-o', output, out / 'first-native-frame.png'], env=env, check=True, timeout=8)
    hc('plugin', 'load', str(plugin))
    hc('eval', 'hl.config({cursor={invisible=true},general={border_size=0},decoration={rounding=0,shadow={enabled=false},blur={enabled=false}}})')
    hc('eval', 'hl.window_rule({name="burn-test",match={title="^PaperEvidence-.*$"},float=true,move="50 50"})')
    hc('dismissnotify', '-1')
    bg = start('wallpaper', [sys.executable, __file__, '--wallpaper'])
    until(lambda: 'burn-evidence' in hc('-j', 'layers'), bg)
    scene = start('client', [sys.executable, ROOT / 'tests/nested/paper_scene.py'], sp.PIPE)
    metrics = {}

    def send(op, ident, **options):
        scene.stdin.write((json.dumps(dict(op=op, id=ident, **options)) + '\n').encode())
        scene.stdin.flush()

    def snap(name):
        return snap_output(name, output)

    def snap_output(name, target):
        path = out / (name + '.png')
        sp.run(['grim', '-o', target, path], env=env, check=True, timeout=6)
        with Image.open(path) as image:
            return image.convert('RGB')

    def difference(a, b, area=None):
        result = ImageChops.difference(a, b)
        return round(sum(ImageStat.Stat(result.crop(area) if area else result).mean) / 3, 3)

    def configure(style, direction):
        hc('eval', 'hl.config({plugin={hyprimate={window={enabled=true,style="burn",close={options={burn={closing_style="' + style + '",debris_direction="' + direction + '"}}}}}}})')
        for key, expected in [('style', 'burn'), ('close:options:burn:closing_style', style),
                              ('close:options:burn:debris_direction', direction)]:
            actual = json.loads(hc('-j', 'getoption', 'plugin:hyprimate:window:' + key))
            if actual.get('str') != expected:
                raise RuntimeError(('configuration readback mismatch', key, expected, actual))
        for phase in ('open', 'movement', 'close'):
            actual = json.loads(hc('-j', 'getoption', f'plugin:hyprimate:window:{phase}:enabled'))
            if actual.get('int', actual.get('bool')) not in (1, True):
                raise RuntimeError(('animation unexpectedly disabled', phase, actual))

    other = None
    if not single_output:
        if hc('output', 'create', 'wayland').lower() != 'ok':
            raise RuntimeError('private second output creation failed')
        other = until(lambda: next((m['name'] for m in ready_outputs()
                      if m['name'].startswith('WAYLAND-') and m['name'] != output), None), compositor)
        hc('dispatch', f'hl.dsp.focus({{monitor="{output}"}})')
    hc('eval', 'hl.animation({leaf="workspaces",enabled=false})')
    workspace = json.loads(hc('-j', 'activeworkspace'))['id']
    recording = None
    if record:
        recorder = shutil.which('wf-recorder') or ROOT / '.project-manager/tools/usr/bin/wf-recorder'
        recording = start('recorder', [recorder, '-y', '-D', '-o', output, '-c', 'libx264',
            '-p', 'crf=16', '-p', 'preset=ultrafast', '-p', 'threads=2', '-f', out / 'burn.mkv'])
    time.sleep(1.2)
    baseline = snap('wallpaper')
    other_native = snap_output('other-native', other) if other else None
    for i, (w, h, style, direction) in enumerate([(450,450,'curl','natural'), (640,210,'consume','fall'),
                                                  (235,520,'curl','rise'), (520,330,'curl','wind')]):
        prefix = f'{style}-{direction}'
        configure(style, direction)
        send('open', i, width=w, height=h, image=(i != 1))
        client = until(lambda: next((c for c in json.loads(hc('-j','clients')) if c['title'] == f'PaperEvidence-{i}'), None), scene)
        x, y = client['at']
        time.sleep(.26)
        opening = snap(prefix + '-open')
        time.sleep(.30)
        snap(prefix + '-open-late')
        # Opening debris can outlive the reveal, especially with a long wind drift.
        time.sleep(2.4)
        settled = snap(prefix + '-settled')
        hc('eval', 'hl.config({plugin={hyprimate={window={enabled=false}}}})')
        time.sleep(.18)
        metrics[prefix + '-native-mae'] = difference(settled, snap(prefix + '-native'))
        metrics[prefix + '-opening-to-native'] = difference(opening, settled, (x, y, x+w, y+h))
        center = (x+w//2-8, y+h//2-8, x+w//2+8, y+h//2+8)
        corner = (x+8, y+8, x+24, y+24)
        metrics[prefix + '-opening-center-print'] = difference(opening, settled, center)
        metrics[prefix + '-opening-corner-wallpaper'] = difference(opening, baseline, corner)
        configure(style, direction)
        dest = min(x + 440, monitor['width'] - w - 20)
        for k in range(1, 13):
            hc('dispatch', f'hl.dsp.window.move({{x={x+(dest-x)*k//12},y={y+3*k},relative=false}})')
            time.sleep(.035)
        time.sleep(.20)
        trail = snap(prefix + '-trail')
        moved = next(c for c in json.loads(hc('-j', 'clients')) if c['title'] == f'PaperEvidence-{i}')
        if moved['at'][0] <= x + 80 or moved['size'] != [w, h]:
            raise RuntimeError(('wrong final client geometry', moved['at'], moved['size'], w, h))
        exposed = (x + 8, y + 10, min(dest - 12, x + w - 8), y + h - 10)
        metrics[prefix + '-exposed-trail-mae'] = difference(baseline, trail, exposed)
        if i == 0:
            if other:
                metrics['other-monitor-mae'] = difference(other_native, snap_output('other-during-trail', other))
            hc('dispatch', 'hl.dsp.focus({workspace=9})')
            time.sleep(.20)
            metrics['other-workspace-mae'] = difference(baseline, snap('other-workspace'))
            hc('dispatch', f'hl.dsp.focus({{workspace={workspace}}})')
            time.sleep(.12)
        time.sleep(.3)
        before_close = snap(prefix + '-before-close')
        send('close', i)
        time.sleep(.40)
        early = snap(prefix + '-close-early')
        metrics[prefix + '-trail-after-unmap'] = difference(baseline, early, exposed)
        time.sleep(.44)
        mid = snap(prefix + '-close-mid')
        mx, my = moved['at']
        center = (mx + w//2-10, my + h//2-10, mx + w//2+10, my + h//2+10)
        # Native fadeout is much shorter than the selected burn. This point
        # should still carry the printed image when the native fade is over.
        metrics[prefix + '-close-center-print'] = difference(early, before_close, center)
        metrics[prefix + '-close-progression'] = difference(early, mid, (mx, my, mx+w, my+h))
        time.sleep(.65)
        snap(prefix + '-ash')
        # Closing damage should still be present after the native window is gone.
        if json.loads(hc('-j', 'clients')):
            raise RuntimeError('close retained a mapped client')
        time.sleep(3.5)
        metrics[prefix + '-healed-mae'] = difference(baseline, snap(prefix + '-healed'))
    # Close during opening; overlap two independent owners and unload with live damage.
    for i in range(4, 9):
        send('open', i, width=300+i*18, height=260, image=False)
        time.sleep(.09)
        if i > 4:
            send('close', i-1)
    send('close', 8)
    time.sleep(.25)
    snap('interrupted')
    hc('plugin', 'unload', str(plugin))
    time.sleep(.75)
    metrics['unload-mae'] = difference(baseline, snap('unloaded'))
    if recording:
        recording.send_signal(signal.SIGINT)
        recording.wait(10)
    with plugin.open('rb') as artifact:
        digest = hashlib.file_digest(artifact, 'sha256').hexdigest()
    (out / 'manifest.json').write_text(json.dumps({
        'plugin_sha256': digest,
        'output': output, 'width': monitor['width'], 'height': monitor['height'],
        'other_output': other, 'refresh_hz': monitor['refreshRate'],
    }, indent=2))
    (out / 'measurements.json').write_text(json.dumps(metrics, indent=2))
    print(json.dumps(metrics, indent=2))
    failures = []
    for key, value in metrics.items():
        # Captured print can differ by one sample due to fractional monitor
        # placement. An intact corner is *not* a wallpaper reveal, even if a
        # native fade changes all its colors slightly.
        if '-print' in key or 'corner-wallpaper' in key:
            good = value <= 16
        elif 'trail' in key or 'opening-to-native' in key or 'close-progression' in key:
            good = value >= 1
        else:
            good = value <= .1
        if not good:
            failures.append((key, value))
    if failures:
        raise RuntimeError(('animation comparison failed; inspect retained PNGs', failures))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--plugin', type=Path, default=ROOT / 'build/hyprimate.so')
    parser.add_argument('--record', action='store_true', help='Also save full-output H.264 with wf-recorder')
    parser.add_argument('--single-output', action='store_true',
                        help='Skip auxiliary-output comparison on hosts without a second virtual swapchain')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    procs = []
    with tempfile.TemporaryDirectory(prefix='burn.', dir='/tmp') as temporary:
        try:
            run(out, args.plugin.resolve(strict=True), Path(temporary), procs, args.record, args.single_output)
        finally:
            root = Path(temporary)
            for log in (*root.glob('hypr/*/hyprland.log'), *root.glob('home/cache/hyprland/*CrashReport*')):
                shutil.copyfile(log, out / ('private-' + log.name))
            for p in reversed(procs):
                if p.poll() is None:
                    os.killpg(p.pid, signal.SIGTERM)
                    try:
                        p.wait(3)
                    except sp.TimeoutExpired:
                        os.killpg(p.pid, signal.SIGKILL)
                        p.wait()


if __name__ == '__main__':
    wallpaper() if sys.argv[1:] == ['--wallpaper'] else main()
