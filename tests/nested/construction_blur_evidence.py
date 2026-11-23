#!/usr/bin/env python3
"""Bounded Construction blur regression in private KWin -> Hyprland only.

/usr/bin/python tests/nested/construction_blur_evidence.py /tmp/construction-blur-fixed \
    --plugin build/construction-review/hyprimate.so

Requires the same installed compositor/GTK/capture tools as construction_evidence.py.
Never connects to the host display or IPC. Lossless video, active PNGs, exact native
endpoint comparisons and lifecycle/resource logs are evidence, not a visual gate.
"""
import argparse
import json
from pathlib import Path
import re
import signal
import socket
import subprocess as sp
import sys
import tempfile
import time

import numpy as np
from PIL import Image

import construction_evidence as evidence
from construction_motion_evidence import HeldPointer, peer

CLOCK = time.monotonic_ns
EVENT = re.compile(r'\[hyprimate\] construction event=([a-z][a-z-]*) seed=(\d+) mono_ns=(\d+)')
SIZE = (480, 320)
IDENT = 20
DURATION = 1.2
OPACITY = .82


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class Pointer(HeldPointer):
    def __init__(self, sock, extent, trace, button):
        super().__init__(sock, extent, trace)
        self.code = button

    def button(self, down):
        self.event(2, (self.code, int(down)), 'button', pressed=down, xy=self.position)
        self.pressed = down


