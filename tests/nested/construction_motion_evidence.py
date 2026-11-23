#!/usr/bin/env python3
"""Film paired Construction motion and close takes on public GTK specimens.

Use --installed-abi --sealed-sha256 SHA --take take-NN for the remaining
Construction variants. The controller always re-execs in the private sandbox.
Lossless VFR capture and PNGs are the comparison sources, not the review MP4s.
"""

import argparse
import bisect
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import struct
import subprocess as sp
import sys
import tempfile
import threading
import time

import numpy as np
from PIL import Image, ImageDraw

import construction_evidence as evidence

CLOCK = time.monotonic_ns
STOCK_HOST = '948ac1abcd398f30460a5fb6951c7d46ba16bac7'
INSTALLED_HOST = '0aa6745f705f2c4b0a5004eb019ef76e5bbdb426'
PLUGIN = Path('/opt/bin/hyprimate.so')
OUTPUT = Path('/evidence')
HERE = Path(__file__).resolve().parent
SIZE = (480, 320)
ORIGIN = (170, 112)
# One button-down per take. The two stationary intervals are not separate grabs.
PATHS = {
    'carry': ((184, 74), (66, 28), (234, 62)),
    'resize': ((112, 64), (-62, -32), (64, 48)),
    'crane': ((225, 56), (56, 22), (272, 62)),
    'pieces': ((254, 63), (21, 18), (280, 72)),
}
VARIANTS = {'carry': 'crew_carry', 'resize': 'crew_carry', 'crane': 'crane', 'pieces': 'piece_by_piece'}
REMAINING = ('crane', 'pieces', 'regional-close')
EVENT = re.compile(r'\[hyprimate\] construction event=([a-z][a-z-]*) seed=(\d+) mono_ns=(\d+)')
OPEN_SECONDS = 1.20  # Deliberate override only for the interrupted-open take.
CLOSE_TOKENS = ((253, 42, 65), (222, 36, 237), (61, 82, 247))
PIECE_MARKS = (((72, 114, 253), .24, .15), ((247, 47, 36), .75, .15), ((36, 248, 91), .23, .80))


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
    def boot(self, scale, kind, sealed_sha, installed, seam):
        cfg = Path(self.env['XDG_CONFIG_HOME'])
        cfg.mkdir()
        # Packaged Hyprland falls back to 1280x720 if the parent configure is empty.
        (cfg/'kwinrulesrc').write_text(
            '[General]\ncount=1\nrules=fixture-size\n\n[fixture-size]\nDescription=Private test output\n'
            'wmclass=.*\nwmclassmatch=3\nsize=1440,900\nsizerule=2\nnoborder=true\nnoborderrule=2\n')
        output_count = ['--output-count', '2'] if seam else []
        parent = self.start(self.out/'kwin.log', ['dbus-run-session', '--', 'kwin_wayland', '--virtual',
                            '--width', '1440', '--height', '900', *output_count,
                            '--no-lockscreen', '--no-global-shortcuts', '--socket', 'construction-parent'])
        evidence.until(lambda: (self.runtime/'construction-parent').is_socket(), parent)
        self.env.update(WAYLAND_DISPLAY='construction-parent', AQ_BACKENDS='wayland')
        evidence.until(lambda: 'wl_output' in sp.run(['wayland-info'], env=self.env, capture_output=True,
                                            text=True, timeout=5).stdout, parent)
        config = self.runtime/'motion.lua'
        dispatch = 'resize' if kind == 'resize' else 'drag'
        closing = str(kind == 'regional-close').lower()
        self.origin = (ORIGIN[0], 200) if kind == 'crane' else ORIGIN
        config.write_text(f'''
            hl.monitor({{output='',mode='1440x900@60',position='0x0',scale={scale}}})
            hl.config({{debug={{disable_logs=false,enable_stdout_logs=true}}, cursor={{invisible=true}},
                general={{border_size=0}}, misc={{disable_hyprland_logo=true,disable_splash_rendering=true}},
                decoration={{rounding=0,shadow={{enabled=false}},blur={{enabled=false}}}},
                animations={{enabled=true}}}})
            hl.window_rule({{name='construction-public',match={{title='^PaperEvidence-2[0-9]$'}},
                float=true,move='{self.origin[0]} {self.origin[1]}',no_screen_share=false}})
            hl.animation({{leaf='windowsIn',enabled={closing},speed=3,bezier='default',style='popin 87%'}})
            hl.animation({{leaf='windowsOut',enabled={closing},speed=3,bezier='default',style='popin 87%'}})
            hl.animation({{leaf='windowsMove',enabled=false}})
            hl.animation({{leaf='fade',enabled=false}})
            hl.animation({{leaf='fadeOut',enabled={closing},speed=3,bezier='default'}})
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
        revision = INSTALLED_HOST if installed else STOCK_HOST
        require(revision in version, f'Unexpected Hyprland revision: {version}')
        def ready_output():
            outputs = json.loads(self.hc('-j', 'monitors'))
            if len(outputs) == 1 and outputs[0]['name'].startswith('WAYLAND-') and outputs[0]['width'] > 0:
                return outputs[0]
            return None

        try:
            monitor = evidence.until(ready_output, self.hypr)
        except RuntimeError as error:
            raise RuntimeError(f'Private Wayland output did not come up: {self.hc("-j", "monitors")}') from error
        self.output = monitor['name']
        self.other = None
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

    def setup_seam(self):
        require(self.hc('output', 'create', 'wayland') == 'ok',
                'Private child rejected creation of a second nested Wayland output')
        # Render an ordinary public setup window on the originating output. Some
        # packaged Wayland backends leave the second xdg_surface commit queued
        # until the first output produces Wayland traffic again.
        self.options('native', movement=False)
        self.hc('dispatch', f'hl.dsp.focus({{monitor="{self.output}"}})')
        self.send('open', 29, width=256, height=180, construction='dark')
        temporary = evidence.until(lambda: self.lookup(29), self.scene)
        self.public(29, temporary['address'])
        self.send('generation', 29, generation=1)
        try:
            self.other = evidence.until(lambda: next((m['name'] for m in json.loads(self.hc('-j', 'monitors'))
                        if m['name'].startswith('WAYLAND-') and m['name'] != self.output), None), self.hypr)
        except RuntimeError as error:
            raise RuntimeError(f'Aquamarine created the second toplevel but did not advertise a second output: '
                               f'{self.hc("-j", "monitors")}') from error
        finally:
            self.send('close', 29)
            evidence.until(lambda: self.lookup(29) is None, self.scene)
        self.hc('eval', f'hl.monitor({{output="{self.output}",mode="1440x900@60",position="0x0",scale=1}});'
                        f'hl.monitor({{output="{self.other}",mode="1440x900@60",position="1440x0",scale=1}})')

        def adjacent():
            outputs = sorted(json.loads(self.hc('-j', 'monitors')), key=lambda monitor: monitor['x'])
            expected = [(self.output, 0, 0, 1440, 900, 1, 0), (self.other, 1440, 0, 1440, 900, 1, 0)]
            actual = [tuple(m[k] for k in ('name', 'x', 'y', 'width', 'height', 'scale', 'transform')) for m in outputs]
            return outputs if actual == expected else None

        try:
            outputs = evidence.until(adjacent, self.hypr)
        except RuntimeError as error:
            raise RuntimeError(f'Private outputs did not reach the adjacent 2880x900 layout: {self.hc("-j", "monitors")}') from error
        self.extent = (2880, 900)
        self.hc('dismissnotify', '-1')
        time.sleep(.30)
        self.capture(self.out/'background.png')
        self.capture(self.out/'neighbor-background.png', self.other)
        evidence.save(self.out/'seam-layout.json', dict(outputs=outputs, extent=self.extent,
                      owner=self.output, neighbor=self.other, outer_owner_edge='left'))

    def public(self, ident, address):
        clients = [client for client in json.loads(self.hc('-j', 'clients')) if client['mapped']]
        require(len(clients) == 1 and clients[0]['pid'] == self.scene.pid
                and clients[0]['title'] == f'PaperEvidence-{ident}' and clients[0]['address'] == address,
                f'Unexpected mapped client set: {clients}')
        rule = self.hc('getprop', 'address:'+address, 'no_screen_share')
        require(rule == 'false', f'Addressed no_screen_share is not false: {address}: {rule}')
        return rule

    def options(self, variant, *, opening=False, closing=False, movement=True):
        boolean = lambda value: str(value).lower()
        self.hc('eval', 'hl.config({plugin={hyprimate={window={enabled=true,style="construction",'
                f'movement={{enabled={boolean(movement)},options={{construction={{variant="{variant}"}}}}}},'
                f'open={{enabled={boolean(opening)},options={{construction={{duration={OPEN_SECONDS}}}}}}},'
                f'close={{enabled={boolean(closing)}}}' + '}}}})')
        wanted = {'enabled': 1, 'style': 'construction', 'open:enabled': int(opening),
                  'close:enabled': int(closing), 'movement:enabled': int(movement),
                  'movement:options:construction:variant': variant}
        options = {key: json.loads(self.hc('-j', 'getoption', 'plugin:hyprimate:window:'+key)) for key in wanted}
        for key, expected in wanted.items():
            raw = options[key]
            actual = raw.get('str') if isinstance(expected, str) else raw.get('int', raw.get('bool'))
            require(actual == expected, f'Unapplied option {key}: wanted {expected}, got {raw}')
        if opening:
            raw = json.loads(self.hc('-j', 'getoption', 'plugin:hyprimate:window:open:options:construction:duration'))
            require(abs(raw['float'] - OPEN_SECONDS) < 1e-5, f'Unexpected opening duration: {raw}')
            options['open:options:construction:duration'] = raw
        return options

    def capture(self, path, output=None):
        sp.run(['grim', '-o', output or self.output, path], env=self.env, check=True, timeout=10)

    def record(self, folder, output=None):
        return self.start(folder/'recorder.log', [self.recorder, '--no-dmabuf', '-y', '-D', '-o', output or self.output,
                          '-c', 'libx264rgb', '-p', 'crf=0', '-p', 'preset=ultrafast', '-p', 'threads=2',
                          '-f', folder/'output.mkv'], extra={'WAYLAND_DEBUG': 'client'})

    def await_recorder(self, folder, recording):
        log = folder/'recorder.log'
        evidence.until(lambda: evidence.READY.search(log.read_text(errors='replace')), recording)

    def specimen(self, ident, size, data, marks=()):
        self.send('open', ident, width=size[0], height=size[1], construction='dark', evidence_marks=marks)
        client = evidence.until(lambda: self.lookup(ident), self.scene)
        address = client['address']
        data.update(address=address, no_screen_share=self.public(ident, address), initial=client['at'] + client['size'])
        require(tuple(data['initial']) == self.origin + size, f'Unexpected starting geometry: {data["initial"]}')
        return address

    def take(self, kind, label, ident):
        folder = self.out/label
        folder.mkdir()
        variant = VARIANTS[kind] if label == 'animated' else 'native'
        data = dict(kind=kind, variant=variant, monitor=self.monitor(), options=self.options(variant),
                    began_ns=CLOCK(), specimen=dict(id=ident, pattern='dark', start_size=SIZE), phases=[], pointer=[], geometry=[])
        path = PATHS[kind]
        if self.other:
            self.hc('dispatch', f'hl.dsp.focus({{monitor="{self.output}"}})')
            if kind == 'pieces':
                path = (*path[:2], (1440 - self.origin[0] - 180, path[2][1]))
        data['path'] = path
        marks = PIECE_MARKS if kind == 'pieces' else []
        address = self.specimen(ident, SIZE, data, marks)
        time.sleep(.30)
        self.capture(folder/'initial.png')
        recordings = [(folder, self.record(folder))]
        stop_sampling = threading.Event()
        errors = []

        def sample():
            try:
                while not stop_sampling.is_set():
                    started = CLOCK()
                    window = self.lookup(ident)
                    require(window is not None and window['address'] == address, 'Mapped specimen changed during grab')
                    data['geometry'].append(dict(query_ns=started, mono_ns=CLOCK(), box=window['at'] + window['size'],
                                                 monitor=window['monitor']))
                    stop_sampling.wait(.024)
            except Exception as exc:
                errors.append(str(exc))

        sampler = threading.Thread(target=sample)
        try:
            if self.other:
                neighbor = folder/'neighbor'
                neighbor.mkdir()
                recordings.append((neighbor, self.record(neighbor, self.other)))
            for destination, recording in recordings:
                self.await_recorder(destination, recording)
            sampler.start()
            time.sleep(.28)
            with socket.socket(socket.AF_UNIX) as sock:
                data['pointer_peer'] = peer(sock, self.wayland_socket, self.hypr.pid)
                pointer = HeldPointer(sock, self.extent, data['pointer'])
                try:
                    pointer.connect()
                    x, y = self.origin
                    start = (x+18, y+18) if kind == 'resize' else (x + SIZE[0]//2, y + SIZE[1]//2)
                    targets = [(start[0]+dx, start[1]+dy) for dx, dy in path]
                    pointer.move(start)
                    time.sleep(.09)
                    pointer.button(True)
                    data['press_ns'] = data['pointer'][-1]['mono_ns']
                    schedule = [('pickup', None, .15), ('outward', targets[0], .67),
                                ('reverse', targets[1], .65), ('held-pause-before-marker', None, .15),
                                ('held-pause-after-marker', None, .23), ('redirect', targets[2], .72),
                                ('held-at-destination', None, .28)]
                    marker_phase = 'redirect' if kind == 'pieces' else 'held-pause-after-marker'
                    for phase, point, seconds in schedule:
                        start_ns = CLOCK()
                        if phase == marker_phase:
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
            # Keep recording through mechanical departure, not just window set-down.
            time.sleep({'crane': 4.2, 'pieces': 2.3}.get(kind, .85) if label == 'animated' else .85)
            self.public(ident, address)
            final = self.lookup(ident)
            data.update(final=final['at'] + final['size'], endpoint_ns=CLOCK())
            self.capture(folder/'endpoint.png')
            if self.other:
                self.capture(neighbor/'endpoint.png', self.other)
            time.sleep(.15)
        finally:
            stop_sampling.set()
            if sampler.ident is not None:
                sampler.join()
            for _, recording in recordings:
                evidence.stop(recording, signal.SIGINT)
            data.update(ended_ns=CLOCK(), sampler_errors=errors)
            evidence.save(folder/'trace.json', data)
        returncodes = [recording.returncode for _, recording in recordings]
        require(all(code == 0 for code in returncodes) and not errors, f'Capture/telemetry failed: {returncodes}, {errors}')
        self.send('close', ident)
        evidence.until(lambda: self.lookup(ident) is None, self.scene)
        time.sleep(.20)
        return data

    def close_take(self, label, ident, *, interrupted=False, fade_in=False):
        folder = self.out/label
        folder.mkdir()
        width = 800 if self.extent[0] >= 1200 else 640
        size = (width, 500 if width == 800 else 420)
        enabled = label != 'native'
        data = dict(label=label, kind='regional-close', id=ident, size=size, interrupted=interrupted,
                    monitor=self.monitor(), began_ns=CLOCK(),
                    options=self.options('native', opening=interrupted, closing=enabled, movement=False))
        token = CLOSE_TOKENS[ident-20]
        marks = [(token, x, y) for x in (.13, .38, .63, .84) for y in (.15, .41, .69, .87)]
        data['token_rgb'] = token
        if enabled:
            data['close_duration_s'] = json.loads(self.hc('-j', 'getoption',
                'plugin:hyprimate:window:close:options:construction:duration'))['float']
        if fade_in:
            self.hc('eval', 'hl.curve("fixtureLinear",{type="bezier",points={{0,0},{1,1}}});'
                            'hl.animation({leaf="fadeIn",enabled=true,speed=20,bezier="fixtureLinear"})')
            data['fade_in_seconds'] = 2.0
            data['native_animations'] = json.loads(self.hc('-j', 'animations'))
        if not (interrupted or fade_in):
            self.specimen(ident, size, data, marks)
            time.sleep(.35)
            self.capture(folder/'initial.png')
        recording = self.record(folder)
        try:
            self.await_recorder(folder, recording)
            time.sleep(.25)
            if interrupted or fade_in:
                data['open_request_ns'] = CLOCK()
                self.specimen(ident, size, data, marks)
                # Native geometry pop-in is 0.30 s; destroy during the independently long fade.
                time.sleep(.58 if fade_in else OPEN_SECONDS * .33)
            data['close_request_ns'] = CLOCK()
            self.send('close', ident)
            evidence.until(lambda: self.lookup(ident) is None, self.scene)
            data['unmapped_ns'] = CLOCK()
            require(not any(client['mapped'] for client in json.loads(self.hc('-j', 'clients'))),
                    'Unexpected client remains in the private run after destroy')
            time.sleep(data.get('close_duration_s', 1.0) + .80)
            self.capture(folder/'endpoint.png')
            time.sleep(.15)
        finally:
            evidence.stop(recording, signal.SIGINT)
            data['ended_ns'] = CLOCK()
            evidence.save(folder/'trace.json', data)
        require(recording.returncode == 0, f'Capture failed: {folder}/recorder.log')
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

def print_ink(frame):
    r, g, b = np.moveaxis(frame, 2, 0)
    return (124 <= r) & (r <= 132) & (171 <= g) & (g <= 181) & (184 <= b) & (b <= 192)


def decoder_for(folder):
    return sp.Popen(['ffmpeg', '-v', 'error', '-threads', '2', '-i', folder/'output.mkv', '-map', '0:v:0',
                     '-fps_mode', 'passthrough', '-enc_time_base', '1:1000',
                     '-pix_fmt', 'rgb24', '-f', 'rawvideo', '-threads', '1', '-'],
                    stdout=sp.PIPE)


def read_frame(decoder, folder, width, height):
    raw = decoder.stdout.read(width * height * 3)
    require(len(raw) == width * height * 3, f'Decoded fewer frames than ffprobe: {folder}')
    return np.frombuffer(raw, dtype=np.uint8).reshape(height, width, 3)


def review_video(folder):
    file = folder/'review.mp4'
    sp.run(['ffmpeg', '-v', 'error', '-i', folder/'output.mkv', '-map', '0:v:0', '-fps_mode', 'passthrough',
            '-c:v', 'libx264', '-threads', '2', '-preset', 'veryfast', '-crf', '16', '-pix_fmt', 'yuv420p',
            '-movflags', '+faststart', file], check=True, timeout=60)
    return {file.name: evidence.digest(file)}

def contact_sheet(folder, stills):
    ordered = sorted(stills, key=lambda entry: entry[1])
    sheet = Image.new('RGB', (960, 220*math.ceil(len(ordered)/3)), 'black')
    text = ImageDraw.Draw(sheet)
    for i, (label, index, offset_ms) in enumerate(ordered):
        x, y = (i%3)*320, (i//3)*220
        with Image.open(folder/f'{label}.png') as image:
            image.thumbnail((320, 200))
            sheet.paste(image, (x, y))
        text.text((x+5, y+202), f'{label}  f{index}  {offset_ms:+.0f} ms', fill='white')
    sheet.save(folder/'contact-sheet.png')


def token_mask(frame, rgb):
    r, g, b = np.moveaxis(frame, 2, 0)
    return ((r >= rgb[0]-6) & (r <= rgb[0]+6) & (g >= rgb[1]-6) & (g <= rgb[1]+6)
            & (b >= rgb[2]-6) & (b <= rgb[2]+6))


def token_bounds(frame, rgb):
    yy, xx = np.nonzero(token_mask(frame, rgb))
    if xx.size >= 20:
        return [int(xx.min()), int(yy.min()), int(xx.max()), int(yy.max()), int(xx.size)]
    return None



def lifecycle(out, case):
    # The compositor also writes stdout. The copied in-process log can omit
    # records still buffered when cleanup starts; de-duplicate both sources.
    log = '\n'.join((out/name).read_text(errors='replace') for name in ('private-hyprland.log', 'hyprland.log'))
    events = {(event, int(seed), int(ns)) for event, seed, ns in EVENT.findall(log)
              if case['began_ns'] <= int(ns) <= case['ended_ns']}
    rows = [dict(kind=event, seed=seed, mono_ns=ns) for event, seed, ns in sorted(events, key=lambda item: item[2])]
    resources = sorted({line for line in evidence.RESOURCE.findall(log)
                        if any(f"seed={event['seed']} " in line for event in rows)})
    return rows, resources


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
    final = mask_for(case['final'], scale, height, width)
    vacated = initial & ~final
    old_source = pixels(folder/'initial.png')
    old_ink = print_ink(old_source)
    old_ink &= vacated & ~mask_for((x-8, y-8, w+16, h+16), scale, height, width)
    old_ink &= np.any(old_source != background, axis=2)
    far_vacated = initial & ~mask_for((x-86, y-86, w+172, h+172), scale, height, width)
    old_print, old_wallpaper = old_source[old_ink], background[far_vacated]
    hold = next(phase for phase in case['phases'] if phase['name'] == 'held-at-destination')
    settled_rows, footprint_rows, marker_rows = [], [], []
    kind = case['kind']
    departure, retarget = [], []
    last_crane_frame = None
    reference_marker = evidence.sentinel(control, 1) if kind == 'pieces' else None
    landmark_references = [(color, token_bounds(control, color)) for color, _, _ in PIECE_MARKS] if kind == 'pieces' else []
    indices = {}
    if kind in REMAINING:
        segments = ('outward', 'reverse', 'held-pause-after-marker', 'redirect', 'held-at-destination')
        witness_times = [(p['name'], (p['start_ns']+p['end_ns'])//2) for p in case['phases'] if p['name'] in segments]
        witness_times += [('before-release', case['release_ns'] - 25_000_000),
                          ('set-down', case['release_ns'] + 240_000_000),
                          ('tail', case['release_ns'] + 680_000_000), ('settled', times[-1])]
        if kind == 'crane':
            witness_times += [('drive-away', case['release_ns'] + 1_150_000_000),
                              ('edge-approach', case['release_ns'] + 1_550_000_000)]
        for label, ns in witness_times:
            indices.setdefault(bisect.bisect_left(times, ns), []).append(label)
    stills = []
    with decoder_for(folder) as decoder:
        try:
            for index, (timestamp, pts_s) in enumerate(zip(times, pts)):
                frame = read_frame(decoder, folder, width, height)
                for label in indices.get(index, ()):
                    Image.fromarray(frame).save(folder/f'{label}.png')
                    stills.append((label, index, (timestamp-case['release_ns'])/1e6))
                if kind == 'crane' and case['release_ns'] <= timestamp < eligibility:
                    yy, xx = np.nonzero(np.any(frame != control, axis=2) & ~final)
                    if xx.size:
                        box = [int(xx.min()), int(yy.min()), int(xx.max()), int(yy.max())]
                        edge = box[0] < 3 or box[2] > width - 4
                        if not departure:
                            Image.fromarray(frame).save(folder/'tail-set-down.png')
                        last_crane_frame = frame
                        departure.append(dict(index=index, mono_ns=timestamp, outside_native_box=box,
                                              outside_native_pixels=int(xx.size), touches_output_edge=edge))
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
                if reference_marker and phase in ('reverse', 'redirect'):
                    observed = evidence.sentinel(frame, int(timestamp >= case['generation_request_ns']))
                    if observed and observed[4] >= 24:
                        xf, yf, _, _ = rect(case['final'], scale)
                        # Compare against the native take's residual rather than treating IPC
                        # readback as a compositor-synchronized position.
                        residual_x = observed[0] - (reference_marker[0] + x0 - xf)
                        residual_y = observed[1] - (reference_marker[1] + y0 - yf)
                        landmarks = []
                        for color, reference in landmark_references:
                            witness = token_bounds(frame, color)
                            if witness and reference:
                                relative = [witness[i] - reference[i] - (observed[i] - reference_marker[i])
                                            for i in range(2)]
                                landmarks.append(dict(rgb=color, bounds=witness, independent_residual_xy=relative))
                        independent = max((m['independent_residual_xy'] for m in landmarks),
                                          key=lambda d: max(map(abs, d)), default=None)
                        client_area = frame[max(0,y0):y1, max(0,x0):x1]
                        background_area = background[max(0,y0):y1, max(0,x0):x1]
                        exposed = int(np.count_nonzero(np.all(client_area == background_area, axis=2)))
                        retarget.append(dict(index=index, mono_ns=timestamp, phase=phase, marker=observed,
                                             generation=int(timestamp >= case['generation_request_ns']),
                                             landmarks=landmarks, independent_residual_xy=independent,
                                             residual_xy=[residual_x, residual_y], native_body_wallpaper_px=exposed))
                        threshold = round(35 * scale)
                        if max(abs(residual_x), abs(residual_y)) >= threshold:
                            file = folder/f'client-piece-{phase}.png'
                            if not file.exists():
                                Image.fromarray(frame).save(file)
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
    if last_crane_frame is not None:
        Image.fromarray(last_crane_frame).save(folder/'tail-last-visible.png')
        row = departure[-1]
        stills.append(('tail-last-visible', row['index'], (row['mono_ns']-case['release_ns'])/1e6))
    if stills:
        contact_sheet(folder, stills)
    active = []
    for phase, (count, index, frame) in candidates.items():
        name = f'worker-{phase}.png'
        Image.fromarray(frame).save(folder/name)
        active.append(dict(file=name, phase=phase, index=index, pts_s=pts[index], amber_pixels=count))
    if candidates:
        Image.fromarray(max(candidates.values(), key=lambda entry: entry[0])[2]).save(folder/'workers.png')
    evidence.save(folder/'pixel-timeline.json', dict(markers=marker_rows, vacated=footprint_rows,
                  settled=settled_rows, departure=departure, retarget=retarget))
    evidence.save(folder/'frames.json', trace)
    review = review_video(folder)
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
                departure=departure, retarget=retarget, review_sha256=review, mp4_sha256=review['review.mp4'])


def geometry_checks(case):
    initial, final = case['initial'], case['final']
    kind = case['kind']
    dx, dy = case['path'][-1]
    want = [initial[0]+dx, initial[1]+dy, initial[2]-(dx if kind == 'resize' else 0),
            initial[3]-(dy if kind == 'resize' else 0)]
    phases = {p['name']: p for p in case['phases']}
    # Repainting the marker splits the trace, not the physical pause.
    pauses = [(phases['held-pause-before-marker']['start_ns'], phases['held-pause-after-marker']['end_ns']),
              (phases['held-at-destination']['start_ns'], phases['held-at-destination']['end_ns'])]
    segment_end, pause_queries = [], []
    fixed_geometry = True
    for start, end in pauses:
        # A query can begin while held and return after the next motion was
        # dispatched. Its reply is not evidence about the earlier pause.
        samples = [s['box'] for s in case['geometry']
                   if start + 70_000_000 < s['query_ns'] <= s['mono_ns'] < end]
        segment_end.append(samples[-1] if samples else None)
        pause_queries.append(len(samples))
        fixed_geometry &= (len(samples) >= 2 and len({tuple(s) for s in samples}) == 1 and
                           not any(event['kind'] == 'motion_absolute' and start <= event['mono_ns'] < end
                                   for event in case['pointer']))
    outbound, reverse, redirect = ('outward', 'reverse', 'redirect')
    signs = []
    for name in (outbound, reverse, redirect):
        phase = phases[name]
        positions = [s['box'][0] for s in case['geometry']
                     if phase['start_ns'] < s['query_ns'] <= s['mono_ns'] < phase['end_ns']]
        delta = positions[-1]-positions[0] if len(positions) > 1 else 0
        signs.append(1 if delta > 30 else -1 if delta < -30 else 0)
    boxes = [s['box'] for s in case['geometry']]
    if kind != 'resize':
        invariant = all(b[2:] == initial[2:] for b in boxes)
    else:
        invariant = all(b[0]+b[2] == initial[0]+initial[2] and b[1]+b[3] == initial[1]+initial[3] for b in boxes)
    return dict(expected_final=want, final_matches=want == final, x_direction=signs, held_pause_stable=fixed_geometry,
                held_pause_boxes=segment_end, held_pause_queries=pause_queries,
                fixed_size_or_opposite_corner=invariant, distinct_boxes=len(set(map(tuple, boxes))),
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
    kind = animated['kind']
    cases, failed = {}, []
    for label, case in [('native', native), ('animated', animated)]:
        events, resources = lifecycle(out, case)
        geom = geometry_checks(case)
        motion_end = next((event['mono_ns'] for event in reversed(events) if event['kind'].endswith('-end')),
                          case['release_ns'])
        video = video_metrics(out/label, case, background, native_endpoint, motion_end)
        ident = case['specimen']['id']
        paints = [row for line in (out/'gtk.log').read_text().splitlines() if line.startswith('{')
                  and (row := json.loads(line)).get('id') == ident and 'generation' in row]
        seeds = sorted({e['seed'] for e in events})
        duration = (events[-1]['mono_ns'] - events[0]['mono_ns']) * 1e-9 if len(events) >= 2 else None
        cases[label] = dict(geometry=geom, video=video, events=events, motion_lifetime_s=duration,
                            seeds=seeds, resources=resources, specimen_paints=paints)
        if not (geom['final_matches'] and geom['x_direction'] == [1, -1, 1] and geom['held_pause_stable']
                and geom['fixed_size_or_opposite_corner'] and geom['distinct_boxes'] > 10):
            failed.append(f'{label}: missing authoritative reversal, held pause, invariant or exact final box')
        old, new = (video['markers'].get(i) for i in (0, 1))
        if not (old and new and old['mono_ns'] < case['generation_request_ns'] <= new['mono_ns'] < case['release_ns']
                and video['max_retired_marker_after_new'] == 0):
            failed.append(f'{label}: marker did not visibly replace the old generation cleanly during the grab')
        if {paint['generation'] for paint in paints} != {0, 1}:
            failed.append(f'{label}: GTK did not paint both generations.')
        if (kind not in ('crane', 'pieces') and not
                (video['held_footprint_frames'] >= 2 and video['old_print_probe_pixels'] >= 100
                 and video['max_old_print_matches'] == 0 and video['max_distant_wallpaper_diff'] == 0)):
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
            if kind in ('carry', 'resize') and (len(video['active_worker_frames']) < 2
                                                or video['distinct_body_relative_amber_masks'] < 3):
                failed.append(f'{label}: no distinct worker frames separated from moving GTK')
        elif events or video['active_worker_frames']:
            failed.append(f'{label}: opt-out unexpectedly drew motion actors or admitted the motion model')
    if not (comparison['same_geometry'] and not comparison['full_endpoint_diff_pixels']):
        failed.append('Animated and native endpoint pixels or geometry differ')
    if not (comparison['vacated_pixels'] and not comparison['vacated_non_wallpaper_pixels']):
        failed.append('Empty or unhealed vacated old footprint')
    video = cases['animated']['video']
    if kind == 'crane':
        departure = video['departure']
        if (len(departure) <= 3 or not departure[-1]['touches_output_edge']
                or departure[-1]['outside_native_pixels'] >= .35*max(r['outside_native_pixels'] for r in departure)):
            failed.append('No shrinking edge-clipped rig remnant before the recorded native handoff.')
    if kind == 'pieces':
        native_motion = cases['native']['video']['retarget']
        native_jitter = max((max(map(abs, row['residual_xy'])) for row in native_motion), default=0)
        native_pair_jitter = max((max(map(abs, row['independent_residual_xy'])) for row in native_motion
                                  if row['independent_residual_xy']), default=0)
        cutoff = max(round(35 * scale), native_jitter + round(15 * scale))
        pair_cutoff = max(round(12 * scale), native_pair_jitter + round(8 * scale))
        displaced = {phase: [row for row in video['retarget'] if row['phase'] == phase
                            and max(map(abs, row['residual_xy'])) >= cutoff
                            and row['independent_residual_xy']
                            and max(map(abs, row['independent_residual_xy'])) >= pair_cutoff
                            and row['native_body_wallpaper_px'] > 100] for phase in ('reverse', 'redirect')}
        comparison.update(native_marker_max_residual_px=native_jitter, moving_marker_threshold_px=cutoff,
                          independent_marker_threshold_px=pair_cutoff, moving_marker_witnesses=displaced)
        if (not native_motion or any(len(rows) < 2 or rows[0]['marker'] == rows[-1]['marker']
                                    for rows in displaced.values()) or not any(r['generation'] for r in displaced['redirect'])):
            failed.append('No independently moving marks, changed generation and exposed wallpaper during both retarget legs.')
    result = dict(comparison=comparison, cases=cases, failed=failed)
    evidence.save(out/'result.json', result)
    return failed

def seam_stream(folder, reference, not_before, release=None):
    pts, times, stream = evidence.timing(folder)
    width, height = stream['width'], stream['height']
    require(times and reference.shape == (height, width, 3), f'No aligned output recording: {folder}')
    rows = []
    witnessed = False
    with decoder_for(folder) as decoder:
        try:
            for index, (stamp, pts_s) in enumerate(zip(times, pts)):
                frame = read_frame(decoder, folder, width, height)
                count = int(np.count_nonzero(np.any(frame != reference, axis=2)))
                rows.append(dict(index=index, mono_ns=stamp, pts_s=pts_s, diff_px=count))
                if not witnessed and stamp >= not_before:
                    Image.fromarray(frame).save(folder/'seam-witness.png')
                    witnessed = True
            require(decoder.stdout.read(1) == b'' and decoder.wait() == 0, f'Video decoder failed: {folder}')
        finally:
            if decoder.poll() is None:
                decoder.kill()
    evidence.save(folder/'seam-pixels.json', rows)
    eligible = [row for row in rows if row['mono_ns'] >= not_before]
    return dict(samples=len(eligible), max_diff_px=max((row['diff_px'] for row in eligible), default=None),
                held_frames=sum(row['mono_ns'] < release for row in eligible) if release is not None else None,
                end_diff_px=int(np.count_nonzero(np.any(pixels(folder/'endpoint.png') != reference, axis=2))),
                review_sha256=review_video(folder))


def analyze_seam(out, native, animated):
    kind = animated['kind']
    completed = out/'result.json'
    cases, failed = {}, []
    if kind == 'crane':
        failed = analyze(out, native, animated)
        cases = json.loads(completed.read_text())['cases']
        exit_history = cases['animated']['video']['departure']
        if not exit_history or exit_history[-1]['outside_native_box'][0] >= 3:
            failed.append('Crane did not reach the outer edge opposite the adjacent output.')
    for label, case in [('native', native), ('animated', animated)]:
        if kind == 'crane':
            neighbor = out/label/'neighbor'
            result = seam_stream(neighbor, pixels(out/'neighbor-background.png'), 0)
            cases[label]['neighbor'] = result
            if result['max_diff_px'] != 0 or result['end_diff_px'] != 0:
                failed.append(f'{label}: pixels appeared on the neighboring output despite fully owned crane move.')
            continue
        geom = geometry_checks(case)
        events, resources = lifecycle(out, case)
        seeds = sorted({event['seed'] for event in events})
        hold = next(p for p in case['phases'] if p['name'] == 'held-at-destination')
        cross = next((row['mono_ns'] for row in case['geometry'] if row['box'][0]+row['box'][2] > 1440), None)
        if not (geom['final_matches'] and geom['held_pause_stable'] and geom['x_direction'] == [1, -1, 1]
                and case['final'][0] < 1440 < case['final'][0]+case['final'][2] and cross):
            failed.append(f'{label}: authoritative held pointer did not cross and stay straddling the seam.')
        kinds = [event['kind'] for event in events]
        if label == 'animated' and (kinds != ['pieces-start', 'pieces-end'] or len(seeds) != 1 or not cross
                or not case['press_ns'] <= events[0]['mono_ns'] < cross
                or not cross-100_000_000 <= events[1]['mono_ns'] <= cross+120_000_000):
            failed.append(f'{label}: no observed piece-site lifetime cut at the drawable scope change.')
        if label == 'native' and kinds:
            failed.append('Native seam calibration unexpectedly ran an animation.')
        checks = {}
        for view, relative in [('owner', Path()), ('neighbor', Path('neighbor'))]:
            folder = out/label/relative
            native_reference = pixels(out/'native'/relative/'endpoint.png')
            bg = pixels(out/('background.png' if view == 'owner' else 'neighbor-background.png'))
            if not np.any(native_reference != bg):
                failed.append(f'{view}: paired native capture did not contain the straddling specimen.')
            good_from = hold['start_ns'] + 70_000_000
            result = seam_stream(folder, native_reference, good_from, case['release_ns'])
            if result['held_frames'] < 2 or result['max_diff_px'] != 0 or result['end_diff_px'] != 0:
                failed.append(f'{label} {view}: held seam and settled frames differ from paired native capture.')
            checks[view] = result
        cases[label] = dict(geometry=geom, events=events, seeds=seeds, resources=resources, native_pixels=checks)
    evidence.save(completed, dict(cases=cases, failed=failed))
    return failed



def close_video(folder, case, background, native, events):
    pts, times, stream = evidence.timing(folder)
    width, height = stream['width'], stream['height']
    require(times and background.shape == (height, width, 3), f'No aligned 1440x900 recording: {folder}')
    request, unmapped = (case[k] for k in ('close_request_ns', 'unmapped_ns'))
    begin = next((event['mono_ns'] for event in events if event['kind'] == 'regional-close-start'), request)
    duration = case.get('close_duration_s', .60)
    end = next((event['mono_ns'] for event in events if event['kind'] == 'regional-close-end'),
               begin + round(duration * 1e9))
    eligible = end + math.ceil(1e9/case['monitor']['refreshRate']) + 1_000_000
    x0, y0, x1, y1 = rect(case['initial'], case['monitor']['scale'])
    crop = np.s_[y0+3:y1-3, x0+3:x1-3]
    source, wallpaper = native[crop], background[crop]
    ink = print_ink(source)
    comparable = ~token_mask(source, CLOSE_TOKENS[0])
    token = tuple(case['token_rgb'])
    retired = CLOSE_TOKENS[:CLOSE_TOKENS.index(token)]
    fade_in = 'fade_in_seconds' in case
    frame_ns = math.ceil(1e9/case['monitor']['refreshRate'])
    if fade_in:
        # Estimate visible coverage in the same encoded RGB space for before/after;
        # high-contrast comparable channels dominate over RGB quantization.
        reference_delta = source.astype(np.int16) - wallpaper
        sampled = (np.abs(reference_delta) >= 64) & comparable[..., None]
        require(np.count_nonzero(sampled) > 1000, 'Insufficient native opacity calibration')
    ys = np.linspace(0, source.shape[0], 5, dtype=int)
    xs = np.linspace(0, source.shape[1], 5, dtype=int)
    # This measures independently remaining and failing print, not smoke or art.
    regions = [np.s_[ys[y]:ys[y+1], xs[x]:xs[x+1]] for y in range(4) for x in range(4)]
    ink_counts = [int(ink[region].sum()) for region in regions]
    require(min(ink_counts) >= 50, f'Insufficient native coordinate print: {ink_counts}')
    indices = {max(0, bisect.bisect_left(times, request)-1): ['before-destroy']}
    if case['interrupted']:
        # The client handles destroy asynchronously. The opening can publish
        # another frame after the controller request but before close is bound.
        reference_ns = min(begin, unmapped)
        index = max(0, bisect.bisect_left(times, reference_ns)-1)
        indices.setdefault(index, []).append('before-handoff')
    for label, stamp in [('first-after-unmap', unmapped), ('early', begin + int(duration*.20e9)),
                         ('middle', begin + int(duration*.45e9)), ('late', begin + int(duration*.72e9)),
                         ('after-end', eligible), ('settled', times[-1])]:
        index = min(len(times)-1, bisect.bisect_left(times, stamp))
        indices.setdefault(index, []).append(label)
    if fade_in:
        index = bisect.bisect_left(times, max(begin + frame_ns, unmapped))
        indices.setdefault(index, []).append('first-owned-opacity')
    trace, stills, settled = [], [], []
    best, previous, active_changes = None, None, 0
    pre_background = pre_token = None
    with decoder_for(folder) as decoder:
        try:
            for index, (stamp, video_time) in enumerate(zip(times, pts)):
                frame = read_frame(decoder, folder, width, height)
                view = frame[crop]
                same = np.all(view == source, axis=2)
                back = np.all(view == wallpaper, axis=2)
                current_mark = token_mask(frame, token)
                current_view_mark = current_mark[crop]
                printed = print_ink(view)
                count = int(np.count_nonzero(np.any(frame != background, axis=2)))
                surviving = same & ink
                retained = [round(float(surviving[region].sum()) / n, 5)
                            for region, n in zip(regions, ink_counts)]
                still, moved = max(range(16), key=retained.__getitem__), min(range(16), key=retained.__getitem__)
                row = dict(index=index, pts_s=video_time, mono_ns=stamp, since_close_ms=(stamp-begin)/1e6,
                           changed_from_wallpaper_px=count, native_identity=round(float(same[comparable].mean()), 5),
                           body_wallpaper=round(float(back.mean()), 5), local_ink_retained=retained,
                           moved_ink_px=int((printed & ~ink).sum()), current_token_px=int(current_mark.sum()),
                           retired_token_px=[int(token_mask(frame, color).sum()) for color in retired],
                           retained_region=still, lost_region=moved,
                           lost_region_wallpaper=round(float(back[regions[moved]].mean()), 5))
                if fade_in and request - 100_000_000 <= stamp < begin + 4*frame_ns:
                    numerator = view.astype(np.int16) - wallpaper
                    ratios = numerator[sampled] / reference_delta[sampled]
                    row['visible_opacity'] = np.percentile(ratios, (10, 50, 90)).round(5).tolist()
                trace.append(row)
                if unmapped <= stamp < end and count:
                    active_changes += int(previous is None or not np.array_equal(previous, frame))
                previous = frame
                if stamp >= eligible:
                    settled.append(row)
                if 'before-handoff' in indices.get(index, ()):
                    pre_background = back.copy()
                    pre_token = current_view_mark.copy()
                if pre_background is not None and stamp >= request:
                    row['preexisting_gap_print_fraction'] = round(
                        float((pre_background & printed).sum()) / max(1, int((pre_background & ink).sum())), 5)
                    row['same_position_token_fraction'] = round(
                        float((pre_token & current_view_mark).sum()) / max(1, int(pre_token.sum())), 5)
                for label in indices.get(index, []):
                    Image.fromarray(frame).save(folder/f'{label}.png')
                    stills.append((label, row))
                if (begin <= stamp < begin + .65*duration*1e9 and retained[still] >= .82
                        and retained[moved] <= .40 and row['moved_ink_px'] >= 64
                        and row['lost_region_wallpaper'] >= .12):
                    score = retained[still] - retained[moved]
                    if best is None or score > best[0]:
                        best = (score, row, frame.copy())
            require(decoder.stdout.read(1) == b'' and decoder.wait() == 0, f'Video decoder failed: {folder}')
        finally:
            if decoder.poll() is None:
                decoder.kill()
    if best:
        Image.fromarray(best[2]).save(folder/'regional-onset.png')
        stills.append(('regional-onset', best[1]))
    contact_sheet(folder, [(label, row['index'], row['since_close_ms']) for label, row in stills])
    before = next(row for label, row in stills if label == 'before-destroy')
    after = next(row for label, row in stills if label == 'first-after-unmap')
    evidence.save(folder/'pixel-timeline.json', trace)
    early = [r for r in trace if max(unmapped, begin) <= r['mono_ns'] < begin + 180_000_000]
    active = [r for r in trace if unmapped <= r['mono_ns'] < end]
    opacity = next((row for label, row in stills if label == 'first-owned-opacity'), None)
    return dict(before=before, after=after, onset=best[1] if best else None, after_unmap_changes=active_changes,
                token_frames_after_unmap=sum(r['current_token_px'] >= 20 for r in active),
                max_retired_token_px=max((max(r['retired_token_px'], default=0) for r in active), default=0),
                continuity_frames=early if case['interrupted'] else [],
                first_owned_opacity=opacity,
                maximum_close_native_identity=max((r['native_identity'] for r in trace if request <= r['mono_ns']),
                                                  default=None),
                settled_frames=len(settled), max_settled_diff=max((r['changed_from_wallpaper_px'] for r in settled),
                                                                default=None),
                endpoint_diff=int(np.count_nonzero(np.any(pixels(folder/'endpoint.png') != background, axis=2))),
                review_sha256=review_video(folder))


def analyze_close(out, takes):
    background = pixels(out/'background.png')
    native = pixels(out/'native'/'initial.png')
    results, failed = {}, []
    for case in takes:
        label = case['label']
        events, resources = lifecycle(out, case)
        close_events = [event for event in events if event['kind'].startswith('regional-close-')]
        seeds = sorted({event['seed'] for event in events})
        video = close_video(out/label, case, background, native, events)
        results[label] = dict(events=events, seeds=seeds, resources=resources, video=video)
        if not (video['settled_frames'] >= 2 and video['max_settled_diff'] == 0 and video['endpoint_diff'] == 0):
            failed.append(f'{label}: no clean, stable empty desktop after close')
        if label == 'native':
            if events or video['before']['native_identity'] < .98 or not video['after_unmap_changes']:
                failed.append('Native control did not show the ordinary close fade on the identical public specimen.')
            continue
        if (len(close_events) != 2 or [r['kind'] for r in close_events] !=
                ['regional-close-start', 'regional-close-end'] or close_events[0]['seed'] != close_events[1]['seed']
                or not case['close_request_ns'] <= close_events[0]['mono_ns'] < close_events[1]['mono_ns']
                or video['after_unmap_changes'] < 3):
            failed.append(f'{label}: no paired model lifetime and changing frames after actual client destroy')
        if 'fade_in_seconds' in case:
            before, after = video['before'], video['first_owned_opacity']
            first = before.get('visible_opacity', [0, 0, 0])[1]
            second = after.get('visible_opacity', [0, 0, 0])[1] if after else 0
            diagnostics = dict(before=first, after=second, product_signature=first*first,
                               observed_ratio=second/first if first else None)
            results[label]['alpha_comparison'] = diagnostics
            if (not .16 <= first <= .65 or not after or after['since_close_ms'] > 65
                    or abs(first-second) > .09 or second - first*first < .10):
                failed.append('Non-unit-alpha continuity was not observed in the aligned pre/owned samples.')
            continue
        if video['token_frames_after_unmap'] < 2 or video['max_retired_token_px'] >= 20:
            failed.append(f'{label}: unproven current-token provenance, or a retired specimen token appeared.')
        if case['interrupted']:
            opens = [event for event in events if event['kind'] == 'open']
            before = video['before']
            if (len(opens) != 1 or not case['open_request_ns'] <= opens[0]['mono_ns'] < case['close_request_ns']
                    or case['close_request_ns'] - before['mono_ns'] > 100_000_000
                    or not .10 <= before['body_wallpaper'] <= .95):
                # The last capture may be queued; use comparable ink and token geometry.
                failed.append('No timely partial opening frame before destroy.')
            continuations = video['continuity_frames']
            if (len(continuations) < 2 or video['maximum_close_native_identity'] is None
                    or video['maximum_close_native_identity'] >= .97
                    or max(r['preexisting_gap_print_fraction'] for r in continuations) >= .50
                    or max(r['same_position_token_fraction'] for r in continuations) < .45):
                failed.append('Interrupted close did not preserve the partial facade during the early handoff.')
        elif not video['onset'] or results['native']['video']['onset']:
            failed.append('No automated regional-onset candidate distinct from the paired native close; inspect the footage.')
    evidence.save(out/'result.json', dict(cases=results, failed=failed))
    return failed


def launch(args):
    require(args.take and re.fullmatch(r'take-[0-9]+', args.take), 'Use a fresh --take take-NN')
    build = HERE.parents[1]/'build'
    stock = build/'upstream-stock-948ac1ab'
    if args.installed_abi:
        trial = build/'construction-remaining-01'
        host_seal = trial/'installed-host.sha256'
        host_files = [Path(p) for p in ('/usr/bin/Hyprland', '/usr/bin/hyprctl', '/usr/lib/libaquamarine.so.0.15.1')]
        recorder = trial/'wf-recorder'
    else:
        trial = build/'construction-live-motion-01'
        host_seal = stock/'provenance-host.sha256'
        host_files = [stock/'obj/Hyprland', stock/'obj/hyprctl/hyprctl', stock/'prefix/lib/libaquamarine.so.0.15.1']
        recorder = stock/'capture-check/wf-recorder'
    plugin = trial/'obj/hyprimate.so'
    require(evidence.digest(plugin) == args.sealed_sha256, 'Artifact does not match the authorized seal')
    host_digests = {Path(path).name: digest for digest, path in
                    (line.split(maxsplit=1) for line in host_seal.read_text().splitlines() if line.strip())}
    recorder_digest = evidence.digest(recorder)
    for path in host_files:
        require(evidence.digest(path) == host_digests.get(path.name), f'Host seal mismatch: {path}')
    take = trial/args.take
    take.mkdir(mode=0o700)
    captures = [item for scale in (args.scale or []) for item in ('--scale', str(scale))]
    captures += [item for kind in (args.kind or []) for item in ('--kind', kind)]
    if args.installed_abi:
        captures.append('--installed-abi')
    if args.seam:
        captures.append('--seam')
    if args.close_alpha:
        captures.append('--close-alpha')
    sandbox = ['--unshare-all', '--as-pid-1', '--new-session', '--die-with-parent',
               '--ro-bind', '/usr', '/usr', '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib', '/lib64',
               '--symlink', 'usr/bin', '/bin', '--dev', '/dev', '--dir', '/dev/dri',
               '--ro-bind', '/sys', '/sys', '--proc', '/proc', '--tmpfs', '/run', '--tmpfs', '/tmp',
               '--ro-bind', '/run/udev/data', '/run/udev/data',
               '--perms', '0700', '--dir', '/tmp/home', '--perms', '0700', '--dir', '/tmp/runtime',
               '--dir', '/opt/bin',
               '--ro-bind', str(host_files[0]), '/opt/bin/Hyprland',
               '--ro-bind', str(host_files[1]), '/opt/bin/hyprctl',
               '--ro-bind', str(plugin), str(PLUGIN),
               '--ro-bind', str(recorder), '/opt/bin/wf-recorder',
               '--bind', str(take), str(OUTPUT), '--chdir', '/tmp']
    if not args.installed_abi:
        sandbox.extend(('--ro-bind', str(stock/'prefix/lib'), '/opt/lib'))
    for name in ('fonts', 'dbus-1', 'passwd', 'group', 'machine-id', 'nsswitch.conf', 'ld.so.cache', 'localtime'):
        path = '/etc/'+name
        sandbox.extend(('--ro-bind-try', path, path))
    for name in ('dri/renderD128', 'dri/renderD129', 'nvidiactl', 'nvidia0', 'nvidia1', 'nvidia-uvm'):
        path = '/dev/'+name
        sandbox.extend(('--dev-bind-try', path, path))
    nested = Path('/opt/slice/tests/nested')
    snapshot = take/'fixture'
    snapshot.mkdir(mode=0o700)
    fixture_digests = {}
    for name in (Path(__file__).name, 'construction_evidence.py', 'burn_evidence.py', 'paper_scene.py'):
        path = snapshot/name
        shutil.copyfile(HERE/name, path)
        fixture_digests[name] = evidence.digest(path)
        sandbox.extend(('--ro-bind', str(path), str(nested/name)))
    env = dict(PATH='/opt/bin:/usr/bin', LANG='C.UTF-8',
               KWIN_RENDER_NODES='/dev/dri/renderD128', GBM_BACKEND='nvidia-drm',
               HOME='/tmp/home', XDG_RUNTIME_DIR='/tmp/runtime', XDG_SESSION_TYPE='wayland',
               GDK_BACKEND='wayland', GSETTINGS_BACKEND='memory', NO_AT_BRIDGE='1', QT_ACCESSIBILITY='0')
    if not args.installed_abi:
        env['LD_LIBRARY_PATH'] = '/opt/lib'
    for key, value in env.items():
        sandbox.extend(('--setenv', key, value))
    command = ['/usr/bin/env', '-i', '/usr/bin/bwrap', *sandbox, '--', '/usr/bin/python', '-B',
               str(nested/Path(__file__).name), '--inside', '--sealed-sha256', args.sealed_sha256, *captures]
    source_commit = trial/'source-commit.txt'
    evidence.save(take/'launch.json', dict(command=command, installed_abi=args.installed_abi,
        host_sha256=host_digests, expected_revision=INSTALLED_HOST if args.installed_abi else STOCK_HOST,
        plugin_sha256=args.sealed_sha256, recorder_sha256=recorder_digest, fixture_sha256=fixture_digests,
        source_commit=source_commit.read_text().strip() if source_commit.is_file() else None))
    os.execv(command[0], command)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sealed-sha256', required=True)
    parser.add_argument('--take', help='fresh take-NN in the selected build directory')
    parser.add_argument('--installed-abi', action='store_true', help='use sealed build/construction-remaining-01')
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--scale', type=float, choices=(1., 1.5), action='append')
    parser.add_argument('--kind', choices=(*PATHS, 'regional-close'), action='append')
    parser.add_argument('--seam', action='store_true', help='paired crane and cross-output pieces on two adjacent 1x outputs')
    parser.add_argument('--close-alpha', action='store_true', help='separate close during a non-unit native fade-in')
    args = parser.parse_args()
    require(re.fullmatch('[a-f0-9]{64}', args.sealed_sha256), 'Invalid sealed SHA-256')
    if args.seam and (not args.installed_abi or any(scale != 1. for scale in (args.scale or ()))
                      or any(kind not in ('crane', 'pieces') for kind in (args.kind or ()))):
        parser.error('--seam requires --installed-abi, scale 1 and crane/pieces only')
    if args.close_alpha and (not args.installed_abi or args.seam or
                             any(kind != 'regional-close' for kind in (args.kind or ()))):
        parser.error('--close-alpha requires --installed-abi and regional-close only')
    if not args.inside:
        launch(args)
    require(os.environ.get('HOME') == '/tmp/home' and os.environ.get('XDG_RUNTIME_DIR') == '/tmp/runtime'
            and not Path('/run/user').exists() and os.getpid() == 1
            and not any(key in os.environ for key in ('WAYLAND_DISPLAY', 'DISPLAY', 'WAYLAND_SOCKET',
                                                      'DBUS_SESSION_BUS_ADDRESS', 'HYPRLAND_INSTANCE_SIGNATURE')),
            'Refusing capture outside the private bwrap launcher')
    require(PLUGIN.is_file() and OUTPUT.is_dir(), 'Invalid sandbox artifact or output')
    summary = []
    scales = args.scale or ((1.,) if args.seam or args.close_alpha else (1., 1.5))
    default_kinds = ('crane', 'pieces') if args.seam else ('regional-close',) if args.close_alpha else (
        REMAINING if args.installed_abi else ('carry', 'resize'))
    kinds = args.kind or default_kinds
    for scale in scales:
        for kind in kinds:
            out = OUTPUT/('seam' if args.seam else f'scale-{scale:g}')/kind
            out.mkdir(parents=True)
            try:
                with tempfile.TemporaryDirectory(prefix='cm.', dir='/tmp/runtime') as runtime:
                    session = MotionSession(Path(runtime), out, PLUGIN, '/opt/bin/wf-recorder')
                    try:
                        session.boot(scale, kind, args.sealed_sha256, args.installed_abi, args.seam)
                        if args.seam:
                            session.setup_seam()
                        if kind == 'regional-close':
                            native = session.close_take('native', 20)
                            if args.close_alpha:
                                closes = (native, session.close_take('alpha', 21, fade_in=True))
                            else:
                                closes = (native, session.close_take('animated', 21),
                                          session.close_take('interrupted', 22, interrupted=True))
                        else:
                            native = session.take(kind, 'native', 20)
                            animated = session.take(kind, 'animated', 21)
                    finally:
                        session.cleanup()
                if kind == 'regional-close':
                    failures = analyze_close(out, closes)
                elif args.seam:
                    failures = analyze_seam(out, native, animated)
                else:
                    failures = analyze(out, native, animated)
            except (OSError, RuntimeError, ValueError, sp.SubprocessError) as exc:
                failures = [f'Evidence unavailable: {exc}']
                evidence.save(out/'unavailable.json', failures)
            result = out/'result.json'
            cases = json.loads(result.read_text())['cases'] if result.is_file() else {}
            summary.append(dict(scale=scale, kind=kind, failed=failures,
                                seeds={label: case['seeds'] for label, case in cases.items()}))
            evidence.save(OUTPUT/'summary.json', summary)
    print(json.dumps(summary, indent=2))
    return int(any(case['failed'] for case in summary))


if __name__ == '__main__':
    sys.exit(main())
