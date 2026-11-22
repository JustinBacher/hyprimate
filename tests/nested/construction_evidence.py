#!/usr/bin/env python3
"""Film the Construction installation in a disposable KWin → Hyprland session.

/usr/bin/python tests/nested/construction_evidence.py build/construction-slice-01 \
    --plugin build/construction-review/hyprimate.so --slice --record

The first output is an unannotated lossless VFR clip; analysis runs after filming.
Never select a host output/socket. A completed run is not a visual gate verdict.
"""
import argparse
import bisect
import contextlib
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess as sp
import sys
import tempfile
import time

import numpy as np
from PIL import Image, ImageDraw

from burn_evidence import ROOT, until

HERE = Path(__file__).resolve().parent
CLOCK = time.monotonic_ns
READY = re.compile(r'zwlr_screencopy_frame_v1[@#]\d+\.ready\((\d+),\s*(\d+),\s*(\d+)\)')
RESOURCE = re.compile(r'\[hyprimate\] construction resources seed=\d+(?: \w+=\d+)+')
EVENT = re.compile(r'\[hyprimate\] construction event=(open|native-seated|end) seed=(\d+) mono_ns=(\d+)')


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def recorder_path(override):
    paths = [override] if override else ['wf-recorder', ROOT / '.project-manager/tools/usr/bin/wf-recorder',
                                        Path.home() / 'projects/hypr-bubble/.project-manager/tools/usr/bin/wf-recorder']
    for path in paths:
        if found := shutil.which(str(path)):
            return Path(found).resolve()
    raise RuntimeError(f'No executable recorder: {paths}; pass --recorder PATH (no install).')


def stop(p, sig=signal.SIGTERM):
    # All launches use their own session. Kill descendants even if a supervisor exited.
    with contextlib.suppress(ProcessLookupError):
        os.killpg(p.pid, sig)
    try:
        p.wait(timeout=6)
    except sp.TimeoutExpired:
        with contextlib.suppress(ProcessLookupError):
            os.killpg(p.pid, signal.SIGKILL)
        p.wait()