class BlurSession(evidence.Session):
    def start(self, log, argv, **kwargs):
        if str(argv[0]) == 'Hyprland':
            config = Path(argv[argv.index('-c') + 1])
            source = config.read_text()
            # Session's empty-output fixture normally disables blur. Change that
            # before the child exists, not by briefly disabling a live window.
            needle = 'blur={enabled=false}'
            require(needle in source, 'Session blur fixture changed; inspect its configuration')
            source = source.replace(needle, 'blur={enabled=true}')
            source += f'''
hl.config({{decoration={{active_opacity={OPACITY},inactive_opacity={OPACITY},
    rounding=22,shadow={{enabled=true,range=12,scale=0.98}},
    blur={{enabled=true,size=3,passes=2}}}},debug={{disable_logs=false,enable_stdout_logs=true}}}})
hl.bind('mouse:272', hl.dsp.window.drag(), {{mouse=true}})
hl.bind('mouse:273', hl.dsp.window.resize(), {{mouse=true}})
'''
            config.write_text(source)
            (self.out/'fixture.lua').write_text(source)
        return super().start(log, argv, **kwargs)

    def capture(self, name):
        path = self.out/(name + '.png')
        sp.run(['grim', '-o', self.output, path], env=self.env, check=True, timeout=8)
        return path

    def settings(self):
        self.hc('eval', 'hl.config({plugin={hyprimate={workspace={enabled=false},window={'
                'enabled=true,style="construction",open={enabled=true,options={construction={duration=1200}}},'
                'close={enabled=true,options={construction={duration=1200}}},'
                'movement={enabled=true,options={construction={variant="crew_carry"}}}}}}})')
        return self.check_settings()

    def check_settings(self):
        wanted = {'decoration:blur:enabled': True, 'decoration:active_opacity': OPACITY,
                  'decoration:inactive_opacity': OPACITY,
                  'plugin:hyprimate:window:enabled': True,
                  'plugin:hyprimate:window:style': 'construction',
                  'plugin:hyprimate:window:open:enabled': True,
                  'plugin:hyprimate:window:close:enabled': True,
                  'plugin:hyprimate:window:movement:enabled': True,
                  'plugin:hyprimate:window:open:options:construction:duration': 1200.,
                  'plugin:hyprimate:window:close:options:construction:duration': 1200.,
                  'plugin:hyprimate:window:movement:options:construction:variant': 'crew_carry'}
        result = {}
        for key, expected in wanted.items():
            raw = json.loads(self.hc('-j', 'getoption', key))
            actual = raw.get('str') if isinstance(expected, str) else (
                raw.get('float') if isinstance(expected, float) else raw.get('int', raw.get('bool')))
            require(actual == expected or (isinstance(expected, float) and actual is not None
                    and abs(actual - expected) < 1e-5), f'Unapplied setting {key}: {raw}')
            result[key] = raw
        return result

    def grab(self, kind, data):
        client = self.lookup(IDENT)
        require(client is not None, f'{kind}: no live specimen')
        x, y = client['at']
        w, h = client['size']
        start = (x + 18, y + 18) if kind == 'resize' else (x + w//2, y + h//2)
        delta = (64, 40) if kind == 'resize' else (150, 70)
        monitor = self.monitor()
        require(monitor['x'] == monitor['y'] == 0 and monitor['scale'] == 1,
                f'Unexpected private output geometry: {monitor}')
        trace = []
        data.update(begin_ns=CLOCK(), initial=client['at'] + client['size'], pointer=trace,
                    options=self.check_settings())
        with socket.socket(socket.AF_UNIX) as sock:
            data['peer'] = peer(sock, self.runtime/self.env['WAYLAND_DISPLAY'], self.hypr.pid)
            pointer = Pointer(sock, (monitor['width'], monitor['height']), trace,
                              0x111 if kind == 'resize' else 0x110)
            try:
                pointer.connect()
                pointer.move(start)
                time.sleep(.08)
                pointer.button(True)
                time.sleep(.16)
                pointer.segment((start[0] + delta[0], start[1] + delta[1]), .65)
                self.capture(kind + '-active')
                active = self.lookup(IDENT)
                require(active is not None, f'{kind}: client disappeared during grab')
                data['held'] = active['at'] + active['size']
                pointer.button(False)
                data['release_ns'] = CLOCK()
            finally:
                pointer.close()
        time.sleep(1.0)
        final = self.lookup(IDENT)
        require(final is not None and final['address'] == client['address'], f'{kind}: changed identity')
        want = ([x + delta[0], y + delta[1], w, h] if kind == 'carry' else
                [x + delta[0], y + delta[1], w - delta[0], h - delta[1]])
        data.update(final=final['at'] + final['size'], expected=want, end_ns=CLOCK())
        require(data['final'] == want, f'{kind}: incorrect final box: {data}')
        self.capture(kind + '-settled')


def pixels(path):
    with Image.open(path) as image:
        return np.array(image.convert('RGB'))


def changed(a, b):
    return int(np.any(a != b, axis=2).sum())


def analyze(out, data):
    log = '\n'.join(path.read_text(errors='replace') for path in
                    (out/'hyprland.log', out/'private-hyprland.log') if path.exists())
    events = [dict(kind=kind, seed=int(seed), mono_ns=int(ns)) for kind, seed, ns in
              sorted(set(EVENT.findall(log)), key=lambda row: int(row[2]))]
    resources = [{key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)}
                 for line in sorted(set(evidence.RESOURCE.findall(log)))]
    result = dict(events=events, resources=resources, stages={}, failed=[], limits=[
        'Synthetic public GTK fixtures only; existing no_screen_share rules are retained, not tested here.',
        'Opacity < 1 plus enabled blur establishes the native shouldBlur preconditions, not a hook counter.',
        'peak_quads proves successful renderer submissions, not GPU timing or matte correctness.',
        'Close has no resource counter; paired lifetime plus changed visible pixels is its geometry witness.',
        'Active PNGs/video require visual review of blur matte shape, clipping and crew exclusion.'])
    failed = result['failed']
    for stage, pair in {'open': ('open', 'end'), 'tiled_open': ('open', 'end'),
                        'carry': ('carry-start', 'carry-end'), 'resize': ('resize-start', 'resize-end'),
                        'close': ('regional-close-start', 'regional-close-end')}.items():
        interval = data['stages'][stage]
        selected = [row for row in events if interval['begin_ns'] <= row['mono_ns'] <= interval['end_ns']]
        starts = [row for row in selected if row['kind'] == pair[0]]
        ends = [row for row in selected if row['kind'] == pair[1]]
        valid = len(starts) == len(ends) == 1 and starts[0]['seed'] == ends[0]['seed']
        if not valid:
            failed.append(f'{stage}: missing unique paired lifecycle: {selected}')
        if stage in ('open', 'tiled_open') and not any(row['kind'] == 'native-seated' for row in selected):
            failed.append(f'{stage}: opening interrupted before native seating')
        rows = [row for row in resources if starts and row.get('seed') == starts[0]['seed']]
        result['stages'][stage] = dict(events=selected, resources=rows)
        if stage != 'close' and not any(row.get('peak_quads', 0) > 0 for row in rows):
            failed.append(f'{stage}: no nonzero rendered geometry with blur enabled')
    for stage in ('open', 'carry', 'resize'):
        diff = changed(pixels(out/(stage+'-settled.png')), pixels(out/(stage+'-native.png')))
        result['stages'][stage]['native_endpoint_diff_pixels'] = diff
        if diff:
            failed.append(f'{stage}: settled pixels differ from native control ({diff})')
    background = pixels(out/'background.png')
    final_diff = changed(pixels(out/'native-closed.png'), pixels(out/'closed.png'))
    result['closed_background_diff_pixels'] = final_diff
    if final_diff:
        failed.append(f'close: desktop differs from native empty control ({final_diff} pixels)')
    for stage, reference in [('open', background), ('close', pixels(out/'resize-settled.png'))]:
        active = pixels(out/(stage+'-active.png'))
        nonempty = changed(active, background)
        displaced = changed(active, reference)
        result['stages'][stage].update(active_nonbackground_pixels=nonempty,
                                       active_reference_diff_pixels=displaced)
        if min(nonempty, displaced) < 100:
            failed.append(f'{stage}: no visible active geometry/change in PNG')
    evidence.save(out/'result.json', result)
    return result


