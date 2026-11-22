#!/usr/bin/env python3
"""Paired held mouse paths on public GTK specimens in a sealed stock nested session.

Invoke with --sealed-sha256 and --take to re-exec inside bwrap. Raw capture
and lossless PNGs are the comparison sources; review.mp4 is for viewing.
"""

import argparse
import bisect
import json
import math
import os
from pathlib import Path
import re
import signal
import socket
import struct
import subprocess as sp
import sys
import tempfile
import threading
import time

import numpy as np
from PIL import Image

import construction_evidence as evidence

CLOCK = time.monotonic_ns
HOST = '948ac1abcd398f30460a5fb6951c7d46ba16bac7'
PLUGIN = Path('/opt/bin/hyprimate.so')
OUTPUT = Path('/evidence')
HERE = Path(__file__).resolve().parent
SIZE = (480, 320)
ORIGIN = (170, 112)
# One button-down per take. The two stationary intervals are not separate grabs.
PATHS = {
    'carry': ((184, 74), (66, 28), (234, 62)),
    'resize': ((112, 64), (-62, -32), (64, 48)),
}
EVENT = re.compile(r'\[hyprimate\] construction event=(carry-start|carry-end|resize-start|resize-end) seed=(\d+) mono_ns=(\d+)')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def peer(sock, path, pid):
    sock.settimeout(4)
    sock.connect(str(path))
    actual = struct.unpack('=3i', sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
    require(actual[:2] == (pid, os.getuid()), f'Not the private child: {path}: {actual}')
    return dict(path=str(path), pid=actual[0], uid=actual[1], gid=actual[2])


class HeldPointer:
    """The small v1 request subset needed for continuous paths across pauses/reversals.

    hover-vpointer's `hold` is a straight segment that releases at its end, so
    it cannot concatenate these segments into one held drag. Use the same
    verified Wayland protocol and keep one virtual device and button down.
    """

    def __init__(self, sock, extent, trace):
        self.sock, self.extent, self.trace = sock, extent, trace
        self.stream = sock.makefile('rb')
        self.position = None
        self.pressed = False

    def request(self, obj, opcode, payload=b''):
        return struct.pack('=II', obj, ((8 + len(payload)) << 16) | opcode) + payload

    def connect(self):
        # wl_display.get_registry, then sync; fixed IDs are private to this connection.
        self.sock.sendall(self.request(1, 1, struct.pack('=I', 2)) + self.request(1, 0, struct.pack('=I', 3)))
        iface = b'zwlr_virtual_pointer_manager_v1\0'
        manager = None
        while True:
            header = self.stream.read(8)
            require(len(header) == 8, 'Private virtual-pointer registry disconnected')
            obj, tag = struct.unpack('=II', header)
            opcode, size = tag & 0xffff, tag >> 16
            require(size >= 8, 'Invalid Wayland header')
            body = self.stream.read(size - 8)
            require(len(body) == size - 8 and (obj, opcode) != (1, 0), f'Wayland registry error: {body!r}')
            if (obj, opcode) == (2, 0):  # wl_registry.global
                name, length = struct.unpack_from('=II', body)
                version, = struct.unpack_from('=I', body, 8 + ((length + 3) & ~3))
                if body[8:8 + length] == iface and version >= 1:
                    manager = name
            if (obj, opcode) == (3, 0):  # wl_callback.done
                break
        require(manager is not None, 'Private child does not expose zwlr_virtual_pointer_manager_v1')
        bind = (struct.pack('=II', manager, len(iface)) + iface.ljust((len(iface) + 3) & ~3, b'\0')
                + struct.pack('=II', 1, 4))
        self.sock.sendall(self.request(2, 0, bind) + self.request(4, 0, struct.pack('=II', 0, 5)))

    def event(self, opcode, arguments, kind, **data):
        stamp = CLOCK()
        payload = struct.pack(f'={len(arguments) + 1}I', stamp // 1_000_000 & 0xffffffff, *arguments)
        self.sock.sendall(self.request(5, opcode, payload) + self.request(5, 4))
        self.trace.append(dict(kind=kind, mono_ns=stamp, **data))

    def move(self, position):
        self.position = position
        self.event(1, (*position, *self.extent), 'motion_absolute', xy=position)

    def button(self, down):
        self.event(2, (0x110, int(down)), 'button', pressed=down, xy=self.position)
        self.pressed = down

    def segment(self, target, seconds):
        start, started = self.position, time.monotonic()
        steps = math.ceil(seconds * 60)
        for index in range(1, steps + 1):
            fraction = index / steps
            time.sleep(max(0, started + fraction * seconds - time.monotonic()))
            self.move(tuple(round(a + fraction * (b - a)) for a, b in zip(start, target)))
        # Stock throttles 60 Hz resize updates. Redeliver the endpoint if the
        # last motion arrived in its throttle window; keep the same button down.
        time.sleep(.030)
        self.move(target)

    def close(self):
        if self.pressed:
            self.button(False)
        self.stream.close()


class MotionSession(evidence.Session):
    def boot(self, scale, kind, sealed_sha):
        parent = self.start(self.out/'kwin.log', ['dbus-run-session', '--', 'kwin_wayland', '--virtual',
                            '--width', '1440', '--height', '900', '--no-lockscreen',
                            '--no-global-shortcuts', '--socket', 'construction-parent'])
        evidence.until(lambda: (self.runtime/'construction-parent').is_socket(), parent)
        self.env.update(WAYLAND_DISPLAY='construction-parent', AQ_BACKENDS='wayland')
        evidence.until(lambda: 'wl_output' in sp.run(['wayland-info'], env=self.env, capture_output=True,
                                            text=True, timeout=5).stdout, parent)
        config = self.runtime/'motion.lua'
        dispatch = 'drag' if kind == 'carry' else 'resize'
        config.write_text(f'''
            hl.monitor({{output='',mode='1440x900@60',position='0x0',scale={scale}}})
            hl.config({{debug={{disable_logs=false,enable_stdout_logs=true}}, cursor={{invisible=true}},
                general={{border_size=0}}, misc={{disable_hyprland_logo=true,disable_splash_rendering=true}},
                decoration={{rounding=0,shadow={{enabled=false}},blur={{enabled=false}}}},
                animations={{enabled=true}}}})
            hl.window_rule({{name='construction-public',match={{title='^PaperEvidence-2[01]$'}},
                float=true,move='{ORIGIN[0]} {ORIGIN[1]}',no_screen_share=false}})
            hl.animation({{leaf='windowsIn',enabled=false}})
            hl.animation({{leaf='windowsOut',enabled=false}})
            hl.animation({{leaf='windowsMove',enabled=false}})
            hl.animation({{leaf='fade',enabled=false}})
            hl.bind('mouse:272', hl.dsp.window.{dispatch}(), {{mouse=true}})
        ''')
        self.out.joinpath('motion.lua').write_text(config.read_text())
        self.hypr = self.start(self.out/'hyprland.log', ['Hyprland', '-c', config],
                               extra={'LIBSEAT_BACKEND': 'seatd', 'SEATD_SOCK': str(self.runtime/'no-seat')})

        def instance():
            reply = sp.run(['hyprctl', 'instances', '-j'], env=self.env, text=True,
                           capture_output=True, timeout=5).stdout.strip()
            for entry in json.loads(reply) if reply.startswith('[') else []:
                sig, name = entry['instance'], entry['wl_socket']
                if (entry['pid'] == self.hypr.pid and Path(sig).name == sig and Path(name).name == name
                        and name != 'construction-parent' and (self.runtime/name).is_socket()
                        and (self.runtime/'hypr'/sig/'.socket.sock').is_socket()):
                    return entry
            return None

        child = evidence.until(instance, self.hypr)
        self.signature = child['instance']
        self.wayland_socket = self.runtime/child['wl_socket']
        self.instance_log = self.runtime/'hypr'/self.signature/'hyprland.log'
        self.env.update(HYPRLAND_INSTANCE_SIGNATURE=self.signature, WAYLAND_DISPLAY=child['wl_socket'])
        require(Path(f'/proc/{self.hypr.pid}/exe').samefile('/opt/bin/Hyprland'), 'Unexpected private child executable')
        peers = []
        for path in [self.runtime/'hypr'/self.signature/'.socket.sock', self.wayland_socket]:
            with socket.socket(socket.AF_UNIX) as sock:
                peers.append(peer(sock, path, self.hypr.pid))
        version = self.hc('version')
        require(HOST in version, f'Not the stock host: {version}')
        ready = evidence.until(lambda: json.loads(self.hc('-j', 'monitors')), self.hypr)
        require(len(ready) == 1 and ready[0]['name'].startswith('WAYLAND-'), f'Unexpected private output: {ready}')
        self.output = ready[0]['name']
        monitor = self.monitor()
        require((monitor['width'], monitor['height'], monitor['x'], monitor['y'], monitor['transform'])
                == (1440, 900, 0, 0, 0) and abs(monitor['scale'] - scale) < .001, f'Unexpected mode: {monitor}')
        self.extent = (round(monitor['width']/scale), round(monitor['height']/scale))
        with socket.socket(socket.AF_UNIX) as sock:
            peer(sock, self.wayland_socket, self.hypr.pid)
            pointer = HeldPointer(sock, self.extent, [])
            pointer.connect()
            pointer.close()
        require(evidence.digest(self.plugin) == sealed_sha, 'Artifact differs from the sealed SHA-256')
        require(not json.loads(self.hc('-j', 'clients')), 'Nonempty child before the first capture')
        self.capture(self.out/'first-native-frame.png')
        self.hc('plugin', 'load', str(self.plugin))
        mappings = Path(f'/proc/{self.hypr.pid}/maps').read_text().splitlines()
        mapped = [line for line in mappings if line.split(maxsplit=5)[-1] == str(self.plugin)]
        require(mapped and all(int(line.split()[4]) == self.plugin.stat().st_ino for line in mapped),
                f'Loaded process does not map the sealed plugin: {mapped}')
        plugins = json.loads(self.hc('-j', 'plugin', 'list'))
        require([entry['name'] for entry in plugins] == ['hyprimate'], f'Unexpected plugin inventory: {plugins}')
        errors = json.loads(self.hc('-j', 'configerrors'))
        require(not any(errors), f'Private configuration errors: {errors}')
        binds = json.loads(self.hc('-j', 'binds'))
        require(len(binds) == 1 and binds[0]['modmask'] == 0 and binds[0]['mouse']
                and binds[0]['key'] == 'mouse:272', f'Unexpected mouse bind: {binds}')
        self.hc('dismissnotify', '-1')
        bg = self.start(self.out/'wallpaper.log', [sys.executable, HERE/'burn_evidence.py', '--wallpaper'])
        evidence.until(lambda: 'burn-evidence' in self.hc('-j', 'layers'), bg)
        self.scene = self.start(self.out/'gtk.log', [sys.executable, HERE/'paper_scene.py'], stdin=sp.PIPE)
        evidence.until(lambda: '"ready": true' in (self.out/'gtk.log').read_text(), self.scene)
        time.sleep(.25)
        self.capture(self.out/'background.png')
        renderer = [line.rsplit('] ', 1)[-1] for line in self.instance_log.read_text(errors='replace').splitlines()
                    if re.search(r'CHyprOpenGLImpl::CHyprOpenGLImpl\] (Using:|Vendor:|Renderer:)', line)]
        evidence.save(self.out/'host.json', dict(instance=child, peers=peers, version=version, monitor=monitor,
                      grab=dispatch, binding=binds[0], pointer_global='zwlr_virtual_pointer_manager_v1', extent=self.extent,
                      renderer=renderer, plugin_sha256=sealed_sha, plugin_inventory=plugins,
                      mapped_plugin=mapped, capture='synthetic public clients only'))

    def public(self, ident, address):
        clients = [client for client in json.loads(self.hc('-j', 'clients')) if client['mapped']]
        require(len(clients) == 1 and clients[0]['pid'] == self.scene.pid
                and clients[0]['title'] == f'PaperEvidence-{ident}' and clients[0]['address'] == address,
                f'Unexpected mapped client set: {clients}')
        rule = self.hc('getprop', 'address:'+address, 'no_screen_share')
        require(rule == 'false', f'Addressed no_screen_share is not false: {address}: {rule}')
        return rule

    def options(self, variant):
        self.hc('eval', 'hl.config({plugin={hyprimate={window={enabled=true,style="construction",'
                'movement={enabled=true,options={construction={variant="'+variant+'"}}},'
                'open={enabled=false},close={enabled=false}}}}})')
        wanted = {'enabled': 1, 'style': 'construction', 'open:enabled': 0, 'close:enabled': 0,
                  'movement:enabled': 1, 'movement:options:construction:variant': variant}
        options = {key: json.loads(self.hc('-j', 'getoption', 'plugin:hyprimate:window:'+key)) for key in wanted}
        for key, expected in wanted.items():
            raw = options[key]
            actual = raw.get('str') if isinstance(expected, str) else raw.get('int', raw.get('bool'))
            require(actual == expected, f'Unapplied option {key}: wanted {expected}, got {raw}')
        return options

    def capture(self, path):
        sp.run(['grim', '-o', self.output, path], env=self.env, check=True, timeout=10)

    def take(self, kind, label, ident):
        folder = self.out/label
        folder.mkdir()
        variant = 'crew_carry' if label == 'animated' else 'native'
        data = dict(kind=kind, variant=variant, monitor=self.monitor(), options=self.options(variant),
                    began_ns=CLOCK(), specimen=dict(id=ident, pattern='dark', start_size=SIZE), phases=[], pointer=[], geometry=[])
        self.send('open', ident, width=SIZE[0], height=SIZE[1], construction='dark')
        client = evidence.until(lambda: self.lookup(ident), self.scene)
        address = client['address']
        data.update(address=address, no_screen_share=self.public(ident, address), initial=client['at'] + client['size'])
        require(tuple(data['initial']) == ORIGIN + SIZE, f'Unexpected starting geometry: {data["initial"]}')
        time.sleep(.30)
        self.capture(folder/'initial.png')
        recording = self.start(folder/'recorder.log', [self.recorder, '--no-dmabuf', '-y', '-D', '-o', self.output,
                               '-c', 'libx264rgb', '-p', 'crf=0', '-p', 'preset=ultrafast', '-p', 'threads=2',
                               '-f', folder/'output.mkv'], extra={'WAYLAND_DEBUG': 'client'})
        stop_sampling = threading.Event()
        errors = []

        def sample():
            try:
                while not stop_sampling.is_set():
                    started = CLOCK()
                    window = self.lookup(ident)
                    require(window is not None and window['address'] == address, 'Mapped specimen changed during grab')
                    data['geometry'].append(dict(query_ns=started, mono_ns=CLOCK(), box=window['at'] + window['size']))
                    stop_sampling.wait(.024)
            except Exception as exc:
                errors.append(str(exc))

        sampler = threading.Thread(target=sample)
        log = folder/'recorder.log'
        try:
            evidence.until(lambda: evidence.READY.search(log.read_text(errors='replace')), recording)
            sampler.start()
            time.sleep(.28)
            with socket.socket(socket.AF_UNIX) as sock:
                data['pointer_peer'] = peer(sock, self.wayland_socket, self.hypr.pid)
                pointer = HeldPointer(sock, self.extent, data['pointer'])
                try:
                    pointer.connect()
                    x, y = ORIGIN
                    start = (x + SIZE[0]//2, y + SIZE[1]//2) if kind == 'carry' else (x+18, y+18)
                    targets = [(start[0]+dx, start[1]+dy) for dx, dy in PATHS[kind]]
                    pointer.move(start)
                    time.sleep(.09)
                    pointer.button(True)
                    data['press_ns'] = data['pointer'][-1]['mono_ns']
                    schedule = [('pickup', None, .15), ('outward', targets[0], .67),
                                ('reverse', targets[1], .65), ('held-pause-before-marker', None, .15),
                                ('held-pause-after-marker', None, .23), ('redirect', targets[2], .72),
                                ('held-at-destination', None, .28)]
                    for phase, point, seconds in schedule:
                        start_ns = CLOCK()
                        if phase == 'held-pause-after-marker':
                            data['generation_request_ns'] = start_ns
                            self.send('generation', ident, generation=1)
                        if point:
                            pointer.segment(point, seconds)
                        else:
                            time.sleep(seconds)
                        data['phases'].append(dict(name=phase, start_ns=start_ns, end_ns=CLOCK(),
                                                   target=point, requested_s=seconds))
                    pointer.button(False)
                    data['release_ns'] = data['pointer'][-1]['mono_ns']
                finally:
                    pointer.close()
            time.sleep(.85)
            self.public(ident, address)
            final = self.lookup(ident)
            data.update(final=final['at'] + final['size'], endpoint_ns=CLOCK())
            self.capture(folder/'endpoint.png')
            time.sleep(.15)
        finally:
            stop_sampling.set()
            if sampler.ident is not None:
                sampler.join()
            evidence.stop(recording, signal.SIGINT)
            data.update(ended_ns=CLOCK(), sampler_errors=errors)
            evidence.save(folder/'trace.json', data)
        require(recording.returncode == 0 and not errors, f'Capture/telemetry failed: {recording.returncode}, {errors}')
        self.send('close', ident)
        evidence.until(lambda: self.lookup(ident) is None, self.scene)
        time.sleep(.20)
        return data


def pixels(path):
    with Image.open(path) as image:
        return np.array(image.convert('RGB'))


def rect(box, scale):
    x, y, w, h = box
    return (math.floor(x*scale), math.floor(y*scale), math.ceil((x+w)*scale), math.ceil((y+h)*scale))


def mask_for(box, scale, height, width):
    x0, y0, x1, y1 = rect(box, scale)
    mask = np.zeros((height, width), dtype=bool)
    mask[max(0, y0):min(height, y1), max(0, x0):min(width, x1)] = True
    return mask


def marker_count(frame, generation):
    r, g, b = np.moveaxis(frame, 2, 0)
    if generation:
        return int(np.count_nonzero((r > 220) & (g < 45) & (b > 165) & (b < 210)))
    return int(np.count_nonzero((r < 45) & (g > 220) & (b > 150) & (b < 195)))


def video_metrics(folder, case, background, control, motion_end_ns):
    pts, times, stream = evidence.timing(folder)
    height, width = background.shape[:2]
    require((width, height) == (stream['width'], stream['height']), 'Capture dimensions differ from private output')
    require(times, f'Capture and compositor clock could not be aligned: {folder}/timestamps.json')
    scale = case['monitor']['scale']
    samples = case['geometry']
    stamp = [(sample['query_ns']+sample['mono_ns'])//2 for sample in samples]
    require(stamp, 'No authoritative geometry samples')
    presses = [event for event in case['pointer'] if event['kind'] == 'button']
    require([event['pressed'] for event in presses] == [True, False], 'Not a single continuous held grab')
    candidates, signatures, markers, trace = {}, set(), {}, []
    # A ready timestamp can be a queued composition. Allow exactly one refresh
    # plus container quantization after the model end before comparing the first
    # eligible frame and every subsequent frame to the same native endpoint.
    eligibility = max(motion_end_ns, case['release_ns']) + math.ceil(1e9/case['monitor']['refreshRate']) + 1_000_000
    x, y, w, h = case['final']
    initial = mask_for(case['initial'], scale, height, width)
    vacated = initial & ~mask_for(case['final'], scale, height, width)
    old_source = pixels(folder/'initial.png')
    r, g, b = np.moveaxis(old_source, 2, 0)
    old_ink = (124 <= r) & (r <= 132) & (171 <= g) & (g <= 181) & (184 <= b) & (b <= 192)
    old_ink &= vacated & ~mask_for((x-8, y-8, w+16, h+16), scale, height, width)
    old_ink &= np.any(old_source != background, axis=2)
    far_vacated = initial & ~mask_for((x-86, y-86, w+172, h+172), scale, height, width)
    old_print, old_wallpaper = old_source[old_ink], background[far_vacated]
    hold = next(phase for phase in case['phases'] if phase['name'] == 'held-at-destination')
    settled_rows, footprint_rows, marker_rows = [], [], []
    frame_bytes = width*height*3
    command = ['ffmpeg', '-v', 'error', '-i', folder/'output.mkv', '-map', '0:v:0',
               '-fps_mode', 'passthrough', '-enc_time_base', 'demux', '-pix_fmt', 'rgb24', '-f', 'rawvideo', '-']
    with sp.Popen(command, stdout=sp.PIPE) as decoder:
        try:
            for index, (timestamp, pts_s) in enumerate(zip(times, pts)):
                raw = decoder.stdout.read(frame_bytes)
                require(len(raw) == frame_bytes, f'Decoded fewer frames than ffprobe: {folder}')
                frame = np.frombuffer(raw, dtype=np.uint8).reshape(height, width, 3)
                if timestamp >= eligibility:
                    if not settled_rows:
                        Image.fromarray(frame).save(folder/'handoff.png')
                    vacated_diff = int(np.count_nonzero(np.any(frame[vacated] != background[vacated], axis=1)))
                    settled_rows.append(dict(index=index, mono_ns=timestamp, pts_s=pts_s, vacated_diff=vacated_diff,
                                             native_diff=int(np.count_nonzero(np.any(frame != control, axis=2)))))
                elif timestamp >= hold['start_ns'] + 60_000_000:
                    # Allow legitimate crew art near the rim without allowing the
                    # unmistakable initial GTK print or a dirty distant footprint.
                    stage = 'held' if timestamp < case['release_ns'] else 'released'
                    if stage == 'held' and not footprint_rows:
                        Image.fromarray(frame).save(folder/'held-destination.png')
                    footprint_rows.append(dict(index=index, mono_ns=timestamp, phase=stage,
                        old_print_matches=int(np.count_nonzero(np.all(frame[old_ink] == old_print, axis=1))),
                        far_wallpaper_diff=int(np.count_nonzero(np.any(frame[far_vacated] != old_wallpaper, axis=1)))))
                if not case['press_ns'] <= timestamp < case['release_ns']:
                    continue
                phase = next((p['name'] for p in case['phases'] if p['start_ns'] <= timestamp <= p['end_ns']), None)
                if not phase:
                    continue
                counts = tuple(marker_count(frame, gen) for gen in (0, 1))
                marker_rows.append(dict(index=index, mono_ns=timestamp, old=counts[0], new=counts[1]))
                for generation, count in enumerate(counts):
                    if count > 50 and generation not in markers:
                        markers[generation] = dict(index=index, mono_ns=timestamp, phase=phase, pixels=count)
                        if generation == 1:
                            Image.fromarray(frame).save(folder/'marker-changed.png')
                after = bisect.bisect_left(stamp, timestamp)
                nearest = min((i for i in (after-1, after) if 0 <= i < len(stamp)), key=lambda i: abs(stamp[i]-timestamp))
                if abs(stamp[nearest]-timestamp) > 60_000_000:
                    continue
                x0, y0, x1, y1 = rect(samples[nearest]['box'], scale)
                # Amber workwear is absent from the dark print, both markers and the contour map.
                # Require it outside the conservative authoritative body, not just RGB changes
                # due to the moving print. The resulting images still need human inspection.
                apron = math.ceil(86*scale)
                x, y = max(0, x0-apron), max(0, y0-apron)
                right, bottom = min(width, x1+apron), min(height, y1+apron)
                view = frame[y:bottom, x:right]
                r, g, b = np.moveaxis(view.astype(np.int16), 2, 0)
                amber = (r > 155) & (g > 82) & (r > g+12) & (g > b+28)
                amber &= np.any(view != background[y:bottom, x:right], axis=2)
                amber[max(0, y0-y-3):y1-y+3, max(0, x0-x-3):x1-x+3] = False
                count = int(amber.sum())
                trace.append(dict(index=index, pts_s=pts_s, mono_ns=timestamp, phase=phase, box=samples[nearest]['box'],
                                  telemetry_skew_ns=stamp[nearest]-timestamp, off_body_amber_pixels=count))
                if count < 20:
                    continue
                yy, xx = np.where(amber)
                signatures.add(hash((tuple(xx+x-x0), tuple(yy+y-y0))))
                if phase not in candidates or count > candidates[phase][0]:
                    candidates[phase] = (count, index, frame.copy())
            require(decoder.stdout.read(1) == b'' and decoder.wait() == 0, f'Video decoder failed: {folder}')
        finally:
            if decoder.poll() is None:
                decoder.kill()
    active = []
    for phase, (count, index, frame) in candidates.items():
        name = f'worker-{phase}.png'
        Image.fromarray(frame).save(folder/name)
        active.append(dict(file=name, phase=phase, index=index, pts_s=pts[index], amber_pixels=count))
    if candidates:
        Image.fromarray(max(candidates.values(), key=lambda entry: entry[0])[2]).save(folder/'workers.png')
    evidence.save(folder/'pixel-timeline.json', dict(markers=marker_rows, vacated=footprint_rows, settled=settled_rows))
    evidence.save(folder/'frames.json', trace)
    sp.run(['ffmpeg', '-v', 'error', '-i', folder/'output.mkv', '-map', '0:v:0', '-fps_mode', 'passthrough',
            '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast', '-crf', '16', '-pix_fmt', 'yuv420p',
            '-movflags', '+faststart', folder/'review.mp4'], check=True, timeout=60)
    first = settled_rows[0] if settled_rows else None
    later = [row for row in settled_rows if first and row['mono_ns'] >= first['mono_ns'] + 100_000_000]
    new = markers.get(1)
    old_after_new = [row['old'] for row in marker_rows if new and row['index'] >= new['index']]
    held_rows = [row for row in footprint_rows if row['phase'] == 'held']
    return dict(frame_count=len(pts), recording_duration_s=pts[-1]-pts[0], active_worker_frames=active,
                distinct_body_relative_amber_masks=len(signatures), markers=markers,
                max_retired_marker_after_new=max(old_after_new, default=None),
                held_footprint_frames=len(held_rows), old_print_probe_pixels=len(old_print),
                max_old_print_matches=max((row['old_print_matches'] for row in footprint_rows), default=None),
                distant_old_footprint_pixels=len(old_wallpaper),
                max_distant_wallpaper_diff=max((row['far_wallpaper_diff'] for row in footprint_rows), default=None),
                eligible_from_ns=eligibility, first_eligible_frame=first, later_settled_frames=len(later),
                max_settled_native_diff=max((row['native_diff'] for row in settled_rows), default=None),
                max_settled_vacated_diff=max((row['vacated_diff'] for row in settled_rows), default=None),
                mp4_sha256=evidence.digest(folder/'review.mp4'))


def geometry_checks(case):
    initial, final = case['initial'], case['final']
    kind = case['kind']
    dx, dy = PATHS[kind][-1]
    want = [initial[0]+dx, initial[1]+dy, initial[2]-(dx if kind == 'resize' else 0),
            initial[3]-(dy if kind == 'resize' else 0)]
    phases = {p['name']: p for p in case['phases']}
    segment_end = []
    fixed_geometry = True
    for name in ('held-pause-before-marker', 'held-pause-after-marker', 'held-at-destination'):
        phase = phases[name]
        samples = [s['box'] for s in case['geometry'] if phase['start_ns']+70_000_000 < s['query_ns'] < phase['end_ns']]
        segment_end.append(samples[-1] if samples else None)
        fixed_geometry &= len({tuple(s) for s in samples}) == 1
    outbound, reverse, redirect = ('outward', 'reverse', 'redirect')
    signs = []
    for name in (outbound, reverse, redirect):
        phase = phases[name]
        positions = [s['box'][0] for s in case['geometry'] if phase['start_ns'] < s['query_ns'] < phase['end_ns']]
        delta = positions[-1]-positions[0] if len(positions) > 1 else 0
        signs.append(1 if delta > 30 else -1 if delta < -30 else 0)
    boxes = [s['box'] for s in case['geometry']]
    if kind == 'carry':
        invariant = all(b[2:] == initial[2:] for b in boxes)
    else:
        invariant = all(b[0]+b[2] == initial[0]+initial[2] and b[1]+b[3] == initial[1]+initial[3] for b in boxes)
    return dict(expected_final=want, final_matches=want == final, x_direction=signs, held_pause_stable=fixed_geometry,
                held_pause_boxes=segment_end, fixed_size_or_opposite_corner=invariant, distinct_boxes=len(set(map(tuple, boxes))),
                button_down_s=(case['release_ns']-case['press_ns'])*1e-9)


def analyze(out, native, animated):
    background = pixels(out/'background.png')
    native_endpoint, animated_endpoint = (pixels(out/label/'endpoint.png') for label in ('native', 'animated'))
    height, width = background.shape[:2]
    scale = native['monitor']['scale']
    initial = mask_for(animated['initial'], scale, height, width)
    final = mask_for(animated['final'], scale, height, width)
    difference = np.any(animated_endpoint != native_endpoint, axis=2)
    leaked = np.any(animated_endpoint != background, axis=2)
    # Exact native-calibrated screenshot comparisons. No subpixel remapping or masking
    # of the entire old footprint; we subtract the still-occupied final rectangle only.
    comparison = dict(same_geometry=native['final'] == animated['final'], full_endpoint_diff_pixels=int(difference.sum()),
                      client_endpoint_diff_pixels=int((difference & final).sum()),
                      vacated_pixels=int((initial & ~final).sum()),
                      vacated_non_wallpaper_pixels=int((leaked & initial & ~final).sum()),
                      outside_body_diff_from_native_pixels=int((difference & ~final).sum()))
    log = (out/'private-hyprland.log').read_text(errors='replace')
    all_events = sorted({(event, int(seed), int(ns)) for event, seed, ns in EVENT.findall(log)}, key=lambda item: item[2])
    cases, failed = {}, []
    for label, case in [('native', native), ('animated', animated)]:
        events = [dict(kind=event, seed=seed, mono_ns=ns) for event, seed, ns in all_events
                  if case['began_ns'] <= ns <= case['ended_ns']]
        geom = geometry_checks(case)
        motion_end = next((event['mono_ns'] for event in reversed(events) if event['kind'].endswith('-end')),
                          case['release_ns'])
        video = video_metrics(out/label, case, background, native_endpoint, motion_end)
        seeds = sorted({e['seed'] for e in events})
        resources = sorted({line for line in evidence.RESOURCE.findall(log) if any(f'seed={seed} ' in line for seed in seeds)})
        duration = (events[-1]['mono_ns'] - events[0]['mono_ns']) * 1e-9 if len(events) >= 2 else None
        cases[label] = dict(geometry=geom, video=video, events=events, motion_lifetime_s=duration,
                            seeds=seeds, resources=resources)
        if not (geom['final_matches'] and geom['x_direction'] == [1, -1, 1] and geom['held_pause_stable']
                and geom['fixed_size_or_opposite_corner'] and geom['distinct_boxes'] > 10):
            failed.append(f'{label}: missing authoritative reversal, held pause, invariant or exact final box')
        old, new = (video['markers'].get(i) for i in (0, 1))
        if not (old and new and old['mono_ns'] < case['generation_request_ns'] <= new['mono_ns'] < case['release_ns']
                and video['max_retired_marker_after_new'] == 0):
            failed.append(f'{label}: marker did not visibly replace the old generation cleanly during the grab')
        if not (video['held_footprint_frames'] >= 2 and video['old_print_probe_pixels'] >= 100
                and video['max_old_print_matches'] == 0 and video['max_distant_wallpaper_diff'] == 0):
            failed.append(f'{label}: old footprint not clean during the final hold and release')
        if not (video['first_eligible_frame'] and video['later_settled_frames']
                and video['max_settled_native_diff'] == 0 and video['max_settled_vacated_diff'] == 0):
            failed.append(f'{label}: first eligible handoff and later frames did not both match the exact native control')
        if label == 'animated':
            kinds = [event['kind'] for event in events]
            expected = case['kind']
            expected_start, expected_end = f'{expected}-start', f'{expected}-end'
            if (kinds != [expected_start, expected_end] or len(seeds) != 1
                    or not (case['press_ns'] <= events[0]['mono_ns'] < case['release_ns'] <= events[-1]['mono_ns'])):
                failed.append(f'{label}: missing matching motion seed/start and post-release end')
            if len(video['active_worker_frames']) < 2 or video['distinct_body_relative_amber_masks'] < 3:
                failed.append(f'{label}: no distinct worker frames separated from moving GTK')
        elif events or video['active_worker_frames']:
            failed.append(f'{label}: opt-out unexpectedly drew motion actors or admitted the motion model')
    if not (comparison['same_geometry'] and not comparison['full_endpoint_diff_pixels']):
        failed.append('Animated and native endpoint pixels or geometry differ')
    if not (comparison['vacated_pixels'] and not comparison['vacated_non_wallpaper_pixels']):
        failed.append('Empty or unhealed vacated old footprint')
    result = dict(comparison=comparison, cases=cases, failed=failed)
    evidence.save(out/'result.json', result)
    return failed


def launch(args):
    require(args.take and re.fullmatch(r'take-[0-9]+', args.take), 'Use a fresh --take take-NN')
    root = HERE.parents[1]
    build = root/'build'
    stock = build/'upstream-stock-948ac1ab'
    trial = build/'construction-live-motion-01'
    plugin = trial/'obj/hyprimate.so'
    require(evidence.digest(plugin) == args.sealed_sha256, 'Artifact does not match the authorized seal')
    host_digests = {Path(path).name: digest for digest, path in
                    (line.split(maxsplit=1) for line in (stock/'provenance-host.sha256').read_text().splitlines())}
    host_files = [stock/'obj/Hyprland', stock/'obj/hyprctl/hyprctl', stock/'prefix/lib/libaquamarine.so.0.15.1']
    for path in host_files:
        require(evidence.digest(path) == host_digests.get(path.name), f'Stock host seal mismatch: {path}')
    take = trial/args.take
    take.mkdir(mode=0o700)
    captures = [item for scale in (args.scale or []) for item in ('--scale', str(scale))]
    captures += [item for kind in (args.kind or []) for item in ('--kind', kind)]
    sandbox = ['--unshare-all', '--as-pid-1', '--new-session', '--die-with-parent',
               '--ro-bind', '/usr', '/usr', '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib', '/lib64',
               '--symlink', 'usr/bin', '/bin', '--dev', '/dev', '--dir', '/dev/dri',
               '--ro-bind', '/sys', '/sys', '--proc', '/proc', '--tmpfs', '/run', '--tmpfs', '/tmp',
               '--ro-bind', '/run/udev/data', '/run/udev/data',
               '--perms', '0700', '--dir', '/tmp/home', '--perms', '0700', '--dir', '/tmp/runtime',
               '--ro-bind', str(stock/'prefix/lib'), '/opt/lib', '--dir', '/opt/bin',
               '--ro-bind', str(host_files[0]), '/opt/bin/Hyprland',
               '--ro-bind', str(host_files[1]), '/opt/bin/hyprctl',
               '--ro-bind', str(plugin), str(PLUGIN),
               '--ro-bind', str(stock/'capture-check/wf-recorder'), '/opt/bin/wf-recorder',
               '--bind', str(take), str(OUTPUT), '--chdir', '/tmp']
    for name in ('fonts', 'dbus-1', 'passwd', 'group', 'machine-id', 'nsswitch.conf', 'ld.so.cache', 'localtime'):
        path = '/etc/'+name
        sandbox.extend(('--ro-bind-try', path, path))
    for name in ('dri/renderD128', 'dri/renderD129', 'nvidiactl', 'nvidia0', 'nvidia1', 'nvidia-uvm'):
        path = '/dev/'+name
        sandbox.extend(('--dev-bind', path, path))
    nested = Path('/opt/slice/tests/nested')
    for name in (Path(__file__).name, 'construction_evidence.py', 'burn_evidence.py', 'paper_scene.py'):
        sandbox.extend(('--ro-bind', str(HERE/name), str(nested/name)))
    env = dict(PATH='/opt/bin:/usr/bin', LANG='C.UTF-8', LD_LIBRARY_PATH='/opt/lib',
               KWIN_RENDER_NODES='/dev/dri/renderD128', GBM_BACKEND='nvidia-drm',
               HOME='/tmp/home', XDG_RUNTIME_DIR='/tmp/runtime', XDG_SESSION_TYPE='wayland',
               GDK_BACKEND='wayland', GSETTINGS_BACKEND='memory', NO_AT_BRIDGE='1', QT_ACCESSIBILITY='0')
    for key, value in env.items():
        sandbox.extend(('--setenv', key, value))
    command = ['/usr/bin/env', '-i', '/usr/bin/bwrap', *sandbox, '--', '/usr/bin/python', '-B',
               str(nested/Path(__file__).name), '--inside', '--sealed-sha256', args.sealed_sha256, *captures]
    evidence.save(take/'launch.json', dict(command=command, stock_host_sha256=host_digests))
    os.execv(command[0], command)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sealed-sha256', required=True)
    parser.add_argument('--take', help='fresh output name under build/construction-live-motion-01, e.g. take-01')
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--scale', type=float, choices=(1., 1.5), action='append')
    parser.add_argument('--kind', choices=tuple(PATHS), action='append')
    args = parser.parse_args()
    require(re.fullmatch('[a-f0-9]{64}', args.sealed_sha256), 'Invalid sealed SHA-256')
    if not args.inside:
        launch(args)
    require(os.environ.get('HOME') == '/tmp/home' and os.environ.get('XDG_RUNTIME_DIR') == '/tmp/runtime'
            and not Path('/run/user').exists() and os.getpid() == 1
            and not any(key in os.environ for key in ('WAYLAND_DISPLAY', 'DISPLAY', 'WAYLAND_SOCKET',
                                                      'DBUS_SESSION_BUS_ADDRESS', 'HYPRLAND_INSTANCE_SIGNATURE')),
            'Refusing capture outside the private bwrap launcher')
    require(PLUGIN.is_file() and OUTPUT.is_dir(), 'Invalid sandbox artifact or output')
    summary = []
    for scale in args.scale or (1., 1.5):
        for kind in args.kind or PATHS:
            out = OUTPUT/f'scale-{scale:g}'/kind
            out.mkdir(parents=True)
            try:
                with tempfile.TemporaryDirectory(prefix='cm.', dir='/tmp/runtime') as runtime:
                    session = MotionSession(Path(runtime), out, PLUGIN, '/opt/bin/wf-recorder')
                    try:
                        session.boot(scale, kind, args.sealed_sha256)
                        native = session.take(kind, 'native', 20)
                        animated = session.take(kind, 'animated', 21)
                    finally:
                        session.cleanup()
                failures = analyze(out, native, animated)
            except (OSError, RuntimeError, ValueError, sp.SubprocessError) as exc:
                failures = [f'Evidence unavailable: {exc}']
                evidence.save(out/'unavailable.json', failures)
            summary.append(dict(scale=scale, kind=kind, failed=failures))
            evidence.save(OUTPUT/'summary.json', summary)
    print(json.dumps(summary, indent=2))
    return int(any(case['failed'] for case in summary))


if __name__ == '__main__':
    sys.exit(main())