class Session:
    def __init__(self, runtime, out, plugin, recorder):
        self.runtime, self.out, self.plugin, self.recorder = runtime, out, plugin, recorder
        self.processes, self.cases = [], []
        home = runtime / 'home'
        home.mkdir()
        self.env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), HOME=str(home),
                        GDK_BACKEND='wayland', QT_QPA_PLATFORM='wayland', NO_AT_BRIDGE='1',
                        GTK_A11Y='none', XDG_SESSION_TYPE='wayland', HYPRLAND_NO_SD_VARS='1',
                        HYPRLAND_NO_SD_NOTIFY='1', XDG_CONFIG_HOME=str(home/'config'),
                        XDG_CACHE_HOME=str(home/'cache'), XDG_DATA_HOME=str(home/'data'),
                        XDG_STATE_HOME=str(home/'state'))
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'DBUS_SESSION_BUS_ADDRESS',
                    'HYPRLAND_INSTANCE_SIGNATURE', 'SESSION_MANAGER', 'DBUS_STARTER_ADDRESS',
                    'DBUS_STARTER_BUS_TYPE', 'DBUS_SESSION_BUS_PID', 'AQ_BACKENDS', 'WAYLAND_DEBUG'):
            self.env.pop(key, None)

    def start(self, log, argv, *, stdin=None, extra=None):
        with log.open('w') as stream:
            process = sp.Popen(argv, stdin=stdin, stdout=stream, stderr=sp.STDOUT,
                               env=self.env | (extra or {}), start_new_session=True)
        self.processes.append(process)
        return process

    def hc(self, *args):
        if self.hypr.poll() is not None:
            raise RuntimeError('private compositor exited')
        reply = sp.check_output(['hyprctl', '-i', self.signature, *args], env=self.env,
                                timeout=8, text=True).strip()
        if args[0] in ('eval', 'plugin') and reply != 'ok':
            raise RuntimeError((args, reply))
        return reply

    def boot(self):
        parent = self.start(self.out/'kwin.log', ['dbus-run-session', '--', 'kwin_wayland', '--virtual',
                            '--width', '1280', '--height', '960', '--no-lockscreen',
                            '--no-global-shortcuts', '--socket', 'construction-parent'])
        until(lambda: (self.runtime/'construction-parent').is_socket(), parent)
        self.env.update(WAYLAND_DISPLAY='construction-parent', AQ_BACKENDS='wayland')
        until(lambda: 'wl_output' in sp.run(['wayland-info'], env=self.env, capture_output=True,
                                            text=True, timeout=5).stdout, parent)
        # This child has a Wayland parent; it must not acquire a physical logind seat.
        # In particular, inheriting an inactive launcher VT stops all frame scheduling.
        # Plugin load/unload triggers a native config reload. Keep the specimen geometry,
        # capture protection and compositing settings in this session's private config.
        config = self.runtime/'construction.lua'
        config.write_text((HERE/'hyprland.lua').read_text() + '''
hl.config({cursor={invisible=true},general={border_size=0},decoration={rounding=0,shadow={enabled=false},blur={enabled=false}}})
hl.window_rule({name="construction-test",match={title="^PaperEvidence-.*$"},float=true,move="96 96"})
hl.window_rule({name="construction-private",match={title="^PaperEvidence-9[0-9][0-9]$"},no_screen_share=true})
hl.animation({leaf="windowsMove",enabled=false})
-- Isolate the stage from independently timed focus/opacity fades. windowsIn is still active.
hl.animation({leaf="fade",enabled=false})
''')
        self.hypr = self.start(self.out/'hyprland.log', ['Hyprland', '-c', config],
                               extra={'LIBSEAT_BACKEND': 'seatd', 'SEATD_SOCK': str(self.runtime/'no-seat')})

        def identify():
            text = sp.run(['hyprctl', 'instances', '-j'], env=self.env, text=True,
                          capture_output=True, timeout=5).stdout.strip()
            for entry in json.loads(text) if text.startswith('[') else []:
                sig, sock = entry['instance'], entry['wl_socket']
                if (entry['pid'] == self.hypr.pid and Path(sig).name == sig and Path(sock).name == sock
                        and sock != 'construction-parent' and (self.runtime/sock).is_socket()
                        and (self.runtime/'hypr'/sig/'.socket.sock').is_socket()):
                    return entry
            return None

        instance = until(identify, self.hypr)
        self.signature = instance['instance']
        self.env.update(HYPRLAND_INSTANCE_SIGNATURE=self.signature, WAYLAND_DISPLAY=instance['wl_socket'])
        self.instance_log = self.runtime/'hypr'/self.signature/'hyprland.log'
        save(self.out/'private-instance.json', instance)
        monitor = until(lambda: next((m for m in json.loads(self.hc('-j', 'monitors'))
                        if m['name'].startswith('WAYLAND-') and m['width'] > 0), None), self.hypr)
        self.output = monitor['name']
        sp.run(['grim', '-o', self.output, self.out/'first-native-frame.png'], env=self.env, check=True, timeout=10)
        self.hc('plugin', 'load', str(self.plugin))
        self.hc('dismissnotify', '-1')
        bg = self.start(self.out/'wallpaper.log', [sys.executable, HERE/'burn_evidence.py', '--wallpaper'])
        until(lambda: 'burn-evidence' in self.hc('-j', 'layers'), bg)
        self.scene = self.start(self.out/'gtk.log', [sys.executable, HERE/'paper_scene.py'], stdin=sp.PIPE)
        until(lambda: '"ready": true' in (self.out/'gtk.log').read_text(), self.scene)
        return instance

    def send(self, op, ident, **options):
        self.scene.stdin.write((json.dumps(dict(op=op, id=ident, **options))+'\n').encode())
        self.scene.stdin.flush()

    def monitor(self):
        return next(m for m in json.loads(self.hc('-j', 'monitors')) if m['name'] == self.output)

    def scale(self, scale):
        monitor = self.monitor()
        mode = f'{monitor["width"]}x{monitor["height"]}@{monitor["refreshRate"]:.3f}'
        self.hc('eval', f'hl.monitor({{output="{self.output}",mode="{mode}",position="0x0",scale={scale}}})')
        until(lambda: abs(self.monitor()['scale'] - scale) < .001, self.hypr)
        time.sleep(.35)

    def lookup(self, ident):
        return next((c for c in json.loads(self.hc('-j', 'clients')) if c['mapped']
                     and c['title'] == f'PaperEvidence-{ident}' and c['pid'] == self.scene.pid), None)

    def options(self, enabled, duration):
        self.hc('eval', 'hl.config({plugin={hyprimate={window={enabled=true,style="construction",'
                'movement={enabled=false},close={enabled=false},open={enabled='+str(enabled).lower()+
                ',options={construction={duration='+str(duration)+'}}}}}}})')
        prefix = 'plugin:hyprimate:window:'
        for key, wanted in [('style', 'construction'), ('open:enabled', enabled), ('movement:enabled', False),
                            ('close:enabled', False), ('open:options:construction:duration', duration)]:
            raw = json.loads(self.hc('-j', 'getoption', prefix+key))
            actual = raw.get('str') if isinstance(wanted, str) else (raw.get('int', raw.get('bool'))
                     if isinstance(wanted, bool) else raw.get('float'))
            if actual != wanted and not (isinstance(wanted, float) and actual is not None and abs(actual-wanted)<1e-5):
                raise RuntimeError(('setting not applied', key, wanted, raw))

    def clip(self, name, ident, size, pattern, duration, *, enabled=True, action=None):
        folder = self.out/name
        folder.mkdir()
        self.options(enabled, duration)
        case = dict(name=name, id=ident, size=size, pattern=pattern, duration=duration, enabled=enabled,
                    action=action, monitor=self.monitor())
        self.cases.append(case)
        recording = self.start(folder/'recorder.log', [self.recorder, '-y', '-D', '-o', self.output,
                               '-c', 'libx264rgb', '-p', 'crf=0', '-p', 'preset=ultrafast', '-p', 'threads=2',
                               '-f', folder/'output.mkv'], extra={'WAYLAND_DEBUG': 'client'})
        until(lambda: READY.search((folder/'recorder.log').read_text(errors='replace')), recording)
        time.sleep(.16)
        case['request_ns'] = CLOCK()
        self.send('open', ident, width=size[0], height=size[1], construction=pattern)
        client = until(lambda: self.lookup(ident), self.scene)
        case.update(mapped_ns=CLOCK(), geometry=client['at'] + client['size'], address=client['address'])
        expected_private = 900 <= ident < 1000
        rule = self.hc('getprop', 'address:'+client['address'], 'no_screen_share')
        if rule != str(expected_private).lower():
            raise RuntimeError(('unexpected effective capture protection', name, rule))
        case['protected'] = expected_private
        if list(size) != client['size']:
            raise RuntimeError(('mapped geometry differs', size, client))
        time.sleep(max(0, (case['request_ns']-CLOCK()) * 1e-9 + duration*.43))
        case['generation_request_ns'] = CLOCK()
        self.send('generation', ident, generation=1)
        if action:
            time.sleep(.08)
            case['intervention_ns'] = CLOCK()
            if action == 'close':
                self.send('close', ident)
            elif action == 'resize':
                self.hc('eval', f'assert(hl.dispatch(hl.dsp.window.resize({{x={size[0]-96},y={size[1]-64},relative=false,window="address:{client["address"]}"}})).ok)')
            elif action == 'unload':
                self.hc('plugin', 'unload', str(self.plugin))
        time.sleep(max(.5, (case['request_ns']-CLOCK())*1e-9 + duration + .55))
        final = self.lookup(ident)
        case['final_geometry'] = final['at'] + final['size'] if final else None
        if final and (final['address'] != client['address'] or
                      self.hc('getprop', 'address:'+final['address'], 'no_screen_share') != rule):
            raise RuntimeError(('window identity or protection changed during take', name))
        if (action == 'close') != (final is None):
            raise RuntimeError(('unexpected map lifetime', name, final))
        if action == 'resize' and final['size'] != [size[0]-96, size[1]-64]:
            raise RuntimeError(('addressed resize did not apply', name, final))
        stop(recording, signal.SIGINT)
        if recording.returncode != 0:
            raise RuntimeError(f'recorder failed: {folder}/recorder.log')
        if final:
            self.send('close', ident)
            until(lambda: not self.lookup(ident), self.scene)
        time.sleep(.42)
        case['closed_ns'] = CLOCK()
        save(folder/'case.json', case)
        return case

    def cleanup(self):
        if hasattr(self, 'instance_log') and self.instance_log.exists():
            shutil.copyfile(self.instance_log, self.out/'private-hyprland.log')
        for process in reversed(self.processes):
            if process.poll() is None or process is getattr(self, 'hypr', None):
                stop(process)
        for path in self.runtime.glob('home/cache/hyprland/*CrashReport*'):
            shutil.copyfile(path, self.out/path.name)


