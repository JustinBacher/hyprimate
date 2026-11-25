#!/usr/bin/env python3
"""Collect compute evidence in disposable KWin/Hyprland sessions.

Requires the private-session dependencies of construction_evidence.py.  No host
compositor configuration or socket is used.  A nonzero exit means the evidence
is incomplete or failed; retained images and logs are part of the result.
"""
import argparse
import ctypes
import fcntl
import json
import math
import os
from pathlib import Path
import re
import signal
import shutil
import statistics
import subprocess as sp
import sys
import tempfile
import time

from PIL import Image, ImageChops, ImageStat

import construction_evidence as construction
from burn_evidence import ROOT, until
from shader_evidence import client_box, settings

FIXTURES = ROOT / 'examples/shaders'
RENDERER = 'NVIDIA GeForce RTX 5070 Ti'
PLUGIN_UNLOADED = re.compile(r'\bPlugin\s+hyprimate\s+unloaded\b', re.IGNORECASE)


_clock_getcpuclockid = ctypes.CDLL(None).clock_getcpuclockid
_clock_getcpuclockid.argtypes = (ctypes.c_int, ctypes.POINTER(ctypes.c_int))
_clock_getcpuclockid.restype = ctypes.c_int


def save(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def difference(a, b, box=None):
    delta = ImageChops.difference(a, b)
    if box:
        delta = delta.crop(box)
    return sum(ImageStat.Stat(delta).mean) / 3


def maximum(a, b, box=None):
    delta = ImageChops.difference(a, b)
    if box:
        delta = delta.crop(box)
    return max(high for _, high in delta.getextrema())

def marker_metrics(image, box, expected):
    """Check the authored marker patch using top-first compositor window UVs."""
    x0, y0, x1, y1 = box
    width, height = x1 - x0, y1 - y0
    patch = (x0 + round(.085 * width), y0 + round(.085 * height),
             x0 + round(.165 * width), y0 + round(.14 * height))
    mean = ImageStat.Stat(image.crop(patch)).mean
    dominant = max(range(3), key=lambda i: expected[i])
    bias = mean[dominant] - max(mean[j] for j in range(3) if j != dominant)
    error = sum(abs(mean[i] - expected[i]) for i in range(3)) / 3
    return dict(box=patch, mean_rgb=mean, chroma_bias=bias, mean_absolute_error=error)


def check_receipts(out, requested_route, *, benchmark=False, cpu=False):
    path = out / 'private-hyprland.log'
    if not path.exists():
        raise RuntimeError('private compositor log missing; cannot verify compute receipts')
    text = path.read_text(errors='replace')
    gl_errors = [line for line in text.splitlines()
                 if 'GL_INVALID_' in line or 'OpenGL error' in line]
    if gl_errors:
        raise RuntimeError(('OpenGL errors in private compute session', gl_errors))
    prepare = re.findall(r'\[hyprimate\] compute generation prepare ([^\n]+)', text)
    generation_retire = re.findall(r'\[hyprimate\] compute generation retire ([^\n]+)', text)
    instances = re.findall(r'\[hyprimate\] compute instance retire ([^\n]+)', text)
    if not prepare or not generation_retire or not instances:
        raise RuntimeError(('missing compute lifecycle receipts', len(prepare),
                            len(generation_retire), len(instances)))
    def parse(row):
        return dict(re.findall(r'([a-z_]+)=([^ ]+)', row))
    preparations = [parse(row) for row in prepare]
    retirements = [parse(row) for row in generation_retire]
    instance_rows = [parse(row) for row in instances]
    if len(preparations) != len(retirements):
        raise RuntimeError(('generation prepare/retire receipt count mismatch',
                            len(preparations), len(retirements)))
    for prep in preparations:
        if prep.get('backend') != requested_route:
            raise RuntimeError(('backend preference mismatch', requested_route, prep))
        if prep.get('reused') not in ('false', '0'):
            raise RuntimeError(('expected a freshly prepared compute generation', prep))
    numeric = ('compiles', 'allocations', 'native', 'slibs', 'samples', 'barriers',
               'live_bytes', 'idle_bytes')
    if any(key not in row or not row[key].isdigit() for row in retirements for key in numeric):
        raise RuntimeError(('incomplete generation retirement receipt', retirements))
    totals = {key: sum(int(row[key]) for row in retirements) for key in numeric}
    if not cpu:
        if requested_route in ('native', 'slibs'):
            selected = totals[requested_route]
            other = 'slibs' if requested_route == 'native' else 'native'
            if selected == 0 or totals[other] != 0:
                raise RuntimeError(('strict backend receipt does not match requested route', requested_route, totals))
        if requested_route == 'auto' and totals['native'] + totals['slibs'] == 0:
            raise RuntimeError(('auto route did not dispatch any compute kernels', totals))
    ids = [row.get('instance_id') for row in instance_rows]
    if any(not value or not value.isdigit() for value in ids) or len(set(ids)) != len(ids):
        raise RuntimeError(('instance receipts need unique monotonic IDs', ids))
    required = ('kind', 'compiles', 'allocations', 'native', 'slibs', 'samples',
                'barriers', 'upload_bytes', 'copy_bytes', 'readback_bytes',
                'live_bytes', 'idle_bytes')
    if any(any(key not in row or not row[key].isdigit() for key in required if key != 'kind')
           or not row.get('kind') for row in instance_rows):
        raise RuntimeError(('incomplete instance retirement receipt', instance_rows))
    if cpu:
        adapter = re.findall(r'\[hyprimate\] compute benchmark cpu instance_id=(\d+) iterations=(\d+)', text)
        retired_ids = {row.get('instance_id') for row in instance_rows}
        if not adapter or any(int(iterations) <= 0 or instance_id not in retired_ids
                              for instance_id, iterations in adapter):
            raise RuntimeError(('CPU adapter receipt missing or unmatched', adapter, retired_ids))
    if benchmark and not prepare:
        raise RuntimeError('benchmark run has no compute prepare receipt')
    receipts = dict(requested_backend_preference=requested_route,
                    observed_dispatches={key: totals[key] for key in ('native', 'slibs')},
                    prepare=preparations, generation_retire=retirements,
                    instance_retire=instance_rows)
    save(out / 'compute-receipts.json', receipts)
    return receipts
def check_no_compute_receipts(out):
    path = out / 'private-hyprland.log'
    text = path.read_text(errors='replace') if path.exists() else ''
    if re.search(r'\[hyprimate\] compute (?:generation|instance) ', text):
        raise RuntimeError('unused-compute control performed compute setup or instance work')

def process_cpu_time_ns(pid):
    clock_id = ctypes.c_int()
    error = _clock_getcpuclockid(pid, ctypes.byref(clock_id))
    if error:
        raise OSError(error, os.strerror(error), pid)
    return time.clock_gettime_ns(clock_id.value)


def snapshot_process(pid):
    # The command in /proc/PID/stat may contain spaces or parentheses.
    stat = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    memory = {}
    for line in Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines():
        match = re.match(r'^(Rss|Pss):\s+(\d+)\s+kB$', line)
        if match:
            memory[match[1].lower() + '_bytes'] = int(match[2]) * 1024
    if set(memory) != {'rss_bytes', 'pss_bytes'}:
        raise RuntimeError('smaps_rollup is missing RSS or PSS')
    return dict(monotonic_ns=time.monotonic_ns(), cpu_time_ns=process_cpu_time_ns(pid),
                user_ticks=int(stat[11]), system_ticks=int(stat[12]), **memory)


def percentiles(values):
    values = sorted(values)
    if not values:
        return None
    return {f'p{p}': values[(len(values) - 1) * p // 100] for p in (50, 95, 99)}


def compare(left, right):
    """Two-sided tie-corrected normal Mann–Whitney and rank-biserial effect."""
    if not left or not right:
        raise ValueError('empty comparison')
    pooled = sorted([(v, 0) for v in left] + [(v, 1) for v in right])
    ranks = [0.0, 0.0]
    ties = 0
    i = 0
    while i < len(pooled):
        j = i + 1
        while j < len(pooled) and pooled[j][0] == pooled[i][0]:
            j += 1
        rank = (i + 1 + j) / 2
        for _, group in pooled[i:j]:
            ranks[group] += rank
        t = j - i
        ties += t**3 - t
        i = j
    n, m = len(left), len(right)
    u = ranks[0] - n * (n + 1) / 2
    total = n + m
    variance = n * m / 12 * ((total + 1) - ties / (total * (total - 1)))
    z = max(0, abs(u - n * m / 2) - .5) / math.sqrt(variance) if variance else 0
    p = math.erfc(z / math.sqrt(2)) if variance else 1.0
    return dict(u=u, p_two_sided=p, alpha=.01, rank_biserial=2*u/(n*m)-1,
                median_ratio=statistics.median(left) / statistics.median(right)
                if statistics.median(right) else None,
                note='Normal approximation with tie and continuity corrections; not an equivalence test.')


def matched_interval(left, right):
    ratios = sorted(a / b for a, b in zip(left, right) if b > 0)
    if len(ratios) != len(left) or len(left) != len(right) or not ratios:
        return None
    n = len(ratios)
    k = next((j for j in range(1, n // 2 + 2)
              if 1 - 2 * sum(math.comb(n, i) for i in range(j)) / 2**n >= .95), None)
    if k is None:
        return None
    low, high = ratios[k - 1], ratios[n - k]
    coverage = 1 - 2 * sum(math.comb(n, i) for i in range(k)) / 2**n
    return dict(median=statistics.median(ratios), low=low, high=high,
                coverage_at_least=coverage,
                margin_3_percent=('within' if low >= .97 and high <= 1.03 else
                                  'outside' if high < .97 or low > 1.03 else 'inconclusive'))


class Session(construction.Session):
    def __init__(self, runtime, out, plugin, node, route, *, cpu=False, es30=False, cpu_affinity=None):
        super().__init__(runtime, out, plugin, None)
        self.route, self.cpu, self.es30 = route, cpu, es30
        self.cpu_affinity = cpu_affinity
        self.env['KWIN_RENDER_NODES'] = str(node)
        for key in ('HYPRIMATE_COMPUTE_BACKEND', 'HYPRIMATE_COMPUTE_BENCH_CPU',
                    'MESA_GLES_VERSION_OVERRIDE', 'LIBGL_ALWAYS_SOFTWARE',
                    '__EGL_VENDOR_LIBRARY_FILENAMES', 'LD_PRELOAD', 'LD_LIBRARY_PATH'):
            self.env.pop(key, None)
        self.node = node
        self.plugin_loaded = False
        self.expect_compute = False
        self.unload_requested = False
        self.unload_live_snapshot = None

    def start(self, log, argv, *, stdin=None, extra=None):
        extra = dict(extra or {})
        if Path(str(argv[0])).name == 'Hyprland':
            extra['HYPRIMATE_COMPUTE_BACKEND'] = self.route
            extra['AQ_WAYLAND_OUTPUTS'] = '1'
            if self.cpu:
                extra['HYPRIMATE_COMPUTE_BENCH_CPU'] = '1'
            if self.es30:
                extra.update(MESA_GLES_VERSION_OVERRIDE='3.0', LIBGL_ALWAYS_SOFTWARE='true',
                             __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json')
            self.child_environment = self.env | extra
            if self.cpu_affinity is not None:
                argv = ['taskset', '--cpu-list', self.cpu_affinity, *argv]
        return super().start(log, argv, stdin=stdin, extra=extra)

    def boot(self):
        instance = super().boot()
        self.plugin_loaded = True
        save(self.out / 'cpu-affinity.json', dict(
            requested=self.cpu_affinity, actual=sorted(os.sched_getaffinity(self.hypr.pid))))
        self.hc('eval', 'hl.monitor({output=' + json.dumps(self.output) +
                ',mode="1280x960@60",position="0x0",scale=1})')
        monitor = until(lambda: (m := self.monitor()) and m['width'] == 1280 and
                        m['height'] == 960 and m['scale'] == 1 and
                        abs(m.get('refreshRate', 0) - 60) <= .1 and m, self.hypr)
        save(self.out / 'monitor.json', monitor)
        self.hc('eval', 'hl.window_rule({name="compute-protected",match={title="^PaperEvidence-protected$"},no_screen_share=true,no_blur=true})')
        save(self.out / 'session-environment.json', {
            key: self.child_environment.get(key) for key in (
                'KWIN_RENDER_NODES', 'HYPRIMATE_COMPUTE_BACKEND',
                'HYPRIMATE_COMPUTE_BENCH_CPU', 'MESA_GLES_VERSION_OVERRIDE',
                'LIBGL_ALWAYS_SOFTWARE', '__EGL_VENDOR_LIBRARY_FILENAMES',
                'WAYLAND_DISPLAY', 'AQ_BACKENDS', 'AQ_WAYLAND_OUTPUTS')})
        return instance

    def unload_plugin(self):
        if self.plugin_loaded:
            text = self.instance_log.read_text(errors='replace')
            prepared = len(re.findall(r'\[hyprimate\] compute generation prepare\b', text))
            self.hc('plugin', 'unload', str(self.plugin))
            self.plugin_loaded = False
            self.unload_requested = True

            current = self.instance_log.read_text(errors='replace')
            self.unload_live_snapshot = dict(
                prepared_generations=len(re.findall(
                    r'\[hyprimate\] compute generation prepare\b', current)),
                retired_generations=len(re.findall(
                    r'\[hyprimate\] compute generation retire\b', current)),
                plugin_unloaded=bool(PLUGIN_UNLOADED.search(current)))

    def cleanup(self):
        try:
            if self.plugin_loaded and self.hypr.poll() is None:
                self.unload_plugin()
        finally:
            try:
                super().cleanup()
            finally:
                if hasattr(self, 'instance_log') and self.instance_log.exists():
                    shutil.copyfile(self.instance_log, self.out/'private-hyprland.log')
        if self.unload_requested:
            text = (self.out/'private-hyprland.log').read_text(errors='replace')
            prepared = len(re.findall(r'\[hyprimate\] compute generation prepare\b', text))
            retired = len(re.findall(r'\[hyprimate\] compute generation retire\b', text))
            unloaded = bool(PLUGIN_UNLOADED.search(text))
            if self.unload_requested and not unloaded:
                raise RuntimeError('plugin unload was acknowledged but final log lacks plugin-unloaded receipt')
            if self.expect_compute and (prepared == 0 or retired < prepared):
                raise RuntimeError(('final log lacks compute generation retirement receipts', prepared, retired))

    def verify_device(self, hardware=True):
        log = self.instance_log.read_text(errors='replace')
        device = [line for line in log.splitlines() if re.search(r'Got node|eglDeviceFromDRMFD|\b(?:Vendor|Renderer|GL version):', line)]
        gpu = sp.run(['nvidia-smi', 'pmon', '-c', '1'], text=True, capture_output=True)
        result = dict(requested_node=str(self.node), lines=device, hardware=hardware,
                      nvidia_smi_processes=gpu.stdout + gpu.stderr)
        if hardware and (gpu.returncode or not re.search(
                rf'^\s*\d+\s+{self.hypr.pid}\s+', gpu.stdout, re.MULTILINE)):
            raise RuntimeError(('nested compositor PID not attributed to an NVIDIA GPU', result))
        save(self.out / 'device.json', result)
        if hardware:
            if not any(f'Got node {self.node}' in line for line in device):
                raise RuntimeError(('nested DMA-BUF node not verified', result))
            if not any('Renderer:' in line and RENDERER in line for line in device):
                raise RuntimeError(('nested hardware renderer not verified', result))
        elif not any('Renderer:' in line and ('llvmpipe' in line.lower() or 'softpipe' in line.lower()) for line in device):
            raise RuntimeError(('constrained software renderer not verified', result))
        return result

    def snap(self, name):
        path = self.out / (name + '.png')
        sp.run(['grim', '-l', '1', '-o', self.output, path], env=self.env, check=True, timeout=10)
        with Image.open(path) as image:
            return image.convert('RGB')

    def effect(self, path, duration=12000):
        choice = ('{enabled=true,effect=' + json.dumps(str(path)) +
                  ',effect_duration=' + str(duration) + '}')
        self.hc('eval', ('hl.config({plugin={hyprimate={window={enabled=true,style="liquid",' +
                         'movement={enabled=false},open=' + choice + ',close=' + choice + '}}}})'))
        errors = self.hc('configerrors')
        if errors:
            raise RuntimeError(('configuration errors', errors))
        checks = {
            'plugin:hyprimate:window:enabled': True,
            'plugin:hyprimate:window:open:enabled': True,
            'plugin:hyprimate:window:close:enabled': True,
            'plugin:hyprimate:window:open:effect': str(path),
            'plugin:hyprimate:window:close:effect': str(path),
            'plugin:hyprimate:window:open:effect_duration': duration,
            'plugin:hyprimate:window:close:effect_duration': duration,
        }
        for option, expected in checks.items():
            value = json.loads(self.hc('getoption', '-j', option))
            actual = value.get('str') if isinstance(expected, str) else (
                value.get('bool') if isinstance(expected, bool) else
                value.get('int') if 'int' in value else value.get('float'))
            if actual != expected:
                raise RuntimeError(('effect configuration did not stick', option, expected, value))
        self.hc('dismissnotify', '-1')
    def open(self, ident, **options):
        self.send('open', ident, width=440, height=300, construction='light', **options)
        return until(lambda: self.lookup(ident), self.scene)

    def close(self, ident):
        self.send('close', ident)
        until(lambda: not self.lookup(ident), self.scene)

    def move(self, item, x, y):
        self.hc('eval', 'assert(hl.dispatch(hl.dsp.window.move({x=' + str(x) + ',y=' + str(y) +
                ',relative=false,window="address:' + item['address'] + '"})).ok)')


def session_run(out, plugin, node, route, fn, *, cpu=False, es30=False, compute=False, cpu_affinity=None):
    out.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix='hc.', dir='/tmp') as temp:
        session = Session(Path(temp), out, plugin, node, route, cpu=cpu, es30=es30,
                          cpu_affinity=cpu_affinity)
        session.expect_compute = compute
        try:
            session.boot()
            device = session.verify_device(hardware=not es30)
            result = fn(session)
            result['device'] = device
        except BaseException:
            try:
                session.cleanup()
            except Exception as cleanup_error:
                save(out / 'cleanup-failure.json', dict(error=str(cleanup_error)))
            raise
        else:
            session.cleanup()
    if compute:
        result['receipts'] = check_receipts(out, route, benchmark=cpu, cpu=cpu)
    else:
        check_no_compute_receipts(out)
    return result


def correctness(session, fixture):
    def require_marker(frame, item, rgb):
        x, y = item['at']
        width, height = item['size']
        observed = marker_metrics(frame, (x, y, x + width, y + height), rgb)
        if observed['chroma_bias'] < 45 or observed['mean_absolute_error'] > 45:
            raise RuntimeError(('fixture presentation marker missing or wrong color', rgb, observed))
        return observed
    metrics = {'fixture': str(fixture), 'route_requested': session.route}
    session.effect(fixture, 3500)
    time.sleep(.7)
    background = session.snap('background')
    item = session.open('main')
    box = client_box(item)
    time.sleep(.35)
    first = session.snap('opening-first')
    time.sleep(.6)
    expected_marker = (20, 235, 168) if 'stencil' in str(fixture) else (242, 77, 191)
    marker = require_marker(first, item, expected_marker)
    second = session.snap('opening-second')
    metrics['interior_change'] = difference(first, second, box)
    metrics['outside_error'] = maximum(background, second, (10, 700, 180, 900))
    if metrics['interior_change'] < .1 or metrics['outside_error'] > 2:
        raise RuntimeError(('animation progression or exterior failed', metrics))
    time.sleep(3.2)
    settled = session.snap('native-handoff')
    if difference(background, settled, box) < 10:
        raise RuntimeError('mapped native client missing after handoff')
    session.close('main')
    time.sleep(.35)
    session.snap('closing')
    time.sleep(3.5)
    if maximum(background, session.snap('retired')) > 2:
        raise RuntimeError('close did not retire to native background')
    # Stencil also has a wallpaper-scoped pass; verify the visible output marker.
    if 'stencil' in str(fixture):
        session.hc('eval', 'hl.config({plugin={hyprimate={wallpaper={effect=' +
                   json.dumps(str(fixture)) + ',effect_duration=0}}}})')
        errors = session.hc('configerrors')
        if errors:
            raise RuntimeError(('wallpaper stencil config errors', errors))
        time.sleep(.5)
        wallpaper = session.snap('wallpaper-stencil')
        wallpaper_marker = marker_metrics(wallpaper, (0, 0, 1280, 960), expected_marker)
        metrics['wallpaper_marker'] = wallpaper_marker
        if wallpaper_marker['chroma_bias'] < 45 or wallpaper_marker['mean_absolute_error'] > 45:
            raise RuntimeError(('wallpaper stencil marker does not match its authored color', wallpaper_marker))

    # Real overlap, reload failure and interruption, with pinned active generation.
    session.effect(fixture, 6000)
    session.open('overlap-a')
    second_item = session.open('overlap-b')
    session.move(second_item, 350, 170)
    def moved_overlap():
        item = session.lookup('overlap-b')
        return item if item and item['at'] == [350, 170] else None
    second_item = until(moved_overlap, session.scene)
    session.snap('overlap-before-invalid-reload')
    try:
        session.effect(session.runtime / 'missing-compute-effect.json', 6000)
    except RuntimeError:
        pass  # Expected preparation rejection; verify that the prior generation remains active.
    time.sleep(.35)
    session.snap('overlap-after-invalid-reload')
    metrics['invalid_reload_pinned_marker'] = require_marker(
        session.snap('overlap-after-invalid-reload-check'), second_item, expected_marker)
    session.close('overlap-b')
    session.close('overlap-a')
    time.sleep(6.3)  # Let both 6 s close generations retire before the next lifecycle case.

    session.effect(fixture, 6000)
    private = session.open('protected')
    private_box = client_box(private)
    rule = session.hc('getprop', 'address:' + private['address'], 'no_screen_share')
    if rule != 'true':
        raise RuntimeError(('protected rule not applied', rule))
    time.sleep(.4)
    protected = session.snap('protected-capture')
    if sum(ImageStat.Stat(protected.crop(private_box)).mean) / 3 > 140:
        raise RuntimeError('protected window visible in screencopy')
    session.close('protected')
    time.sleep(6.3)  # Do not let the protected window's close animation contaminate resize evidence.
    session.effect(fixture, 6000)
    resized = session.open('resize')
    address = 'address:' + resized['address']
    session.hc('eval', 'assert(hl.dispatch(hl.dsp.window.resize({x=520,y=345,relative=false,window="' +
               address + '"})).ok)')
    resized = until(lambda: (item := session.lookup('resize')) and item['size'] == [520, 345] and item,
                    session.hypr)
    session.snap('resized-during-open-unasserted')
    time.sleep(6.3)  # Wait out the fixture's declared 6 s open duration before requiring native pixels.
    def settled_native_resize():
        frame = session.snap('resized-instance')
        current = session.lookup('resize')
        if not current:
            return None
        x, y = current['at']
        width, height = current['size']
        background_rgb = frame.getpixel((x + 78, y + 38))
        glyph_rgb = frame.getpixel((x + int(.76 * width) + 4, y + int(.73 * height) + 5))
        save(session.out / 'resized-native-probe.json',
             dict(initial_geometry=resized, current_geometry=current,
                  background_rgb=background_rgb, glyph_rgb=glyph_rgb))
        expected_background = (240, 232, 207)
        expected_glyph = (18, 244, 173)
        close = lambda actual, expected: max(abs(a - b) for a, b in zip(actual, expected)) <= 2
        if close(background_rgb, expected_background) and close(glyph_rgb, expected_glyph):
            return dict(background=background_rgb, glyph=glyph_rgb)
        return None
    metrics['resized_native_source'] = until(settled_native_resize, session.hypr)
    if session.hc('output', 'create', 'wayland', 'COMPUTE-SECONDARY') != 'ok':
        raise RuntimeError('could not create private compute lifecycle output')
    def secondary():
        return next((m for m in json.loads(session.hc('-j', 'monitors'))
                     if m['name'] == 'COMPUTE-SECONDARY'), None)
    session.hc('eval', 'hl.monitor({output="COMPUTE-SECONDARY",mode="640x480@60",position="1280x0",scale=1})')
    until(lambda: (m := secondary()) and m['width'] == 640, session.hypr)
    sp.run(['grim', '-l', '1', '-o', 'COMPUTE-SECONDARY',
            session.out / 'secondary-resized-output.png'], env=session.env, check=True, timeout=10)
    if session.hc('output', 'remove', 'COMPUTE-SECONDARY') != 'ok':
        raise RuntimeError('private compute lifecycle output did not remove')
    until(lambda: not secondary(), session.hypr)
    session.close('resize')


    time.sleep(6.3)  # Retire the resize case before testing active-plugin unload.
    active = session.open('unload')
    time.sleep(.3)
    session.snap('before-active-unload')
    session.unload_plugin()
    time.sleep(.5)
    after = session.snap('after-active-unload')
    if difference(background, after, client_box(active)) < 10:
        raise RuntimeError('native rendering did not resume after unload')
    session.close('unload')
    metrics['marker'] = marker
    metrics['observations'] = ['map', 'two-instance overlap', 'invalid reload', 'interrupt/close',
                               'protected screencopy', 'active unload', 'native handoff']
    return metrics


def quiet_environment(out):
    ps = sp.run(['ps', '-eo', 'pid,ppid,args'], text=True, capture_output=True, check=True).stdout
    gpu = sp.run(['nvidia-smi', 'pmon', '-c', '1'], text=True, capture_output=True)
    usage = sp.run(['nvidia-smi', '--query-gpu=index,utilization.gpu,clocks.gr,clocks.mem,power.draw,temperature.gpu',
                    '--format=csv,noheader'], text=True, capture_output=True)
    (out / 'processes.txt').write_text(ps)
    (out / 'gpu-processes.txt').write_text(gpu.stdout + gpu.stderr)
    (out / 'gpu-environment.txt').write_text(usage.stdout + usage.stderr)
    if gpu.returncode:
        raise RuntimeError('nvidia-smi pmon unavailable; cannot attribute the nested renderer')
    competitors = [line for line in ps.splitlines()
                   if re.search(r'\b(?:cc1plus|ninja|clang\+\+)\b', line)]
    if competitors:
        raise RuntimeError(('timing lane occupied by compiler processes', competitors))
    if any(re.search(r'\b(?:compute-bench|glmark2|gpu-burn)\b', line) for line in ps.splitlines()):
        raise RuntimeError('timing lane occupied by a GPU benchmark')
def first_changed_latency(session, workload):
    folder = session.out / 'latency'
    folder.mkdir()
    try:
        recorder = construction.recorder_path(None)
    except RuntimeError as error:
        return dict(value_ms=None, unavailable=str(error))
    log_path = folder / 'recorder.log'
    with log_path.open('w') as log:
        process = sp.Popen(
            [str(recorder), '-y', '-D', '-o', session.output, '-c', 'libx264rgb',
             '-p', 'crf=0', '-p', 'preset=ultrafast', '-p', 'threads=2',
             '-f', folder / 'output.mkv'], env=session.env | {'WAYLAND_DEBUG': 'client'},
            stdout=log,
            stderr=sp.STDOUT, start_new_session=True)
        item = None
        try:
            until(lambda: construction.READY.search(log_path.read_text(errors='replace')), process)
            time.sleep(.16)
            requested_ns = time.monotonic_ns()
            session.send('open', 'latency', width=440, height=300, construction='light')
            item = until(lambda: session.lookup('latency'), session.scene)
            time.sleep(.9)
            session.close('latency')
            x, y = item['at']
            width, height = item['size']
            item_box = (x, y, x + width, y + height)
            expected = (20, 235, 168) if workload == 'compute-stencil' else (242, 77, 191)
        except Exception as error:
            return dict(value_ms=None, unavailable=str(error))
        finally:
            if process.poll() is None:
                construction.stop(process, signal.SIGINT)
    try:
        pts, capture_ns, stream = construction.timing(folder)
        if item is None or len(capture_ns) != len(pts) or not capture_ns:
            return dict(value_ms=None, unavailable='screencopy/container timing did not align uniquely')
        frame_size = int(stream['width']) * int(stream['height']) * 3
        decoder_log = (folder / 'decoder.log').open('w')
        decoder = sp.Popen(['ffmpeg', '-v', 'error', '-threads', '2', '-i', folder / 'output.mkv',
                            '-map', '0:v:0', '-fps_mode', 'passthrough', '-enc_time_base', '1:1000',
                            '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-threads', '1', '-'],
                           stdout=sp.PIPE, stderr=decoder_log)
        baseline = None
        first_changed = None
        first_marker = None
        marker_observation = None
        changed_fraction_threshold = .001
        channel_quantization_bound = 2
        with decoder:
            for stamp in capture_ns:
                raw = decoder.stdout.read(frame_size)
                if len(raw) != frame_size:
                    break
                frame = Image.frombytes('RGB', (int(stream['width']), int(stream['height'])), raw)
                region = frame.crop(item_box)
                if stamp < requested_ns:
                    baseline = region.copy()
                    continue
                if baseline is None:
                    continue
                delta = ImageChops.difference(region, baseline)
                channels = delta.split()
                delta = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2])
                changed = Image.eval(delta, lambda value: 255 if value > channel_quantization_bound else 0)
                changed_fraction = changed.histogram()[255] / (region.width * region.height)
                if first_changed is None and changed_fraction >= changed_fraction_threshold:
                    first_changed = stamp
                observed = marker_metrics(frame, item_box, expected)
                if first_marker is None and observed['chroma_bias'] >= 45 and observed['mean_absolute_error'] <= 45:
                    first_marker = stamp
                    marker_observation = observed
        decoder.stdout.close()
        status = decoder.wait()
        decoder_log.close()
        if status:
            return dict(value_ms=None, unavailable='ffmpeg raw-frame decode failed')
        if baseline is None:
            return dict(value_ms=None, unavailable='no aligned lossless frame preceded the request')
        if first_changed is None:
            return dict(value_ms=None, unavailable='no client-ROI change exceeded the RGB quantization bound')
        if first_marker is None:
            return dict(value_ms=None, unavailable='client ROI changed but authored marker never appeared')
        return dict(value_ms=(first_changed - requested_ns) / 1e6,
                    request_monotonic_ns=requested_ns,
                    first_changed_screencopy_monotonic_ns=first_changed,
                    first_marker_screencopy_monotonic_ns=first_marker,
                    marker=marker_observation,
                    change_test=dict(client_roi=item_box, per_channel_difference_gt=channel_quantization_bound,
                                     changed_pixel_fraction_at_least=changed_fraction_threshold),
                    timestamp_source='lossless VFR frame PTS uniquely aligned to Wayland screencopy ready events',
                    metric='request-to-first-changed-client-ROI-frame; not physical display latency',
                    frame_count=len(pts), alignment_candidates=json.loads(
                        (folder / 'timestamps.json').read_text())['alignment_candidates'])
    except Exception as error:
        return dict(value_ms=None, unavailable=str(error))




def profile_trial(session, workload, count):
    if workload == 'idle':
        session.hc('eval', settings('{effect="",enabled=false}', '{effect="",enabled=false}'))
    elif workload == 'builtin':
        session.hc('eval', 'hl.config({plugin={hyprimate={window={enabled=true,style="liquid",movement={enabled=false},open={enabled=true,effect="",effect_duration=12000},close={enabled=false}}}}})')
        for option in ('plugin:hyprimate:window:enabled', 'plugin:hyprimate:window:open:enabled'):
            value = json.loads(session.hc('getoption', '-j', option))
            enabled = value.get('int', value.get('bool'))
            if enabled not in (1, True):
                raise RuntimeError(('built-in control animation is disabled', option, value))
    else:
        session.effect(FIXTURES / workload / 'effect.json', 12000)
    session.hc('eval', 'hl.config({plugin={hyprimate={window={close={enabled=false}}}}})')
    close_enabled = json.loads(session.hc(
        'getoption', '-j', 'plugin:hyprimate:window:close:enabled'))
    close_value = close_enabled.get('int', close_enabled.get('bool'))
    if close_value not in (0, False):
        raise RuntimeError(('benchmark close animation must be disabled for idle retirement', close_enabled))
    latency = first_changed_latency(session, workload) if workload.startswith('compute-') else None
    time.sleep(.5)
    before = snapshot_process(session.hypr.pid)
    if workload != 'idle':
        for i in range(count):
            item = session.open('bench-' + str(i))
            session.move(item, 30 + (i % 2) * 600, 30 + (i // 2) * 440)
    # Exclude cold preparation and the warm-up window from profiler and steady-state deltas.
    time.sleep(1.0)
    session.hc('hyprimate:profile', 'start')
    start = snapshot_process(session.hypr.pid)
    samples = [start]
    gpu_samples = (session.out / 'gpu-samples.csv').open('w')
    gpu_sampler = sp.Popen(
        ['nvidia-smi', '--query-gpu=timestamp,index,utilization.gpu,clocks.gr,clocks.mem,pstate',
         '--format=csv,noheader', '-l', '1'], stdout=gpu_samples, stderr=sp.STDOUT,
        start_new_session=True)
    sample_interval = 2.0 if workload == 'idle' else .5
    try:
        for _ in range(16):
            time.sleep(sample_interval)
            samples.append(snapshot_process(session.hypr.pid))
        report = json.loads(session.hc('hyprimate:profile', 'stop'))
    finally:
        if gpu_sampler.poll() is None:
            gpu_sampler.terminate()
            try:
                gpu_sampler.wait(timeout=3)
            except sp.TimeoutExpired:
                gpu_sampler.kill()
                gpu_sampler.wait()
        gpu_samples.close()
    for i in range(count if workload != 'idle' else 0):
        session.close('bench-' + str(i))
    time.sleep(.6)
    after = snapshot_process(session.hypr.pid)
    save(session.out / 'profile.json', report)
    save(session.out / 'process-samples.json', dict(before=before, samples=samples, after=after,
                                                    clock_ticks_per_second=os.sysconf('SC_CLK_TCK')))
    elapsed = (samples[-1]['monotonic_ns'] - start['monotonic_ns']) / 1e9
    cpu = ((samples[-1]['cpu_time_ns'] - start['cpu_time_ns']) / 1e9 / elapsed)
    rows = [row for row in report.get('styles', []) if row.get('domain') == 'window']
    if workload == 'builtin' and not any(
            row.get('style') == 'liquid' and row.get('drawn_frames', 0) > 0 for row in rows):
        raise RuntimeError(('built-in control produced no liquid style draws', rows))
    result = dict(workload=workload, instances=count, whole_process_cpu_fraction=cpu,
                  measurement_seconds=elapsed, process_sample_interval_seconds=sample_interval,
                  process_rss_peak=max(x['rss_bytes'] for x in samples),
                  process_pss_peak=max(x['pss_bytes'] for x in samples),
                  retained_idle=after, profile=report, window_rows=rows,
                  gpu_timing_available=report.get('gpu_status') == 'available' and
                  not report.get('gpu_disjoint_events') and any(r.get('gpu', {}).get('samples', 0) for r in rows),
                  gpu_sample_rows=sum(1 for line in (session.out / 'gpu-samples.csv').read_text().splitlines()
                                      if line.strip() and not line.lower().startswith('timestamp')),
                  latency=latency)
    session.unload_plugin()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--render-node', type=Path, required=True)
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--bench-plugin', type=Path)
    parser.add_argument('--cpu-affinity', metavar='CPU_LIST')
    args = parser.parse_args()
    if args.cpu_affinity is not None and not shutil.which('taskset'):
        parser.error('--cpu-affinity requires the taskset executable')
    for command in ('kwin_wayland', 'dbus-run-session', 'Hyprland', 'hyprctl',
                    'grim', 'wayland-info', 'nvidia-smi'):
        if not shutil.which(command):
            raise RuntimeError(f'missing nested evidence dependency: {command}')
    for name in ('compute-stencil', 'compute-particles'):
        fixture = FIXTURES / name / 'effect.json'
        if not fixture.is_file():
            raise RuntimeError(f'missing production compute fixture: {fixture}')
    plugin = args.plugin.resolve(strict=True)
    baseline = args.baseline.resolve(strict=True)
    bench = args.bench_plugin.resolve(strict=True) if args.bench_plugin else None
    node = args.render_node.resolve(strict=True)
    if not node.is_char_device():
        raise RuntimeError(f'not a DRM character device: {node}')
    if args.benchmark and not bench:
        raise RuntimeError('--benchmark requires --bench-plugin built with the test-only CPU adapter')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    provenance = dict(command=sys.argv, render_node=str(node),
                      artifacts={str(path): construction.digest(path) for path in (plugin, baseline, bench) if path},
                      fixture_hashes={str(path): construction.digest(path) for name in ('compute-stencil', 'compute-particles')
                                      for path in (FIXTURES / name).glob('*') if path.is_file()},
                      refresh_hz=60, topology='1280x960 scale 1')
    save(out / 'provenance.json', provenance)
    failures, results = [], []
    unverified = ['capture-replay no-advance invariant: no dispatch receipt is exposed for replay']

    def run_case(name, artifact, route, fn, *, compute=False, **options):
        try:
            result = session_run(out / name, artifact, node, route, fn,
                                 compute=compute, cpu_affinity=args.cpu_affinity, **options)
            save(out / name / 'result.json', result)
            results.append(dict(name=name, result=result))
            return result
        except Exception as error:
            failures.append(dict(name=name, error=str(error)))
            save(out / name / 'failure.json', failures[-1])
            return None

    if not args.benchmark:
        for route in ('auto', 'native', 'slibs'):
            for name in ('compute-stencil', 'compute-particles'):
                fixture = FIXTURES / name / 'effect.json'
                run_case(route + '-' + name, plugin, route,
                         lambda s, f=fixture: correctness(s, f), compute=True)
        run_case('es30-stencil', plugin, 'auto',
                 lambda s: correctness(s, FIXTURES / 'compute-stencil/effect.json'),
                 es30=True, compute=True)
        def expect_native_unavailable(session):
            try:
                session.effect(FIXTURES / 'compute-stencil/effect.json', 3000)
            except RuntimeError as error:
                if 'native' not in str(error).lower() and 'compute' not in str(error).lower():
                    raise
                return dict(expected_strict_native_failure=str(error))
            raise RuntimeError('forced native unexpectedly prepared on the GLES 3.0 lane')
        run_case('es30-forced-native', plugin, 'native', expect_native_unavailable, es30=True)
    else:
        # The lock coordinates cooperating evidence runs; process checks detect nonparticipants.
        with open('/tmp/hyprimate-compute-timing.lock', 'w') as lane:
            fcntl.flock(lane, fcntl.LOCK_EX | fcntl.LOCK_NB)
            quiet_environment(out)
            groups = {}
            # Same-binary controls first. Fresh processes for every observation.
            compute_modes = [('control-a', bench, 'native', False), ('control-b', bench, 'native', False),
                             ('cpu', bench, 'auto', True), ('native', bench, 'native', False),
                             ('slibs', bench, 'slibs', False)]
            unused_modes = [('baseline-control-a', baseline, 'auto', False),
                            ('baseline-control-b', baseline, 'auto', False),
                            ('shipping-control-a', plugin, 'auto', False),
                            ('shipping-control-b', plugin, 'auto', False),
                            ('baseline', baseline, 'auto', False),
                            ('shipping', plugin, 'auto', False)]
            workloads = [('compute-stencil', 1), ('compute-stencil', 4),
                         ('compute-particles', 1), ('compute-particles', 4),
                         ('idle', 1), ('builtin', 1), ('diffusion', 1)]
            # Run every same-binary control before any measured mode.
            for workload, count in workloads:
                selected = compute_modes if workload.startswith('compute-') else unused_modes
                controls = [mode for mode in selected if 'control-' in mode[0]]
                control_pairs = (('control-a', 'control-b'),) if workload.startswith('compute-') else (
                    ('baseline-control-a', 'baseline-control-b'),
                    ('shipping-control-a', 'shipping-control-b'))
                for batch in range(8):
                    order = controls if batch % 2 == 0 else list(reversed(controls))
                    for mode, artifact, route, cpu in order:
                        name = f'{workload}-{count}-{batch:02d}-{mode}'
                        value = run_case(name, artifact, route,
                                         lambda s, w=workload, c=count: profile_trial(s, w, c),
                                         cpu=cpu, compute=workload.startswith('compute-'))
                        if value:
                            groups.setdefault((workload, count, mode), []).append(value)
                for a_name, b_name in control_pairs:
                    a = groups.get((workload, count, a_name), [])
                    b = groups.get((workload, count, b_name), [])
                    if len(a) != 8 or len(b) != 8:
                        failures.append(dict(name=workload,
                                             error=f'incomplete same-binary controls {a_name}/{b_name}'))
                        break
                    av = [v['whole_process_cpu_fraction'] for v in a]
                    bv = [v['whole_process_cpu_fraction'] for v in b]
                    control = av + bv
                    cv = statistics.stdev(control) / statistics.mean(control) if statistics.mean(control) else math.inf
                    drift = abs(statistics.median(av) / statistics.median(bv) - 1) if statistics.median(bv) else math.inf
                    save(out / f'{workload}-{count}-{a_name}-noise.json',
                         dict(control_cv=cv, control_median_drift=drift, threshold_cv=.10,
                              threshold_drift=.03, controls='same artifact and backend'))
                    if cv > .10 or drift >= .03:
                        failures.append(dict(name=workload,
                                             error=f'control noise gate failed: CV={cv}, drift={drift}'))
                        break
                if failures:
                    break
            if not failures:
                # Only collect comparisons after all control lanes pass.
                for workload, count in workloads:
                    selected = compute_modes if workload.startswith('compute-') else unused_modes
                    measured = [mode for mode in selected if 'control-' not in mode[0]]
                    for batch in range(8):
                        order = measured if batch % 2 == 0 else list(reversed(measured))
                        for mode, artifact, route, cpu in order:
                            name = f'{workload}-{count}-{batch:02d}-{mode}'
                            value = run_case(name, artifact, route,
                                             lambda s, w=workload, c=count: profile_trial(s, w, c),
                                             cpu=cpu, compute=workload.startswith('compute-'))
                            if value:
                                groups.setdefault((workload, count, mode), []).append(value)
            comparisons = {}
            for workload, count, mode in list(groups):
                if 'control-' in mode:
                    continue
                reference = 'cpu' if workload.startswith('compute-') else 'baseline'
                left, right = groups[(workload, count, mode)], groups.get((workload, count, reference), [])
                if mode == reference or len(left) != 8 or len(right) != 8:
                    continue
                row = {}
                for metric in ('whole_process_cpu_fraction', 'process_rss_peak', 'process_pss_peak'):
                    lhs = [v[metric] for v in left]
                    rhs = [v[metric] for v in right]
                    row[metric] = dict(left=percentiles(lhs), right=percentiles(rhs),
                                       mann_whitney=compare(lhs, rhs),
                                       paired_median_ratio=matched_interval(lhs, rhs))
                row['gpu_timing'] = dict(left_available=sum(bool(v['gpu_timing_available']) for v in left),
                                         right_available=sum(bool(v['gpu_timing_available']) for v in right),
                                         note='Per-trial raw profile.json retains asynchronous GPU timing samples.')
                if workload.startswith('compute-'):
                    if all(v.get('latency') and v['latency'].get('value_ms') is not None
                           for v in left + right):
                        lhs = [v['latency']['value_ms'] for v in left]
                        rhs = [v['latency']['value_ms'] for v in right]
                        row['request_to_first_changed_ms'] = dict(
                            left=percentiles(lhs), right=percentiles(rhs),
                            mann_whitney=compare(lhs, rhs),
                            paired_median_ratio=matched_interval(lhs, rhs),
                            clock='request monotonic time to first changed client-ROI capture; not physical display latency')
                    else:
                        unverified.append(f'{workload}/{count}/{mode}: request-to-first-changed latency unavailable')
                comparisons[f'{workload}/{count}/{mode}-vs-{reference}'] = row
            save(out / 'comparisons.json', comparisons)
            if not any(v['gpu_timing_available'] for values in groups.values() for v in values):
                unverified.append('GPU duration: profiler did not return a usable asynchronous timing sample')
            unverified = sorted(set(unverified))
            save(out / 'benchmark-unverified.json', unverified)
    complete = not failures and not unverified
    save(out / 'summary.json', dict(results=results, failures=failures, complete=complete,
                                    unverified=unverified))
    if failures:
        raise SystemExit(1)
    if unverified:
        raise SystemExit(2)


if __name__ == '__main__':
    main()