def exercise(session, data):
    data['instance'] = session.boot()
    data['options'] = session.settings()
    errors = [error for error in json.loads(session.hc('-j', 'configerrors')) if error.strip()]
    require(not errors, f'Private config has errors: {errors}')
    monitor = session.monitor()
    require(monitor['scale'] == 1, f'Unexpected scale: {monitor}')
    time.sleep(.2)
    session.capture('background')
    recording = session.start(session.out/'recorder.log', [session.recorder, '--no-dmabuf', '-y', '-D',
        '-o', session.output, '-c', 'libx264rgb', '-p', 'crf=0', '-p', 'preset=ultrafast',
        '-p', 'threads=2', '-f', session.out/'output.mkv'], extra={'WAYLAND_DEBUG': 'client'})
    try:
        evidence.until(lambda: evidence.READY.search((session.out/'recorder.log').read_text(errors='replace')),
                       recording)
        opening = data['stages']['open'] = dict(begin_ns=CLOCK())
        session.send('open', IDENT, width=SIZE[0], height=SIZE[1], construction='dark')
        client = evidence.until(lambda: session.lookup(IDENT), session.scene)
        require(client['size'] == list(SIZE), f'Wrong specimen size: {client}')
        data['address'] = client['address']
        data['no_screen_share'] = session.hc('getprop', 'address:'+client['address'], 'no_screen_share')
        require(data['no_screen_share'] == 'false', 'Synthetic fixture unexpectedly capture-protected')
        time.sleep(.35)
        session.capture('open-active')
        time.sleep(DURATION + .35)
        seated = session.lookup(IDENT)
        opening.update(final=seated['at'] + seated['size'], end_ns=CLOCK())
        session.capture('open-settled')
        for kind in ('carry', 'resize'):
            session.grab(kind, data['stages'].setdefault(kind, {}))
        closing = data['stages']['close'] = dict(begin_ns=CLOCK(), options=session.check_settings())
        session.send('close', IDENT)
        evidence.until(lambda: session.lookup(IDENT) is None, session.scene)
        closing['unmapped_ns'] = CLOCK()
        time.sleep(.30)
        session.capture('close-active')
        time.sleep(DURATION + .4)
        session.capture('closed')
        closing['end_ns'] = CLOCK()
        require(not any(c['mapped'] for c in json.loads(session.hc('-j', 'clients'))),
                'Mapped client remains after close')
    finally:
        evidence.stop(recording, signal.SIGINT)
    require(recording.returncode == 0, f'Recorder failed: {recording.returncode}')
    tiled = data['stages']['tiled_open'] = dict(begin_ns=CLOCK())
    session.hc('eval', 'hl.window_rule({name="construction-test",'
               'match={title="^PaperEvidence-.*$"},float=false})')
    session.send('open', IDENT + 1, width=SIZE[0], height=SIZE[1], construction='dark')
    client = evidence.until(lambda: session.lookup(IDENT + 1), session.scene)
    require(not client['floating'], 'Tiled opening fixture stayed floating')
    time.sleep(.35)
    session.capture('tiled-open-active')
    time.sleep(DURATION + .35)
    tiled['end_ns'] = CLOCK()
    session.capture('tiled-open-settled')
    session.send('close', IDENT + 1)
    evidence.until(lambda: session.lookup(IDENT + 1) is None, session.scene)
    time.sleep(DURATION + .35)
    session.hc('eval', 'hl.window_rule({name="construction-test",'
               'match={title="^PaperEvidence-.*$"},float=true,move="96 96"})')
    # An unloaded-plugin reference retains exactly the same blur/opacity config.
    # All switches stay enabled throughout the animated workload above.
    session.hc('plugin', 'unload', str(session.plugin))
    session.hc('dismissnotify', '-1')
    session.send('open', IDENT, width=SIZE[0], height=SIZE[1], construction='dark')
    evidence.until(lambda: session.lookup(IDENT), session.scene)
    time.sleep(.65)
    for kind in ('open', 'carry', 'resize'):
        x, y, w, h = data['stages'][kind]['final']
        address = session.lookup(IDENT)['address']
        session.hc('eval', f'assert(hl.dispatch(hl.dsp.window.resize({{x={w},y={h},relative=false,window="address:{address}"}})).ok);'
                   f'assert(hl.dispatch(hl.dsp.window.move({{x={x},y={y},relative=false,window="address:{address}"}})).ok)')
        time.sleep(.25)
        native = session.lookup(IDENT)
        require(native['at'] + native['size'] == [x, y, w, h], f'{kind}: native reference box mismatch')
        session.capture(kind+'-native')
    session.send('close', IDENT)
    evidence.until(lambda: session.lookup(IDENT) is None, session.scene)
    time.sleep(.65)
    session.capture('native-closed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--plugin', required=True, type=Path)
    parser.add_argument('--recorder', help='Existing wf-recorder executable')
    args = parser.parse_args()
    plugin = args.plugin.resolve(strict=True)
    recorder = evidence.recorder_path(args.recorder)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    data = dict(plugin=str(plugin), plugin_sha256=evidence.digest(plugin), recorder=str(recorder), stages={})

    def interrupted(signum, _frame):
        raise RuntimeError(f'Interrupted or exceeded 100-second private evidence deadline (signal {signum})')

    for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGALRM):
        signal.signal(signum, interrupted)
    signal.alarm(100)
    try:
        with tempfile.TemporaryDirectory(prefix='cb.', dir='/tmp') as runtime:
            session = BlurSession(Path(runtime), out, plugin, recorder)
            try:
                exercise(session, data)
            finally:
                # Cancel the deadline so cleanup cannot itself be interrupted by it.
                signal.alarm(0)
                session.cleanup()
                evidence.save(out/'manifest.json', data)
        result = analyze(out, data)
        print(json.dumps(dict(output=str(out), failed=result['failed'])))
        return int(bool(result['failed']))
    except Exception as error:
        evidence.save(out/'failure.json', dict(error=str(error), stages=data['stages']))
        print(f'{out}: {error}', file=sys.stderr)
        return 1
    finally:
        signal.alarm(0)


if __name__ == '__main__':
    raise SystemExit(main())