def timing(folder):
    """Anchor container PTS to real private screencopy ready timestamps, not recorder spawn."""
    probe = json.loads(sp.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                       '-show_frames', '-show_streams', '-show_entries',
                       'frame=best_effort_timestamp_time:stream=width,height,codec_name,pix_fmt',
                       '-of', 'json', folder/'output.mkv']))
    pts = [float(f['best_effort_timestamp_time']) for f in probe['frames']]
    if len(pts) < 3 or any(b <= a for a, b in zip(pts, pts[1:])):
        raise RuntimeError(f'Invalid frame PTS: {folder}')
    ready = [((int(hi) << 32) + int(lo))*10**9 + int(ns) for hi, lo, ns in
             READY.findall((folder/'recorder.log').read_text(errors='replace'))]
    # The encoder can drop frames; accept an alignment only when it maps every PTS
    # to a distinct protocol event within container precision. Never guess an offset.
    matches = []
    for origin in ready[:max(0, len(ready)-len(pts)+1)]:
        indices, errors = [], []
        for t in pts:
            target = origin + round((t-pts[0])*1e9)
            i = bisect.bisect_left(ready, target)
            j = min((n for n in (i-1, i) if 0 <= n < len(ready)), key=lambda n: abs(ready[n]-target))
            # Matroska's millisecond ticks round to nearest; the encoder's intermediate
            # microsecond rounding can contribute one more microsecond.
            if abs(ready[j]-target) > 501_000 or (indices and j <= indices[-1]):
                break
            indices.append(j)
            errors.append(abs(ready[j]-target))
        if len(indices) == len(pts):
            matches.append((indices, max(errors)))
    times = [ready[i] for i in matches[0][0]] if len(matches) == 1 else []
    save(folder/'timestamps.json', dict(stream=probe['streams'][0], pts_s=pts, protocol_ready_ns=ready,
         matched_capture_ns=times, alignment_candidates=len(matches),
         max_alignment_residual_ns=matches[0][1] if times else None,
         capture_gap_ms=np.percentile(np.diff(pts)*1000, [50, 95, 99, 100]).tolist(),
         gap_percentiles=[50, 95, 99, 100], clock='screencopy ready CLOCK_MONOTONIC; not GPU/physical presentation'))
    return pts, times, probe['streams'][0]


