#!/usr/bin/env python3
"""Capture Burn on actual Wayland clients, in private virtual KWin + Hyprland.

/usr/bin/python tests/nested/burn_evidence.py build/burn-evidence
Needs kwin_wayland, Hyprland/hyprctl, grim, GTK3 + gtk-layer-shell GI, Cairo, PIL.
--record additionally needs wf-recorder and ffprobe.
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


def movement_matrix(hc, send, scene, snap, baseline, difference, metrics, out, compositor_pid):
    """Exercise addressed native windows against an independently captured wallpaper."""
    def settings(fields):
        hc('eval', 'hl.config({plugin={hyprimate={window={' + fields + '}}}})')

    def option(path):
        return json.loads(hc('-j', 'getoption', 'plugin:hyprimate:window:' + path))['float']

    smoke = 'movement:options:burn:smoke_density'
    embers = 'options:burn:ember_density'
    original = option(smoke), option(embers)
    lifetime = option('options:burn:scorch_lifetime')
    healing = option('options:burn:healing_duration')
    settings('open={enabled=false},close={enabled=false},movement={enabled=false}')
    hc('eval', 'hl.animation({leaf="windowsMove",enabled=false})')
    width, height = baseline.size
    # Two disjoint lanes: a wide painted window above a tall printed one.
    start = [(32, 32), (width - 164, height // 2 + 30)]
    sizes = [(min(282, width // 4), min(158, height // 5)), (132, min(262, height // 3))]
    ids = (20, 21)
    clients = {}
    def lookup(ident):
        return next((c for c in json.loads(hc('-j', 'clients'))
                     if c['title'] == f'PaperEvidence-{ident}' and c['mapped']), None)

    for n, ident in enumerate(ids):
        w, h = sizes[n]
        send('open', ident, width=w, height=h, image=n == 0)
        clients[n] = until(lambda: lookup(ident), scene)['address']

    def place(*moves):
        # One IPC request: both owners move before the next repaint.
        hc('eval', ';'.join(
            f'assert(hl.dispatch(hl.dsp.window.move({{x={x},y={y},relative=false,window="address:{clients[n]}"}})).ok)'
            for n, (x, y) in moves))

    place(*enumerate(start))
    time.sleep(.65)
    native = snap('movement-native')
    def rect(xy, size):
        x, y = xy
        return x, y, x + size[0], y + size[1]

    references = [native.crop(rect(xy, size)) for xy, size in zip(start, sizes)]
    settings('movement={enabled=true,options={burn={smoke_density=0}}},options={burn={ember_density=0}}')
    if (option(smoke), option(embers)) != (0, 0):
        raise RuntimeError('movement evidence requires smoke and embers both disabled')
    samples = []
    epoch = time.monotonic()
    def sample(name, positions):
        before = time.monotonic()
        image = snap('movement-' + name)
        after = time.monotonic()
        boxes = []
        for n, xy in enumerate(positions):
            client = lookup(ids[n])
            if not client or client['at'] != list(xy) or client['size'] != list(sizes[n]):
                raise RuntimeError(('wrong movement geometry', name, ids[n], xy, sizes[n], client))
            box = rect(xy, sizes[n])
            boxes.append(box)
            # The compositor quantizes the inactive client's colors one code
            # value differently across positions. Check every channel, not MAE:
            # this still rejects a single covered glyph or wallpaper pixel.
            delta = ImageChops.difference(references[n].convert('RGB'), image.crop(box).convert('RGB'))
            metrics[f'movement-{name}-native-max-{n}'] = max(hi for _, hi in delta.getextrema())
        # Screencopy wall times identify the sample, not GPU frame times.
        samples.append(dict(name=name, capture_interval_s=[before-epoch, after-epoch], boxes=boxes))
        return image

    probes = {}
    def exposed(name, image, region):
        metrics[f'movement-{name}-trail'] = difference(baseline, image, region)
        # Preserve both the footprint and its signed contrast. The same mean
        # error could otherwise describe a few sparks or an opaque rectangle.
        bg = baseline.crop(region).convert('L')
        shown = image.crop(region).convert('L')
        pixels = bg.width * bg.height
        probes[name] = dict(region=region,
            darker_fraction=sum(ImageChops.subtract(bg, shown).histogram()[12:]) / pixels,
            lighter_fraction=sum(ImageChops.subtract(shown, bg).histogram()[12:]) / pixels)

    distance = min(width // 5, 180)
    for step in range(1, 13):
        dx = distance * step // 12
        pair = [(start[0][0] + dx, start[0][1]), (start[1][0] - dx, start[1][1])]
        place(*enumerate(pair))
        time.sleep(.045)
        if step in (6, 12):
            frame = sample(f'horizontal-{step}', pair)
            for n, ((x, y), (w, h)) in enumerate(zip(start, sizes)):
                left, right = (x, x + dx) if n == 0 else (x + w - dx, x + w)
                exposed(f'horizontal-{step}-{n}', frame, (left + 6, y + 8, right - 6, y + h - 8))

    x, y = pair[0]
    w, h = sizes[0]
    end = (width - w - 32, y + height // 6)
    final = [end, pair[1]]
    # Between both endpoints, not under either window or in the preceding
    # horizontal trail. A jump that stamps only the endpoint must fail.
    cx = (x + w + end[0]) // 2
    cy = (y + end[1] + h) // 2
    half_width = min(60, (end[0] - x - w) // 3)
    corridor = (cx - half_width, cy - h // 4, cx + half_width, cy + h // 4)
    place((0, end))
    time.sleep(.10)
    jumped = sample('diagonal', final)
    exposed('diagonal', jumped, corridor)
    time.sleep(lifetime + healing * .55)
    repairing = sample('repairing', final)
    exposed('repairing', repairing, corridor)
    place((0, pair[0]))
    time.sleep(.10)
    reversed_frame = sample('reverse', pair)
    exposed('reverse', reversed_frame, corridor)
    place((0, end))
    time.sleep(.10)
    reheated = sample('reheated', final)
    exposed('reheated', reheated, corridor)
    # Hold beyond the first stamp's expiry. Ordinary repair cannot explain a
    # surviving mark in the jump corridor at this point.
    time.sleep(healing * .45 + .3)
    held = sample('renewed-hold', final)
    exposed('renewed-hold', held, corridor)
    metrics['movement-untouched'] = difference(baseline, held, (8, height // 2, width // 4, height - 16))
    metrics['movement-reheating-trail-change'] = difference(repairing, reheated, corridor)
    # Stress the same recovering material with two owners, without screencopy or
    # configuration changes in the loop. RSS includes the compositor and driver pools.
    def rss_kib():
        status = Path(f'/proc/{compositor_pid}/status').read_text()
        return int(next(row.split()[1] for row in status.splitlines() if row.startswith('VmRSS:')))

    stress_rss = [rss_kib()]
    for turn in range(32):
        positions = (pair[0], start[1]) if turn % 2 == 0 else final
        place(*enumerate(positions))
        time.sleep(.045)
        if turn % 8 == 7:
            stress_rss.append(rss_kib())
    sample('repeat-stop', final)
    time.sleep(lifetime + healing + .75)
    stopped = sample('stopped', final)
    metrics['movement-restored-corridor'] = difference(baseline, stopped, corridor)
    # Require the whole exposed image (not just the centre probe) to restore.
    expected = baseline.copy()
    for ref, xy in zip(references, final):
        expected.paste(ref, xy)
    metrics['movement-stopped-full-mae'] = difference(expected, stopped)
    (out / 'movement-samples.json').write_text(json.dumps(dict(
        smoke_density=0, ember_density=0, lifetime=lifetime, healing=healing,
        corridor=corridor, probes=probes, samples=samples, repeat_moves=32,
        compositor_rss_kib=stress_rss, settled_rss_kib=rss_kib()), indent=2))
    after_stop = time.monotonic()
    for ident in ids:
        send('close', ident)
    until(lambda: not any(lookup(ident) for ident in ids), scene)
    settings('open={enabled=true},close={enabled=true},movement={options={burn={smoke_density='
             + str(original[0]) + '}}},options={burn={ember_density=' + str(original[1]) + '}}')
    return epoch, after_stop


def recording_cadence(video, started, movement_interval):
    # Frame PTS are measured, but alignment to the Python controller includes
    # recorder startup latency. Never substitute capture cadence for GPU timing.
    raw = sp.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_frames',
                           '-show_entries', 'frame=best_effort_timestamp_time', '-of', 'json', video])
    pts = [float(f['best_effort_timestamp_time']) for f in json.loads(raw)['frames']]
    if len(pts) < 2:
        raise RuntimeError('recording did not contain multiple frames')
    start, stop = movement_interval
    selected = [t for t in pts if start - started <= t - pts[0] <= stop - started]
    gaps = sorted((b - a) * 1000 for a, b in zip(selected, selected[1:]))
    return dict(frames=len(pts), movement_frames=len(selected), movement_pts_s=selected,
                median_gap_ms=gaps[len(gaps)//2] if gaps else None,
                p95_gap_ms=gaps[int(.95 * (len(gaps)-1))] if gaps else None,
                note='Recorded frame spacing only; approximate movement interval; not compositor render time.')


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
    until(lambda: 'wl_output' in sp.run(['wayland-info'], env=env, text=True,
                                        capture_output=True, timeout=5).stdout, parent)
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
        sp.run(['grim', '-l', '1', '-o', target, path], env=env, check=True, timeout=6)
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
        recording_start = time.monotonic()
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
    movement_interval = movement_matrix(hc, send, scene, snap, baseline, difference, metrics, out, compositor.pid)
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
        if recording.wait(10) != 0:
            raise RuntimeError('recorder failed; see recorder.log')
        cadence = recording_cadence(out / 'burn.mkv', recording_start, movement_interval)
        (out / 'recording-cadence.json').write_text(json.dumps(cadence, indent=2))
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
        elif 'native-max-' in key:
            good = value <= 1
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
