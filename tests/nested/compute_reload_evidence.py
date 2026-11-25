#!/usr/bin/env python3
"""Prove transactional compute reloads in a disposable nested compositor.

Requires the dependencies of compute_evidence.py and tesseract. The private
fixture is changed in place; no installed effect or compositor is modified.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess as sp
import sys
import tempfile
import time

from PIL import Image, ImageOps, ImageStat

import compute_evidence as evidence
from burn_evidence import until


GREEN = (20, 235, 168)
MAGENTA = (235, 26, 209)
CONSUMER = (242, 26, 5)
POSITIONS = {'a': (30, 60), 'b': (500, 60), 'c': (30, 430)}


def fixture_copy(session):
    folder = session.runtime / 'compute-stencil-private'
    shutil.copytree(evidence.FIXTURES / 'compute-stencil', folder)
    seed = folder / 'seed.glsl'
    source = seed.read_text()
    needle = 'void kernel(ivec3 id, inout float energy) {'
    if source.count(needle) != 1:
        raise RuntimeError('unexpected stencil seed source')
    # A stable nonzero field makes the presentation probe independent of frame
    # count while retaining the original impulses and Jacobi update.
    seed.write_text(source.replace(needle, needle + '\n    energy = 0.5;', 1))
    compose = folder / 'compose.frag'
    source = compose.read_text()
    needle = '    color = marker && visibility > 0.025 ?'
    replacement = ('    bool probe = all(greaterThan(uv, vec2(0.20, 0.075))) && '
                   'all(lessThan(uv, vec2(0.30, 0.15)));\n'
                   '    if (probe && visibility > 0.025) {\n'
                   '        color = energy > 0.1 ? vec4(0.95, 0.1, 0.02, 1.0) : '
                   'vec4(0.02, 0.1, 0.95, 1.0);\n'
                   '        return;\n'
                   '    }\n' + needle)
    if source.count(needle) != 1:
        raise RuntimeError('unexpected stencil compose source')
    compose.write_text(source.replace(needle, replacement, 1))
    return folder


def position(session, name):
    item = session.open(name)
    x, y = POSITIONS[name]
    session.move(item, x, y)
    return until(lambda: (current := session.lookup(name)) and current['at'] == [x, y]
                 and current['address'] == item['address'] and current, session.hypr)


def geometry(session, names):
    items = {}
    for name in names:
        item = session.lookup(name)
        if not item or item['at'] != list(POSITIONS[name]) or item['size'] != [440, 300]:
            raise RuntimeError(('window moved, disappeared or resized', name, item))
        items[name] = item
    rectangles = [(v['at'][0], v['at'][1], v['at'][0] + v['size'][0],
                   v['at'][1] + v['size'][1]) for v in items.values()]
    for i, a in enumerate(rectangles):
        for b in rectangles[i + 1:]:
            if a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]:
                raise RuntimeError(('window rectangles overlap', rectangles))
    return items


def inspect(session, name, expected):
    items = geometry(session, expected)
    frame = session.snap(name)
    observations = {}
    for ident, color in expected.items():
        item = items[ident]
        x, y = item['at']
        w, h = item['size']
        marker = evidence.marker_metrics(frame, (x, y, x + w, y + h), color)
        expected_chroma = max(color) - sorted(color)[-2]
        if marker['chroma_bias'] < expected_chroma - 12 or marker['mean_absolute_error'] > 12:
            raise RuntimeError(('marker missing or wrong generation', name, ident, marker))
        probe_box = (x + round(.22 * w), y + round(.09 * h),
                     x + round(.28 * w), y + round(.13 * h))
        mean = ImageStat.Stat(frame.crop(probe_box)).mean
        error = sum(abs(a - b) for a, b in zip(mean, CONSUMER)) / 3
        if error > 45 or mean[0] - max(mean[1:]) < 100:
            raise RuntimeError(('computed field consumer probe failed', name, ident, mean))
        observations[ident] = dict(geometry=item, marker=marker,
                                   consumer_box=probe_box, consumer_rgb=mean,
                                   consumer_error=error)
    return observations
def verify_selection(session, manifest):
    prefix = 'plugin:hyprimate:window:'

    def value(option):
        raw = json.loads(session.hc('-j', 'getoption', prefix + option))
        return raw.get('str') if option.endswith(':effect') else raw

    enabled = json.loads(session.hc('-j', 'getoption', prefix + 'enabled')).get('bool')
    if enabled is not True:
        raise RuntimeError(('private config reload reset global window enabled', enabled))
    for event in ('open', 'close'):
        selected = value(event + ':effect')
        event_enabled = json.loads(session.hc('-j', 'getoption', prefix + event + ':enabled')).get('bool')
        duration = json.loads(session.hc('-j', 'getoption', prefix + event + ':effect_duration'))
        if selected != str(manifest) or event_enabled is not True or duration.get('int', duration.get('float')) != 6000:
            raise RuntimeError(('private config reload reset compute request', event, selected,
                                event_enabled, duration))



def run_case(session):
    folder = fixture_copy(session)
    manifest = folder / 'effect.json'
    step = folder / 'step.glsl'
    compose = folder / 'compose.frag'
    valid_step = step.read_text()
    valid_compose = compose.read_text()
    # Keep the exact request in the private config file because a native reload
    # replaces IPC overrides before the production refresh listener runs.
    choice = ('{enabled=true,effect=' + json.dumps(str(manifest)) +
              ',effect_duration=6000}')
    config = session.runtime / 'construction.lua'
    with config.open('a') as stream:
        stream.write('\n' + evidence.settings(choice, choice) +
                     '\nhl.config({plugin={hyprimate={window={enabled=true}}}})\n')
    session.effect(manifest, 6000)
    verify_selection(session, manifest)
    position(session, 'a')
    time.sleep(.35)
    results = {'fixture': str(manifest), 'backend': session.route}
    results['before'] = inspect(session, 'before-invalid', {'a': GREEN})

    # An unresolved function call is live output data, so both strict backends
    # must reject it during actual shader compilation.
    bad = valid_step.replace('energy = c + diffusion',
                             'energy = rollback_missing_function(c) + diffusion', 1)
    if bad == valid_step:
        raise RuntimeError('unexpected stencil step source')
    step.write_text(bad)
    log_offset = session.instance_log.stat().st_size
    session.hc('reload')
    if session.hc('configerrors'):
        raise RuntimeError('private config parse failed instead of kernel compilation')
    verify_selection(session, manifest)
    time.sleep(.2)
    session.snap('invalid-reload-notification')
    image = session.out / 'invalid-reload-notification.png'
    with Image.open(image) as screenshot:
        banner = screenshot.crop((round(screenshot.width * .566), 10, screenshot.width, 34))
        ocr_image = ImageOps.invert(banner.convert('L')).resize(
            (banner.width * 4, banner.height * 4), Image.Resampling.LANCZOS)
        ocr_image.save(session.out / 'invalid-reload-notification-ocr.png')
    ocr = sp.run(['tesseract', str(session.out / 'invalid-reload-notification-ocr.png'),
                  'stdout', '--psm', '7'], text=True, capture_output=True,
                 timeout=15, check=True)
    (session.out / 'invalid-reload-notification.txt').write_text(ocr.stdout + '\n' + ocr.stderr)
    new_log = session.instance_log.read_bytes()[log_offset:].decode(errors='replace')
    diagnostic = ocr.stdout + '\n' + new_log
    # Verify the banner names the modified kernel, compile error and deliberate
    # unresolved identifier; an active drawable generation is not proof.
    if not (re.search(r'\bstep\b', ocr.stdout, re.I) and
            re.search(r'\berror\b', ocr.stdout, re.I) and
            re.search(r'rollback_missing', ocr.stdout, re.I)):
        raise RuntimeError(('no observable compute compilation failure diagnostic', diagnostic))
    results['failure_diagnostic'] = diagnostic
    session.hc('dismissnotify', '-1')
    position(session, 'b')
    time.sleep(.3)
    results['after_failure'] = inspect(session, 'after-invalid', {'a': GREEN, 'b': GREEN})

    step.write_text(valid_step)
    old_color = 'vec3(0.08, 0.92, 0.66)'
    if valid_compose.count(old_color) != 1:
        raise RuntimeError('unexpected stencil marker source')
    compose.write_text(valid_compose.replace(old_color, 'vec3(0.92, 0.10, 0.82)', 1))
    session.hc('reload')
    if session.hc('configerrors'):
        raise RuntimeError('private restored config reload failed')
    verify_selection(session, manifest)
    position(session, 'c')
    time.sleep(.3)
    results['after_restore'] = inspect(session, 'after-valid',
                                       {'a': GREEN, 'b': GREEN, 'c': MAGENTA})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--render-node', type=Path, required=True)
    parser.add_argument('--backend', choices=('native', 'slibs'), required=True)
    args = parser.parse_args()
    for command in ('kwin_wayland', 'dbus-run-session', 'Hyprland', 'hyprctl',
                    'grim', 'wayland-info', 'nvidia-smi', 'tesseract'):
        if not shutil.which(command):
            raise RuntimeError(f'missing nested evidence dependency: {command}')
    plugin = args.plugin.resolve(strict=True)
    node = args.render_node.resolve(strict=True)
    if not node.is_char_device():
        raise RuntimeError(f'not a DRM character device: {node}')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    evidence.save(out / 'provenance.json', dict(command=sys.argv, plugin=str(plugin),
                  plugin_sha256=evidence.construction.digest(plugin), render_node=str(node),
                  backend=args.backend))
    result = None
    with tempfile.TemporaryDirectory(prefix='hc-reload.', dir='/tmp') as temp:
        session = evidence.Session(Path(temp), out, plugin, node, args.backend)
        session.expect_compute = True
        try:
            session.boot()
            device = session.verify_device()
            result = run_case(session)
            result['device'] = device
            evidence.save(out / 'observations.json', result)
        finally:
            cleanup_error = None
            try:
                session.cleanup()
            except BaseException as error:
                cleanup_error = error
            # This runs after all private children have been reaped, including on
            # assertion failure, whenever a successful generation was prepared.
            log = out / 'private-hyprland.log'
            if log.exists() and 'compute generation prepare ' in log.read_text(errors='replace'):
                receipts = evidence.check_receipts(out, args.backend)
                if result is not None:
                    result['receipts'] = receipts
                    evidence.save(out / 'observations.json', result)
            if cleanup_error:
                raise cleanup_error
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