def delta(current, control, geometry, monitor):
    if current.shape != control.shape:
        raise ValueError('control has a different output extent')
    scale = monitor['scale']
    x, y, w, h = geometry
    x, y = x-monitor['x'], y-monitor['y']
    x0, y0, x1, y1 = (round(t*scale) for t in (x, y, x+w, y+h))
    diff = np.abs(current.astype(np.int16)-control).max(axis=2)
    inner = diff[y0+2:y1-2, x0+2:x1-2]
    exact = dict(interior_max=int(inner.max()), interior_mismatched=int(np.count_nonzero(inner)),
                 compared_interior_pixels=inner.size)
    diff[y0+2:y1-2, x0+2:x1-2] = 0
    exact['edge_max'] = int(diff[max(0,y0-1):y1+1, max(0,x0-1):x1+1].max())
    diff[max(0,y0-1):y1+1, max(0,x0-1):x1+1] = 0
    exact['outside_max'] = int(diff.max())
    return exact


def sentinel(frame, generation):
    # Saturated colors deliberately absent from the map and either specimen palette.
    r, g, b = np.moveaxis(frame, 2, 0)
    if generation == 'edge':
        mask = (r > 48) & (r < 96) & (g < 44) & (b > 218)
    elif generation:
        mask = (r > 210) & (g < 60) & (b > 150) & (b < 215)
    else:
        mask = (r < 52) & (g > 210) & (b > 140) & (b < 201)
    yy, xx = np.nonzero(mask)
    return [int(xx.min()), int(yy.min()), int(xx.max()), int(yy.max()), int(xx.size)] if xx.size else None


