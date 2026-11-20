#!/usr/bin/env python3
"""Capture real production paper in disposable virtual KWin/Hyprland, never the host.

Baseline: python tests/nested/paper_evidence.py --label baseline
New build: python tests/nested/paper_evidence.py --label production --matrix
Output includes lossless VFR MKV, timestamped commands, raw frame timestamps,
source/config readback, native-endpoint screenshots and image error metrics.
Cadence is presentation PTS, NOT a claim about CPU solver or GPU pass duration.

Dependencies: Hyprland/hyprctl, virtual kwin_wayland, grim, wf-recorder,
ffmpeg/ffprobe, and /usr/bin/python with PyGObject (GTK 3), pycairo and PIL.
Recorder resolution: WF_RECORDER override, then installed PATH wf-recorder,
then the known repo-local .project-manager/tools/usr/bin/wf-recorder fallback.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import statistics
import subprocess as sp
import tempfile
import time
from paper_cadence import analyze

ROOT = Path(__file__).resolve().parents[2]


def _replay_seed(text):
    if not text or not text.isascii() or not text.isdigit():
        raise argparse.ArgumentTypeError('Replay seed must be an unsigned uint64 decimal')
    significant = text.lstrip('0') or '0'
    if len(significant) > 20:
        raise argparse.ArgumentTypeError('Replay seed exceeds uint64')
    value = int(significant)
    if value > (1 << 64) - 1:
        raise argparse.ArgumentTypeError('Replay seed exceeds uint64')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label', required=True)
    parser.add_argument('--plugin', type=Path, default=ROOT / 'build/hyprimate.so')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/paper-evidence')
    parser.add_argument('--matrix', action='store_true')
    parser.add_argument('--quick', action='store_true', help='Native printed-text endpoint plus reversal only')
    parser.add_argument('--close-only', action='store_true', help='Map natively, then exercise production closing only')
    parser.add_argument('--replay-seed', type=_replay_seed, help='Private uint64 first-begin seed; never inherited from host environment')
    parser.add_argument('--directional-left', action='store_true', help='Only first-job 620x620 mixed .55/left/directional open+close, no reversal')
    parser.add_argument('--interactions', action='store_true', help='Exercise live geometry and compare settled frames against native rendering before the lifecycle cases')
    parser.add_argument('--unfolding', action='store_true', help='Record short, standard and long openings for a paired release comparison')
    parser.add_argument('--settle', type=float, default=3.0, help='Minimum seconds from map request to settled capture')
    args = parser.parse_args()
    if args.directional_left and (args.matrix or args.quick or args.close_only):
        parser.error('--directional-left cannot be combined with --matrix, --quick or --close-only')
    if args.unfolding and (args.matrix or args.quick or args.close_only or args.directional_left):
        parser.error('--unfolding selects its own opening cases')
    if not shutil.which('grim'):
        raise RuntimeError('grim is required for real native endpoint comparisons')
    override = os.environ.get('WF_RECORDER')
    recorder = shutil.which(override or 'wf-recorder')
    if override and not recorder:
        raise RuntimeError(f'WF_RECORDER does not name an executable: {override}')
    if not recorder:
        fallback = ROOT / '.project-manager/tools/usr/bin/wf-recorder'
        if fallback.is_file() and os.access(fallback, os.X_OK):
            recorder = str(fallback)
    if not recorder:
        raise RuntimeError('wf-recorder is required; install it or set WF_RECORDER')
    out = args.output.resolve() / args.label
    out.mkdir(parents=True, exist_ok=False)
    plugin = out / 'hyprimate.so'
    shutil.copy2(args.plugin, plugin)
    # Linux AF_UNIX paths include Hyprland's long instance signature.
    work = Path(tempfile.mkdtemp(prefix='p.', dir='/tmp'))
    run = work
    home = work / 'home'
    home.mkdir()
    env = dict(os.environ, XDG_RUNTIME_DIR=str(run), HOME=str(home),
               XDG_CONFIG_HOME=str(home / 'config'), XDG_DATA_HOME=str(home / 'data'),
               XDG_CACHE_HOME=str(home / 'cache'), XDG_STATE_HOME=str(home / 'state'),
               QT_QPA_PLATFORM='wayland', GDK_BACKEND='wayland', NO_AT_BRIDGE='1',
               GTK_A11Y='none', XDG_SESSION_TYPE='wayland')
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE', 'WAYLAND_SOCKET',
                'SESSION_MANAGER', 'DBUS_SESSION_BUS_ADDRESS', 'DBUS_SESSION_BUS_PID',
                'DBUS_STARTER_ADDRESS', 'HYPRIMATE_PAPER_REPLAY_SEED'):
        env.pop(key, None)
    if args.replay_seed is not None:
        env['HYPRIMATE_PAPER_REPLAY_SEED'] = str(args.replay_seed)
    procs = []
    events = []
    failures = []
    clock = time.monotonic()
    manifest = {'plugin_source': str(args.plugin.resolve()),
        'plugin_sha256': hashlib.sha256(plugin.read_bytes()).hexdigest(), 'private_runtime': str(run),
        'capture': 'lossless libx264rgb VFR; PTS cadence is not per-pass timing',
        'recorder': str(Path(recorder).resolve()),
        'matrix': args.matrix, 'opening_enabled': not args.close_only,
        'directional_left': args.directional_left,
        'unfolding': args.unfolding,
        'replay_seed': str(args.replay_seed) if args.replay_seed is not None else None,
        'replay_semantics': 'First PaperWorkers begin uses the exact seed; later begin calls increment it, not necessarily once per window.',
        'capture_dimensions': None}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2))

    def event(kind, **values):
        events.append(dict(t=time.monotonic() - clock, kind=kind, **values))
        (out / 'events.json').write_text(json.dumps(events, indent=2))

    def start(name, command, stdin=None):
        with (out / (name + '.log')).open('w') as log:
            p = sp.Popen([str(x) for x in command], env=env, stdin=stdin,
                         stdout=log, stderr=sp.STDOUT, start_new_session=True)
        procs.append(p)
        return p

    def until(fn, process):
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'process {process.pid} exited {process.returncode}; see logs')
            value = fn()
            if value:
                return value
            time.sleep(.1)
        raise TimeoutError('nested session readiness timeout')

    try:
        parent = start('kwin', ['kwin_wayland', '--virtual', '--width', '1280', '--height', '960',
            '--no-global-shortcuts', '--socket', 'paper-parent'])
        until(lambda: (run / 'paper-parent').is_socket(), parent)
        env.update(WAYLAND_DISPLAY='paper-parent', AQ_BACKENDS='wayland')
        config = out / 'hyprland.lua'
        shutil.copy2(ROOT / 'tests/nested/hyprland.lua', config)
        compositor = start('hyprland', ['Hyprland', '-c', config])

        def identify():
            reply = sp.run(['hyprctl', 'instances', '-j'], env=env, capture_output=True, text=True).stdout.strip()
            if not reply.startswith('['):
                return None
            return next((x for x in json.loads(reply) if x['pid'] == compositor.pid and
                (run / x['wl_socket']).is_socket() and
                (run / 'hypr' / x['instance'] / '.socket.sock').is_socket()), None)

        inst = until(identify, compositor)
        env.update(HYPRLAND_INSTANCE_SIGNATURE=inst['instance'], WAYLAND_DISPLAY=inst['wl_socket'])
        assert ('XDG_RUNTIME_DIR=' + str(run)).encode() in Path(f'/proc/{compositor.pid}/environ').read_bytes().split(b'\0')

        def hc(*command):
            result = sp.check_output(['hyprctl', '-i', inst['instance'], *command], env=env, text=True).strip()
            event('hyprctl', command=command, response=result)
            if command[0] in ('eval', 'plugin', 'dispatch') and result != 'ok':
                raise RuntimeError((command, result))
            return result

        def monitor_ready():
            monitors = json.loads(hc('-j', 'monitors'))
            return monitors if monitors and all(m['width'] > 0 and m['height'] > 0 for m in monitors) else None

        monitors = until(monitor_ready, compositor)
        manifest['capture_dimensions'] = [dict(name=m['name'], width=m['width'], height=m['height'], scale=m['scale']) for m in monitors]
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2))
        event('output-coverage', monitors=monitors, skipped=['fractional scale/HiDPI', 'multi-output', 'host desktop input'])
        hc('plugin', 'load', str(plugin))
        hc('eval', 'hl.config({cursor={invisible=true},general={border_size=0},decoration={rounding=0,shadow={enabled=false},blur={enabled=false}}})')
        hc('eval', 'hl.window_rule({name="paper-evidence",match={title="^PaperEvidence-.*$"},float=true,move="40 40"})')
        hc('dismissnotify', '-1')

        def options(settings='duration=1.2', close_duration=None):
            opening = 'false' if args.close_only else 'true'
            duration = dict(item.split('=', 1) for item in settings.split(','))['duration']
            closing_duration = duration if close_duration is None else str(close_duration)
            source = 'hl.config({plugin={hyprimate={workspace={enabled=false},window={enabled=true,style="paper",movement={enabled=false},open={enabled=' + opening + ',options={paper={duration=' + duration + '}}},close={enabled=true,options={paper={duration=' + closing_duration + '}}},options={paper={' + settings + '}}}}}})'
            hc('eval', source)
            with (out / 'source-config.lua').open('a') as f:
                f.write(source + '\n')
            # getoption is the compositor's actual native configuration readback.
            keys = {'duration', 'crumpliness', 'variation', 'shading'}
            expected = dict(item.split('=', 1) for item in settings.split(','))
            keys.update(expected)
            for key in sorted(keys):
                readback = hc('-j', 'getoption', 'plugin:hyprimate:window:options:paper:' + key)
                event('config-readback', key=key, value=json.loads(readback))
                (out / ('readback-' + key + '.json')).write_text(readback)
                if key in expected:
                    desired = expected[key].strip('\"')
                    actual = json.loads(readback)
                    value = next((actual[k] for k in ('float', 'int', 'str') if k in actual), None)
                    matches = (value == desired if expected[key].startswith('\"')
                               else isinstance(value, (int, float)) and abs(value - float(desired)) < 1e-5)
                    if not actual.get('set') or not matches:
                        raise RuntimeError(('configuration readback mismatch', key, desired, actual))
            for phase, desired in (('open', float(duration)), ('close', float(closing_duration))):
                actual = json.loads(hc('-j', 'getoption', f'plugin:hyprimate:window:{phase}:options:paper:duration'))
                event('phase-duration-readback', phase=phase, desired=desired, actual=actual)
                if abs(actual.get('float', -1) - desired) > 1e-5:
                    raise RuntimeError(('phase duration readback mismatch', phase, desired, actual))
            return float(duration)

        options()
        scene = start('scene', ['/usr/bin/python', ROOT / 'tests/nested/paper_scene.py'], stdin=sp.PIPE)

        def send(command):
            event('scene', command=command)
            scene.stdin.write((json.dumps(command) + '\n').encode())
            scene.stdin.flush()

        # Keep compositor-provided PTS unchanged; do not force an encoder frame rate.
        # -D requests frames even without damage, avoiding recorder shutdown deadlock.
        # Avoid one lossless encoder thread per logical CPU competing with the
        # solver on busy hosts during the matched opening measurement.
        encoder_threads = ['-p', 'threads=2'] if args.unfolding else []
        capture = start('capture', [recorder, '-y', '-D', '-o', 'WAYLAND-1', '-c', 'libx264rgb',
            '-p', 'crf=0', '-p', 'preset=ultrafast', *encoder_threads, '-f', out / 'capture.mkv'])
        event('capture-start')
        time.sleep(.8)
        if args.interactions:
            from paper_interactions import exercise
            exercise(hc, send, event, env, out)
            options()
        index = 0
        cases = [('concurrent-dense', 560, 280, False, 4, ''),
                 ('native-text', 620, 620, False, 1, ''),
                 ('native-image', 620, 620, True, 1, '')]
        if args.quick or args.close_only:
            cases = [cases[1]]
        if args.directional_left:
            cases = [('directional-left', 620, 620, True, 1,
                'duration=1.2,fold_density=.55,crumple_origin=\"left\",force_direction=\"directional\"')]
        if args.unfolding:
            cases = [('long-print', 760, 540, False, 1, 'duration=3,fold_density=.55'),
                     ('wide-print', 1050, 300, False, 1, 'duration=.6,fold_density=.2'),
                     ('tall-mixed', 320, 640, True, 1, 'duration=1.2,fold_density=.9,crumple_origin="left"')]
        if args.matrix:
            for name, width, height, image in [('small', 240, 160, False), ('large', 1160, 620, True),
                    ('wide', 1100, 240, False), ('tall', 260, 620, True), ('square', 620, 620, True)]:
                for seed in range(3):
                    settings = ('duration=1.2,fold_density=' + str([.15, .55, 1][seed]) +
                        ',crumple_origin="' + ['center', 'left', 'random'][seed] + '",force_direction="' +
                        ['inward', 'directional', 'rotational'][seed] + '",simulation_quality="adaptive"')
                    cases.append((name + '-seed-sequence-' + str(seed), width, height, seed != 0, 1, settings))
            for shade, name in ((1, 'shading-high'), (0, 'shading-zero')):
                cases.append((name, 980, 620, False, 1,
                    f'duration=1.2,variation=0,fold_density=.8,crumple_origin=\"center\",'
                    f'force_direction=\"inward\",shading={shade}'))
            cases.append(('cursor-origin', 760, 540, False, 1,
                'duration=1.2,fold_density=.8,crumple_origin=\"cursor\",force_direction=\"directional\",shading=.7'))
            cases.append(('production-defaults', 760, 540, False, 1,
                'duration=.6,crumpliness=.6,shading=.7,variation=.5,fold_density=.55,'
                'paper_stiffness=.72,crumple_size=.18,crumple_origin=\"center\",'
                'force_direction=\"inward\",rotation_intensity=.22,settling=.45,simulation_quality=\"adaptive\"'))
        for name, width, height, image, count, settings in cases:
            if name == 'cursor-origin':
                hc('dispatch', 'hl.dsp.cursor.move({x=170,y=230})')
                pointer = json.loads(hc('-j', 'cursorpos'))
                event('checked-cursor-origin', requested=[170, 230], actual=pointer)
                if abs(pointer['x'] - 170) > .5 or abs(pointer['y'] - 230) > .5:
                    raise RuntimeError(('private pointer position mismatch', pointer))
            close_duration = .5 if args.unfolding or name == 'production-defaults' else None
            opening_duration = options(settings or 'duration=1.2', close_duration=close_duration)
            ids = list(range(index, index + count))
            index += count
            event('case', name=name, ids=ids, width=width, height=height,
                  seed_note='Production assigns seeds; distinct windows exercise successive seeds, not an injected seed.')
            if name.startswith('shading-'):
                event('shading-comparison', name=name, content_id=42,
                      limitation='Identical printed content and settings except shading; production assigns different seeds. Not a same-pose A/B proof.')
            for n, ident in enumerate(ids):
                hc('eval', f'hl.window_rule({{name="paper-{ident}",match={{title="^PaperEvidence-{ident}$"}},float=true,move="{40 + (n % 2) * 610} {40 + (n // 2) * 340}"}})')
                send(dict(op='open', id=ident, width=width, height=height, image=image,
                          content_id=42 if name.startswith('shading-') else ident))
            nominal_hold = opening_duration + .05
            time.sleep(nominal_hold)
            if count == 1:
                sp.run(['grim', str(out / (name + '-nominal-end.png'))], env=env, check=True)
            event('nominal-opening-hold', name=name, seconds=nominal_hold)
            time.sleep(max(0, args.settle - nominal_hold))
            clients = json.loads(hc('-j', 'clients'))
            if len(clients) != count:
                raise RuntimeError(('expected mapped clients', count, clients))
            event('mapped-open-hold', name=name, seconds=max(nominal_hold, args.settle))
            if count == 1 and shutil.which('grim'):
                actual = out / (name + '-settled.png')
                native = out / (name + '-native.png')
                sp.run(['grim', str(actual)], env=env, check=True)
                hc('eval', 'hl.config({plugin={hyprimate={window={enabled=false}}}})')
                time.sleep(.15)
                sp.run(['grim', str(native)], env=env, check=True)
                from PIL import Image, ImageChops, ImageStat
                nominal = Image.open(out / (name + '-nominal-end.png')).convert('RGB')
                native_rgb = Image.open(native).convert('RGB')
                nominal_error = ImageStat.Stat(ImageChops.difference(nominal, native_rgb)).mean
                event('nominal-open-comparison', name=name, at_seconds=nominal_hold, mean_absolute_rgb=nominal_error)
                difference = ImageChops.difference(Image.open(actual).convert('RGB'), Image.open(native).convert('RGB'))
                stats = ImageStat.Stat(difference)
                event('endpoint-comparison', name=name, mean_absolute_rgb=stats.mean, bounds=difference.getbbox())
                if max(stats.mean) > .1:
                    failures.append(dict(case=name, check='native-open-endpoint', mean_absolute_rgb=stats.mean))
                # Restore the complete source settings, not only the enable flag.
                options(settings or 'duration=1.2', close_duration=close_duration)
            for ident in ids:
                send(dict(op='close', id=ident))
            time.sleep(1.8)
            if json.loads(hc('-j', 'clients')):
                raise RuntimeError('native close endpoint still has clients')
            event('native-close-endpoint', name=name)
        if not args.close_only and not args.directional_left:
            # Close during an opening: exercise the production reversal path.
            send(dict(op='open', id=index, width=760, height=540, image=False))
            time.sleep(.38)
            send(dict(op='close', id=index))
            event('reversal')
            time.sleep(1.8)
            if json.loads(hc('-j', 'clients')):
                raise RuntimeError('reversal did not reach native closed endpoint')
        if args.matrix:
            options('duration=1.2,fold_density=.8,shading=.7')
            event('burst-start', count=24, delays=[0, .005, .03],
                  limitation='Map/unmap requests include zero-delay windows; first-pose publication is not directly instrumented.')
            for burst in range(24):
                ident = index + 1 + burst
                send(dict(op='open', id=ident, width=560, height=280, image=bool(burst % 2)))
                delay = (0, .005, .03)[burst % 3]
                if delay:
                    time.sleep(delay)
                send(dict(op='close', id=ident))
            index += 25
            time.sleep(2)
            if json.loads(hc('-j', 'clients')):
                raise RuntimeError('burst left mapped native clients')
            event('burst-end', compositor_alive=compositor.poll() is None)
            for cycle in range(3):
                ident = index + cycle
                send(dict(op='open', id=ident, width=1100, height=620, image=True))
                until(lambda: len(json.loads(hc('-j', 'clients'))) == 1, compositor)
                time.sleep(.01)
                event('active-animation-unload', cycle=cycle,
                      limitation='Unload requested during nominal opening; unfinished worker task is not directly instrumented.')
                hc('plugin', 'unload', str(plugin))
                if compositor.poll() is not None:
                    raise RuntimeError('private compositor exited during plugin unload')
                hc('plugin', 'load', str(plugin))
                options('duration=1.2,fold_density=.8,shading=.7')
                send(dict(op='close', id=ident))
                time.sleep(1.8)
                if json.loads(hc('-j', 'clients')):
                    raise RuntimeError('reload cycle left mapped native client')
                event('active-animation-reload-end', cycle=cycle)
        capture.send_signal(signal.SIGINT)
        if capture.wait(20) != 0:
            raise RuntimeError('recorder failed')
        event('capture-stop')
        print(f'RECORDING_STOPPED {out}', flush=True)
        frames = json.loads(sp.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
            '-show_frames', '-show_entries', 'frame=best_effort_timestamp_time,pkt_duration_time', '-of', 'json', out / 'capture.mkv']))
        (out / 'frames.json').write_text(json.dumps(frames, indent=2))
        pts = [float(f['best_effort_timestamp_time']) for f in frames['frames']]
        gaps = sorted(b - a for a, b in zip(pts, pts[1:]))
        summary = {'frames': len(pts), 'span_seconds': pts[-1] - pts[0],
            'cadence_gap_ms': {'median': statistics.median(gaps) * 1000,
                'p95': gaps[int((len(gaps) - 1) * .95)] * 1000, 'max': max(gaps) * 1000},
            'warning': 'VFR includes idle intervals. Use events and raw PTS to isolate animation cadence; this is not per-pass timing.'}
        capture_start = next(e['t'] for e in events if e['kind'] == 'capture-start')
        animation_cadence = []
        durations = {}
        for e in events:
            if e['kind'] == 'phase-duration-readback':
                durations[e['phase']] = e['actual']['float']
            if e['kind'] != 'scene' or e['command']['op'] not in ('open', 'close'):
                continue
            begin = e['t'] - capture_start
            end = begin + durations[e['command']['op']]
            intervals = sorted((b - a) * 1000 for a, b in zip(pts, pts[1:])
                               if begin <= a <= end and b <= end)
            animation_cadence.append(dict(id=e['command']['id'], op=e['command']['op'],
                approximate_video_start=begin, frames=len(intervals),
                median_ms=statistics.median(intervals) if intervals else None,
                p95_ms=intervals[int((len(intervals) - 1) * .95)] if intervals else None,
                max_ms=max(intervals) if intervals else None))
        summary['animation_cadence'] = animation_cadence
        changed_cadence = analyze(out)
        summary['presentation_stalls_over_100ms'] = changed_cadence['presentation_stalls_over_100ms']
        summary['automated_check_scope'] = 'Native endpoint and lifecycle checks only; cadence warnings and shading require separate review.'
        (out / 'cadence.json').write_text(json.dumps(summary, indent=2))
        stage_times = set((.8, 1.0, 1.3, 1.7, 2.4, 2.8, 3.1, 3.5))
        if args.matrix or args.directional_left:
            for e in events:
                if e['kind'] == 'scene' and e['command']['op'] in ('open', 'close'):
                    stage_times.update(round(e['t'] - capture_start + phase, 3)
                                       for phase in (.15, .5, .9))
        for second in sorted(stage_times):
            if second < pts[-1]:
                sp.run(['ffmpeg', '-v', 'error', '-ss', str(second), '-i', out / 'capture.mkv',
                    '-frames:v', '1', '-y', out / f'stage-{second:.3f}.png'], check=True)
        event('complete', passed=not failures, failures=failures, summary=summary)
        print(json.dumps(dict(output=str(out), passed=not failures, failures=failures, **summary), indent=2), flush=True)
        if failures:
            raise RuntimeError(('paper acceptance checks failed; all capture evidence retained', failures))
    finally:
        for process in reversed(procs):
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(5)
                except sp.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
        # Preserve compositor-private diagnostics before disposing its runtime.
        for diagnostic in work.rglob('*'):
            if diagnostic.is_file() and (diagnostic.suffix == '.log' or 'CrashReport' in diagnostic.name):
                shutil.copy2(diagnostic, out / ('runtime-' + diagnostic.name))
        shutil.rmtree(work)


if __name__ == '__main__':
    main()