def analyze(out, case, trace, controls):
    folder = out/case['name']
    pts, times, stream = timing(folder)
    width, height = stream['width'], stream['height']
    monitor = case['monitor']
    if (width, height) != (monitor['width'], monitor['height']):
        raise RuntimeError(('not a full physical output', width, height, monitor))
    cut = {}
    events = {key: (seed, ns) for key, seed, ns in trace if case['request_ns'] <= ns <= case['closed_ns']}
    painted = [j for line in (out/'gtk.log').read_text().splitlines() if line.startswith('{')
               and (j := json.loads(line)).get('id') == case['id']]
    unavailable, failed = [], []
    begin = events.get('open')
    # The specimen's L is in the bottom-right bay; timings are relative to that
    # crew's start, not to the team that seats the first panel.
    bay_start = .15 + (.07 if begin and begin[0] & 1 else 0)
    bay_span = .64
    seat_fraction = bay_start + bay_span * .66
    if times:
        def at(ns):
            return min(len(times)-1, bisect.bisect_left(times, ns))
        cut['pre'] = max(0, bisect.bisect_left(times, case['request_ns'])-1)
        epoch = begin[1] if begin else case['request_ns']
        for label, frac in [('brace', bay_start), ('raise', bay_start + bay_span*.32),
                            ('seat', seat_fraction), ('strike', bay_start + bay_span*.75),
                            ('withdraw', bay_start + bay_span*.86)]:
            cut[label] = at(epoch + round(frac*case['duration']*1e9))
        if 'native-seated' in events and 'end' in events:
            # The slot is still running, but the compositor presents its own surfaces.
            # Check both captured frames before slot end, not two convenient idle frames.
            first = bisect.bisect_left(times, events['native-seated'][1])
            period = 1e9 / max(1.0, monitor['refreshRate'])
            post = bisect.bisect_left(times, events['end'][1])
            if first+1 < post and times[first]-events['native-seated'][1] < period:
                cut['active-seated'] = first
                cut['following-frame'] = first+1
            if post < len(times):
                cut['first-post-end'] = post
        if 'intervention_ns' in case:
            cut['interrupted'] = at(case['intervention_ns']+120_000_000)
        if case['name'] == 'allocation-denied':
            cut['fault-active'] = at(epoch + round(.59*case['duration']*1e9))
    else:
        unavailable.append('No unique mapping from MKV PTS to screencopy ready timestamps.')
        cut.update({f'sample-{i}':round((len(pts)-1)*i/8) for i in range(9)})
    cut['native-calibration'] = max(0, len(pts)-5)
    cut['settled'] = len(pts)-1
    refname = case.get('control')
    native = controls.get(refname)
    checks, thumbs, markers, changed_pts = {}, {}, [], []
    keyframes = {i: [s for s in cut if cut[s] == i] for i in cut.values()}
    previous = None
    count_bytes = width*height*3
    control_case = not case['enabled']
    protected = case['protected']
    private_fault = case['name'] == 'allocation-denied'
    labels = ('brace','raise','seat','strike','withdraw','active-seated','following-frame','first-post-end')
    with sp.Popen(['ffmpeg', '-v', 'error', '-threads', '2', '-i', folder/'output.mkv',
                   '-map', '0:v:0', '-fps_mode', 'passthrough', '-enc_time_base', '1:1000', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                   '-threads', '1', '-'], stdout=sp.PIPE) as decoder:
        for i, t in enumerate(pts):
            raw = decoder.stdout.read(count_bytes)
            if len(raw) != count_bytes:
                raise RuntimeError(('short decoded frame', folder, i))
            if raw != previous:
                changed_pts.append(t)
            previous = raw
            img = np.frombuffer(raw, np.uint8).reshape(height, width, 3)
            markers.append([sentinel(img, 0), sentinel(img, 1), sentinel(img, 'edge')])
            for label in keyframes.get(i, []):
                still = Image.fromarray(img)
                if label in ('brace','raise','seat','strike','active-seated','following-frame','first-post-end','pre','interrupted','settled','fault-active'):
                    still.save(folder/(label+'.png'))
                if label in labels or label.startswith('sample-'):
                    still.thumbnail((320, 200))
                    thumbs[label] = still
                if label == 'native-calibration' and control_case:
                    calibration = img.copy()
                if label == 'settled' and control_case:
                    controls[case['name']] = img.copy()
                    checks['native-repeat'] = delta(img, calibration, case['final_geometry'], monitor)
                    case['native_calibration'] = checks['native-repeat']
                if native is not None and label in ('active-seated','following-frame','first-post-end',
                                                    'settled','fault-active','interrupted'):
                    geometry = case['final_geometry'] or case['geometry']
                    checks[label] = delta(img, native, geometry, monitor)
            if case['action'] == 'close' and i == cut.get('pre', 0):
                blank = img.copy()
            if case['action'] == 'close' and i == len(pts)-1:
                checks['healed'] = delta(img, blank, case['geometry'], monitor)
        if decoder.stdout.read(1) or decoder.wait() != 0:
            raise RuntimeError(('unexpected decoded frame count', folder))
    if control_case and not protected and (not any(m[0] for m in markers) or not all(markers[-1][1:])):
        failed.append('Native positive control lacks a moving or installed specimen sentinel.')
    if {p.get('generation') for p in painted} != {0, 1}:
        failed.append('GTK did not paint both requested generations.')
    if case['enabled'] and not protected and not begin:
        unavailable.append('No observed Construction open event; effect ownership unproven.')
    if protected:
        if begin or 'native-seated' in events:
            failed.append('Construction acquired a window that must use protected native presentation.')
        if any(any(m) for m in markers):
            failed.append('Protected capture contains specimen sentinel.')
    elif case['enabled'] and not case['action'] and not private_fault and times and begin:
        seat = begin[1] + round(seat_fraction*case['duration']*1e9)
        target = markers[-1][1]
        def displaced(m):
            return target and m and m[4] >= 24 and max(abs(c-d) for c,d in zip(m[:4], target[:4])) >= 3
        moving = [i for i, m in enumerate(markers) if begin[1] < times[i] < seat and displaced(m[1])]
        last_displaced = max((i for i,m in enumerate(markers) if displaced(m[1])), default=None)
        if last_displaced is not None and last_displaced+1 < len(times):
            checks['last_displaced_marker'] = dict(frame=last_displaced, mono_ns=times[last_displaced],
                seconds_since_open=(times[last_displaced]-begin[1])*1e-9,
                next_capture_ns=times[last_displaced+1], note='3-pixel extent threshold; not a support/contact measurement')
        if not moving or not any(times[i] > begin[1] and displaced(markers[i][0]) for i in range(moving[0])):
            failed.append('Two generations not observed on displaced panel before seating.')
        if 'active-seated' not in cut:
            unavailable.append('No pair of timely captured frames during the live native-seated interval.')
    if native is not None and not protected:
        compare = ['settled']
        if private_fault:
            compare += ['fault-active']
        elif case['action'] in ('resize','unload'):
            compare += ['interrupted']
        elif not control_case:
            compare += ['active-seated','following-frame','first-post-end']
        for label in compare:
            if label not in checks:
                unavailable.append(f'No {label} comparison.')
                continue
            result = checks[label]
            calibration = controls[refname+'-tolerance']
            if (result['interior_max'] != 0 or result['edge_max'] > calibration['edge_max'] or
                    result['outside_max'] > calibration['outside_max']):
                failed.append(f'{label}: pixel mismatch against {refname}.')
    if control_case:
        controls[case['name']+'-tolerance'] = checks['native-repeat']
    if 'healed' in checks and any(checks['healed'][k] for k in ('interior_max','edge_max','outside_max')):
        failed.append('Close interruption left differences in the original output.')
    if thumbs:
        sheet = Image.new('RGB', (960, math.ceil(len(thumbs)/3)*226), '#1d2228')
        draw = ImageDraw.Draw(sheet)
        for n, (label, im) in enumerate(thumbs.items()):
            x, y = (n%3)*320, (n//3)*226
            sheet.paste(im, (x,y))
            idx = cut[label]
            draw.text((x+4,y+201), f'{label}  #{idx}  PTS {pts[idx]:.3f}s', fill='white')
        sheet.save(folder/'contact-sheet.jpg', quality=92)
    save(folder/'observations.json', dict(events=events, gtk_paints=painted, frames=cut, samples=checks,
         changed_frame_pts_s=changed_pts, sentinel_boxes_xyxy_count=markers, failures=failed, unavailable=unavailable))
    return dict(name=case['name'], events=events, compared=checks, failed=failed, unavailable=unavailable)


def film(session, fault, duration, visual_only):
    normal, small = (800,550), (480,320)
    def clip(name, ident, *, size=normal, style='light', seconds=duration, control=None, **kwargs):
        c = session.clip(name, ident, size, style, seconds, **kwargs)
        c['control'] = control
        save(session.out/name/'case.json', c)
    session.scale(1)
    # Consecutive fresh opens have opposite observed seed parity. Do not assume which
    # one is delayed, nor substitute the slower-duration cut for that causal repeat.
    clip('installation-a', 10, control='native-normal')
    clip('installation-b', 11, control='native-normal')
    clip('installation-spacious', 12, seconds=.85, control='native-normal')
    clip('native-normal', 13, enabled=False)
    session.scale(1.25)
    clip('installation-small', 20, size=small, style='dark', control='native-small')
    clip('native-small', 21, size=small, style='dark', enabled=False)
    if visual_only:
        # A wider load has room for a third carrier. Keep its own same-size
        # native control; don't substitute a scaled copy of the standard take.
        session.scale(1)
        clip('installation-wide', 14, size=(1100, 560), control='native-wide')
        clip('native-wide', 15, size=(1100, 560), enabled=False)
        return
    session.scale(1)
    clip('native-resized', 31, enabled=False, action='resize')
    clip('interrupt-resize', 32, action='resize', control='native-resized')
    clip('interrupt-close', 33, action='close')
    clip('protected-native', 900, enabled=False)
    clip('protected-opening', 901, control='protected-native')
    if fault:
        session.hc('plugin', 'unload', str(session.plugin))
        session.hc('plugin', 'load', str(fault))
        clip('allocation-denied', 41, control='native-normal')
        session.hc('plugin', 'unload', str(fault))
        session.hc('plugin', 'load', str(session.plugin))
        clip('allocation-retry', 42, control='native-normal')
    clip('interrupt-unload', 50, action='unload', control='native-normal')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('output', type=Path)
    ap.add_argument('--plugin', type=Path, required=True)
    ap.add_argument('--fault-plugin', type=Path,
                    help='Test relink: deny only Construction work-buffer acquisition')
    ap.add_argument('--recorder', help='Path to an existing wf-recorder')
    ap.add_argument('--slice', action='store_true', required=True, help='Only the installation workload exists')
    ap.add_argument('--record', action='store_true', required=True, help='Capture lossless full-output VFR clips')
    ap.add_argument('--duration', type=float, default=.62, help='Primary open duration, in seconds')
    ap.add_argument('--visual-only', action='store_true',
                    help='Film the two crew tempos, delayed arrival, and size-matched controls; not a safety run')
    args = ap.parse_args()
    if not .25 <= args.duration <= 3:
        ap.error('--duration must be in [0.25,3]')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    plugin = args.plugin.resolve(strict=True)
    recorder = recorder_path(args.recorder or os.environ.get('WF_RECORDER'))
    fault = args.fault_plugin.resolve(strict=True) if args.fault_plugin else None
    if fault and digest(plugin) == digest(fault):
        raise ValueError('The allocation-denied fixture must be distinct from the clean build.')
    manifest = dict(plugin_path=str(plugin), plugin_sha256=digest(plugin), recorder=str(recorder),
        fault_path=str(fault) if fault else None, fault_sha256=digest(fault) if fault else None,
        git_head=sp.check_output(['git','-C',ROOT,'rev-parse','HEAD'], text=True).strip(),
        sources={str(p.relative_to(ROOT)):digest(p) for p in (Path(__file__).resolve(), HERE/'burn_evidence.py',
                 HERE/'paper_scene.py', HERE/'hyprland.lua')},
        capture='lossless libx264rgb, full physical nested output, unchanged recorder PTS',
        source_paths={'choreography':'src/animations/construction/ConstructionSite.cpp',
                      'live_adapter':'src/animations/construction/ConstructionLiveStyle.cpp',
                      'renderer':'src/animations/construction/ConstructionRenderer.cpp'},
        requested_duration=args.duration, visual_only=args.visual_only)
    save(out/'manifest.json', manifest)
    (out/'verdict.md').write_text('# Not validated\n\nCapture or analysis has not completed. Inspect logs.\n')
    def terminate(signum, _frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    with tempfile.TemporaryDirectory(prefix='cs.', dir='/tmp') as tmp:
        session = Session(Path(tmp), out, plugin, recorder)
        try:
            manifest['instance'] = session.boot()
            manifest['system_info'] = session.hc('systeminfo')
            film(session, fault, args.duration, args.visual_only)
        finally:
            manifest['cases'] = session.cases
            session.cleanup()
            save(out/'manifest.json', manifest)

    log = '\n'.join(p.read_text(errors='replace') for p in
                    [out/'hyprland.log', out/'private-hyprland.log'] if p.is_file())
    trace = sorted({(kind, int(seed), int(ns)) for kind, seed, ns in EVENT.findall(log)}, key=lambda e: e[2])
    manifest['driver_log'] = [s for s in log.splitlines() if re.search(r'\b(Vendor|Renderer|GL version):', s)]
    manifest['trace'] = trace
    resources = []
    for record in sorted(set(RESOURCE.findall(log))):
        values = {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', record)}
        fmt = values['work_fourcc'].to_bytes(4, 'little').decode('ascii', errors='replace')
        bpp = (4 if fmt in ('XR24','XB24','AR24','AB24','XR30','XB30','AR30','AB30') else
               8 if fmt in ('XR4H','XB4H','AR4H','AB4H','XR48','XB48','AR48','AB48') else
               2 if fmt in ('RG16','BG16') else None)
        values.update(fourcc_name=fmt, peak_instance_upload_bytes=values['peak_quads'] * values['stride_bytes'],
                      work_color_bytes=(values['work_width'] * values['work_height'] * bpp if bpp else None))
        resources.append(values)
    manifest['resource_log'] = resources
    manifest['memory_measurement'] = 'Largest successful color attachment and peak instance payload; excludes driver padding, depth/stencil and pooling.'
    save(out/'manifest.json', manifest)
    controls, reports = {}, []
    for case in sorted(session.cases, key=lambda c: c['enabled']):
        if case.get('control'):
            reference = next(c for c in session.cases if c['name'] == case['control'])
            if reference['final_geometry'] != case['final_geometry'] or reference['monitor']['scale'] != case['monitor']['scale']:
                raise RuntimeError(('native control geometry mismatch', case, reference))
        report = analyze(out, case, trace, controls)
        reports.append(report)
        save(out/'measurements.json', reports)
    failures = [f'{r["name"]}: {msg}' for r in reports for msg in r['failed']]
    gaps = [f'{r["name"]}: {msg}' for r in reports for msg in r['unavailable']]
    seeds = [r['events']['open'][0] for r in reports if r['name'] in ('installation-a','installation-b')
             and 'open' in r['events']]
    if {s & 1 for s in seeds} != {0,1}:
        gaps.append('Matched normal/delayed seed pair not observed.')
    arrivals = {r['events']['open'][0] & 1:r['compared']['last_displaced_marker']['seconds_since_open']
                for r in reports if r['name'] in ('installation-a','installation-b')
                and 'open' in r['events'] and 'last_displaced_marker' in r['compared']}
    if len(arrivals) == 2:
        manifest['delayed_minus_normal_last_displacement_seconds'] = arrivals[1]-arrivals[0]
        if arrivals[1] <= arrivals[0]:
            failures.append('Odd seed did not show a later return of the displaced marker than even seed.')
    if not resources:
        gaps.append('No measured Construction resource records.')
    fault_count = log.count('[construction-fixture] work buffer denied')
    if not args.visual_only and (not fault or not fault_count):
        gaps.append('No confirmed mapped work-buffer denial from separate test relink.')
    manifest.update(observed_pair_seeds=seeds, fault_log_count=fault_count)
    save(out/'manifest.json', manifest)
    report = ['# Construction installation — not promoted', '',
              'Footage is at **1×**, without annotations, in each `output.mkv`. '
              'Compare `installation-a/output.mkv` with `installation-spacious/output.mkv` at original size. '
              'Contact sheets are diagnostic only.', '',
              f'Artifact: `{plugin}` (SHA-256 `{manifest["plugin_sha256"]}`).',
              f'Observed matched seeds: `{seeds}`. Raw frame indices, clock correlation, marker extent, '
              'changed-frame times and native pixel comparisons are in the per-case JSON files.', '',
              '## Automatic discrepancies', *(f'- {v}' for v in failures or ['None recorded.']), '',
              '## Unavailable measurements', *(f'- {v}' for v in gaps),
              '- Recorder PTS measure nested capture cadence, not CPU submission, GPU or physical display timing.',
              '- Inspect visible helmet-to-sole height against output scale, not the authored value.',
              '', '## Review still required',
              'Compare the short cut with the .85 s cut at 1×. Look for parallel hand placement, '
              'panels standing upright, the tool strike, and a clean handoff. Contact sheets do not establish legibility.',
              'Visual-only takes do not exercise or clear the outstanding safety gates.' if args.visual_only else
              'The allocation-denied case exercises a null lease, not a host driver allocation failure.']
    (out/'verdict.md').write_text('\n'.join(report)+'\n')
    print(out/'verdict.md')
    if failures:
        raise SystemExit(1)
    if gaps:
        raise SystemExit(2)


if __name__ == '__main__':
    main()

