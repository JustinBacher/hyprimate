#!/usr/bin/env python3
"""Exercise native workspace gestures in disposable KWin → /usr/bin/Hyprland sessions.

Requires a matching compositor/header ABI, the BUILD_TESTING swipe probe, numpy,
Pillow, and the tools checked by preflight. Evidence directories are append-never.
No host socket, config, output, or process is used. --gdb launches a new child
under GDB (never attaches) and preserves all-thread backtraces on a fatal signal.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess as sp
import sys
import tempfile
import time


HYPRLAND = Path('/usr/bin/Hyprland')
CASES = ('keyboard', 'forward', 'reverse', 'cancel', 'interrupt', 'continuous')
CONFIGURATIONS = ('native', 'disabled', 'rotation')
COLORS = {1: 'b03030', 2: '208050', 3: '3040b0'}
# grim versions use either wlr-screencopy or ext-image-copy-capture. Both carry
# a compositor-provided monotonic presentation timestamp, unlike file mtimes.
PRESENTED = re.compile(r'(?:\.ready|\.presentation_time)\((\d+),\s*(\d+),\s*(\d+)\)')


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def preflight(args):
    global evidence, peer, Image
    # Import every transitive dependency before launching anything.
    import construction_evidence as evidence
    from construction_motion_evidence import peer
    from PIL import Image
    for path in (args.plugin, args.probe, HYPRLAND):
        require(path.is_file() and os.access(path, os.R_OK), f'Missing readable file: {path}')
    for tool in ('hyprctl', 'kwin_wayland', 'dbus-run-session', 'wayland-info',
                 'grim', 'kitty', 'sleep', 'nm', 'objdump', 'gdb'):
        require(shutil.which(tool), f'Missing tool: {tool}')
    require(os.access(HYPRLAND, os.X_OK), f'Not executable: {HYPRLAND}')
    for path in (args.plugin, args.probe):
        require(path.open('rb').read(4) == b'\x7fELF', f'Not an ELF plugin: {path}')
    require(not args.out.exists() or (args.out.is_dir() and not any(args.out.iterdir())),
            f'Refusing populated evidence directory: {args.out}')
    version = sp.check_output([HYPRLAND, '--version'], text=True, timeout=10)
    symbols = sp.check_output(['nm', '-D', '-n', '-C', HYPRLAND], text=True, timeout=15)
    matches = re.findall(r'^([0-9a-f]+) T CUnifiedWorkspaceSwipeGesture::(end|endSegment)\(\)$',
                         symbols, re.M)
    require({name for _, name in matches} == {'end', 'endSegment'},
            'Required native completion symbols are unavailable')
    instructions = {}
    for address, name in matches:
        start = int(address, 16)
        instructions[name] = sp.check_output(['objdump', '-d', f'--start-address={start}',
                                             f'--stop-address={start+16}', HYPRLAND], text=True)
    # For the known binary the complete displaced entry is eight branch-free
    # bytes. An unknown entry needs independent hook-relocation review, not a
    # speculative runtime test against the user's replacement compositor.
    entry = instructions['endSegment']
    segment_start = next(int(address, 16) for address, name in matches if name == 'endSegment')
    displaced = ((0, 'f3 0f 1e fa', 'endbr64'),
                 (4, '55', r'push\s+%rbp'), (5, '48 89 e5', r'mov\s+%rsp,%rbp'))
    require(all(re.search(rf'^\s*{segment_start+offset:x}:\s+{encoded}\s+{op}\s*$',
                          entry, re.M) for offset, encoded, op in displaced),
            'Unvalidated endSegment entry; inspect binary compatibility before testing')
    return dict(executable=str(HYPRLAND), version=version, instructions=instructions,
                plugin=str(args.plugin), plugin_sha256=evidence.digest(args.plugin),
                probe=str(args.probe), probe_sha256=evidence.digest(args.probe))


def poll(session, predicate, description, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        require(session.hypr.poll() is None, f'Private compositor terminated: {session.hypr.returncode}')
        value = predicate()
        if value:
            return value
        time.sleep(.025)
    raise RuntimeError(f'Timed out: {description}')


def configuration():
    return '''-- Entire configuration belongs to this disposable test child.
hl.monitor({output="",mode="preferred",position="0x0",scale=1})
hl.config({
 animations={enabled=true},
 debug={disable_logs=false,enable_stdout_logs=true},
 misc={disable_hyprland_logo=true,force_default_wallpaper=0},
 cursor={invisible=true},
 decoration={wobble={enabled=false},blur={enabled=false},shadow={enabled=false}},
 gestures={workspace_swipe_distance=300,workspace_swipe_cancel_ratio=0.5,
 workspace_swipe_min_speed_to_force=0,workspace_swipe_invert=false,
 workspace_swipe_create_new=false,workspace_swipe_use_r=false,workspace_swipe_forever=false}
})
hl.gesture({fingers=3,direction="horizontal",action="workspace"})
'''


def boot(self, mode, probe, gdb):
    # Only boot/setup differs from construction_evidence.Session. Its inherited
    # start, hc and cleanup retain process-group ownership and private IPC.
    for key in ('XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_STATE_HOME', 'XDG_DATA_HOME'):
        Path(self.env[key]).mkdir(mode=0o700, parents=True, exist_ok=True)
    for key in ('HYPRLAND_CMD', 'HYPRLAND_CONFIG', 'HYPRLAND_TRACE', 'LD_PRELOAD',
                'LD_LIBRARY_PATH', 'HYPRIMATE_FRAME_TRACE', 'XDG_ACTIVATION_TOKEN',
                'DESKTOP_STARTUP_ID', 'SWAYSOCK', 'I3SOCK'):
        self.env.pop(key, None)
    parent = self.start(self.out/'kwin.log', ['dbus-run-session', '--', 'kwin_wayland',
                        '--virtual', '--width', '1280', '--height', '960', '--no-lockscreen',
                        '--no-global-shortcuts', '--socket', 'workspace-swipe-parent'])
    evidence.until(lambda: (self.runtime/'workspace-swipe-parent').is_socket(), parent)
    self.env.update(WAYLAND_DISPLAY='workspace-swipe-parent', AQ_BACKENDS='wayland')
    evidence.until(lambda: 'wl_output' in sp.run(['wayland-info'], env=self.env,
                   capture_output=True, text=True, timeout=5).stdout, parent)
    config = self.runtime/'workspace-swipe.lua'
    config.write_text(configuration())
    shutil.copyfile(config, self.out/'hyprland.lua')
    argv = [str(HYPRLAND), '-c', str(config)]
    if gdb:
        commands = self.out/'gdb.commands'
        commands.write_text('set pagination off\nset confirm off\nset debuginfod enabled off\n'
                            'set breakpoint pending on\nbreak hook_gesture_end\n'
                            'commands\nsilent\nprintf \"WORKSPACE END HOOK REACHED\\n\"\n'
                            'bt 5\ncontinue\nend\n'
                            'handle SIGPIPE nostop noprint pass\nrun\n'
                            'thread apply all bt full\ninfo registers\nx/16i $pc-16\nquit\n')
        argv = ['gdb', '--batch', '-x', str(commands), '--args', *argv]
    self.hypr = self.start(self.out/'hyprland.log', argv,
                          extra={'LIBSEAT_BACKEND': 'seatd', 'SEATD_SOCK': str(self.runtime/'no-seat')})

    def identify():
        raw = sp.run(['hyprctl', 'instances', '-j'], env=self.env, capture_output=True,
                     text=True, timeout=5).stdout.strip()
        for item in json.loads(raw) if raw.startswith('[') else []:
            sig, sock = item['instance'], item['wl_socket']
            if (Path(sig).name != sig or Path(sock).name != sock or
                    sock == 'workspace-swipe-parent' or (not gdb and item['pid'] != self.hypr.pid)):
                continue
            if gdb:
                # GDB owns only this child; do not accept another private/host PID.
                status = Path(f'/proc/{item["pid"]}/status').read_text()
                if not re.search(rf'^PPid:\s+{self.hypr.pid}$', status, re.M):
                    continue
            if ((self.runtime/sock).is_socket() and
                    (self.runtime/'hypr'/sig/'.socket.sock').is_socket()):
                return item
        return None

    item = evidence.until(identify, self.hypr)
    self.signature = item['instance']
    self.instance_log = self.runtime/'hypr'/self.signature/'hyprland.log'
    self.env.update(HYPRLAND_INSTANCE_SIGNATURE=self.signature, WAYLAND_DISPLAY=item['wl_socket'])
    ownership = []
    for path in (self.runtime/'hypr'/self.signature/'.socket.sock', self.runtime/item['wl_socket']):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            ownership.append(peer(sock, path, item['pid']))
    save(self.out/'private-instance.json', dict(instance=item, ownership=ownership))
    def ready_output():
        return next((m for m in json.loads(self.hc('-j', 'monitors'))
                     if m['name'] != 'FALLBACK' and m['width'] > 0 and m['height'] > 0), None)

    try:
        monitor = poll(self, ready_output, 'initial nested output', timeout=2)
    except RuntimeError as error:
        if not str(error).startswith('Timed out:'):
            raise
        # Same late-output setup as check-gl.sh, confined to our verified child.
        require(self.hc('output', 'create', 'wayland', 'workspace-swipe-test') == 'ok',
                'Could not create private Wayland output')
        monitor = poll(self, ready_output, 'configured non-FALLBACK output', timeout=30)
    self.output = monitor['name']
    for other in json.loads(self.hc('-j', 'monitors')):
        if other['name'] not in ('FALLBACK', self.output):
            require(self.hc('output', 'remove', other['name']) == 'ok',
                    'Could not isolate a single private output')
    self.frame_size = (monitor['width'], monitor['height'])
    sp.run(['grim', '-o', self.output, self.out/'first-native-frame.png'], env=self.env,
           check=True, timeout=10)
    self.hc('plugin', 'load', str(probe))
    self.hc('eval', 'assert(not pcall(hl.plugin.workspace_swipe_test.event,\"unknown\",0,0)); '
            'assert(not pcall(hl.plugin.workspace_swipe_test.event,\"update\"))')
    if mode != 'native':
        self.hc('plugin', 'load', str(self.plugin))
        self.hc('eval', 'for _, n in ipairs(hl.notification.get()) do '
                'local text = n:get_text(); '
                'assert(not text or not text:find(\"workspace transitions unavailable\",1,true),text) end')
        loaded = json.loads(self.hc('-j', 'plugin', 'list'))
        require(any(p.get('name') == 'hyprimate' for p in loaded), 'Hyprimate failed to initialize')
        self.hc('eval', 'hl.config({plugin={hyprimate={window={enabled=false},workspace={'
                f'enabled={str(mode == "rotation").lower()},style="rotation"' + '}}}})')
        actual = json.loads(self.hc('-j', 'getoption', 'plugin:hyprimate:workspace:enabled'))
        require(actual.get('int', actual.get('bool')) == (mode == 'rotation'),
                f'Workspace animation setting not applied: {actual}')
    errors = self.hc('configerrors')
    require(not errors or errors == 'ok', f'Private configuration errors: {errors}')
    fixtures = []
    for workspace in (1, 2, 3):
        self.hc('dispatch', f'hl.dsp.focus({{workspace={workspace}}})')
        fixture = self.start(self.out/f'kitty-{workspace}.log', ['kitty', '--config', 'NONE',
                             '--title', f'WorkspaceSwipe-{workspace}', '-o', 'confirm_os_window_close=0',
                             '-o', f'background=#{COLORS[workspace]}', '-o', 'cursor_blink_interval=0',
                             'sleep', 'infinity'])
        client = poll(self, lambda: next((c for c in json.loads(self.hc('-j', 'clients'))
                      if c['mapped'] and c['pid'] == fixture.pid and
                      c['title'] == f'WorkspaceSwipe-{workspace}'), None), 'kitty fixture')
        require(client['workspace']['id'] == workspace, f'Fixture on wrong workspace: {client}')
        fixtures.append(client)
    save(self.out/'fixtures.json', fixtures)
    self.hc('dispatch', 'hl.dsp.focus({workspace=2})')
    return item


def logs(session):
    return '\n'.join(p.read_text(errors='replace') for p in
                     (session.out/'hyprland.log', session.instance_log) if p.exists())


def frame(session, label, newer_than=-1):
    path = session.out/f'{label}.png'
    trace = session.out/f'{label}-grim.log'
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        require(session.hypr.poll() is None, 'Compositor exited before rendering')
        with trace.open('a') as stream:
            result = sp.run(['grim', '-l', '1', '-o', session.output, path], env=session.env |
                            {'WAYLAND_DEBUG': 'client'}, stdout=stream, stderr=stream, timeout=4)
        if result.returncode == 0 and path.exists():
            with Image.open(path) as image:
                image.load()
                require(image.size == session.frame_size, f'Invalid native screenshot: {image.size}')
                # Exclude the native startup notification's independently ticking
                # progress bar and the terminal caret, not workspace content.
                interior = image.convert('RGB').crop((40, 80, image.width-40, image.height-40))
                visual_digest = hashlib.sha256(interior.tobytes()).hexdigest()
            timestamps = [((int(hi) << 32) | int(lo))*1_000_000_000 + int(ns)
                          for hi, lo, ns in PRESENTED.findall(trace.read_text(errors='replace'))]
            require(timestamps, 'grim did not provide compositor presentation timestamps')
            if timestamps[-1] > newer_than:
                return dict(path=str(path), presentation_ns=timestamps[-1],
                            sha256=evidence.digest(path), visual_sha256=visual_digest)
        time.sleep(.025)
    raise RuntimeError(f'No new rendered frame: {label}')


def workspace(session):
    return json.loads(session.hc('-j', 'activeworkspace'))['id']


def settled(session, expected, label):
    poll(session, lambda: workspace(session) == expected, f'workspace {expected}')
    # Bounded native captures, not a delay posing as successful rendering. Two
    # equal rendered images with advancing presentation timestamps establish
    # that the static fixture has settled after native/plugin animations.
    previous = frame(session, label+'-first', getattr(session, 'action_ns', -1))
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        current = frame(session, label, previous['presentation_ns'])
        with Image.open(current['path']) as image:
            pixel = image.convert('RGB').getpixel(tuple(n//2 for n in session.frame_size))
        color = tuple(bytes.fromhex(COLORS[expected]))
        correct_fixture = all(abs(a-b) <= 3 for a, b in zip(pixel, color))
        if (correct_fixture and current['visual_sha256'] == previous['visual_sha256']
                and workspace(session) == expected):
            return current
        previous = current
        time.sleep(.025)
    raise RuntimeError(f'Workspace rendering did not settle: {label}')


def focus(session, target, label):
    session.action_ns = time.monotonic_ns()
    session.hc('dispatch', f'hl.dsp.focus({{workspace={target}}})')
    return settled(session, target, label)


def event(session, case, phase, dx=0, dy=0, expected=None, prior=None):
    record = dict(phase=phase, dx=dx, dy=dy, request_ns=time.monotonic_ns(),
                  workspace_before=workspace(session))
    session.action_ns = record['request_ns']
    case['events'].append(record)
    save(session.out/'current-case.json', case)
    expression = f'hl.plugin.workspace_swipe_test.event("{phase}",{dx},{dy})'
    if expected is not None:
        expression = f'assert({expression} == {str(expected).lower()}, "incorrect gesture lifecycle")'
    session.hc('eval', expression)
    record.update(return_ns=time.monotonic_ns(), workspace_after=workspace(session))
    # The callback diagnostic is the source of truth, even when Lua IPC only
    # returns "ok". Preserve all diagnostics in the child log as well.
    text = (session.out/'hyprland.log').read_text(errors='replace')
    entries = [line for line in text.splitlines() if 'workspace-swipe-probe ' in line]
    record['diagnostics'] = entries[-2:]
    require(len(entries) >= 2 and ' entry ' in entries[-2] and ' return ' in entries[-1]
            and all(f'phase={phase}' in line for line in entries[-2:]),
            f'Missing phase diagnostics: {entries[-2:]}')
    record['active_before'] = 'in_progress=true' in entries[-2]
    record['active_after'] = 'in_progress=true' in entries[-1]
    if prior is not None:
        require(any(f'phase={phase}' in line and 'entry' in line and
                    f'in_progress={str(prior).lower()}' in line for line in entries[-2:]),
                f'Incorrect native state before {phase}: {entries[-2:]}')
    record['expected_active'] = expected
    return record['workspace_after']


def updates(session, case, count, dx):
    selected = []
    for _ in range(count):
        time.sleep(.016)
        selected.append(event(session, case, 'update', dx, 0, expected=True))
    return selected


def complete(session, case, destination, label, dx=20):
    event(session, case, 'begin', expected=False)
    updates(session, case, 10, dx)
    event(session, case, 'end', expected=False)
    case['screenshots'].append(settled(session, destination, label))


def run_cases(session, mode, result):
    for name in CASES:
        case = dict(name=name, events=[], screenshots=[], passed=False)
        result['cases'].append(case)
        try:
            case['screenshots'].append(focus(session, 2, name+'-setup'))
            case['workspace_before'] = workspace(session)
            if name == 'keyboard':
                offset = (session.out/'hyprland.log').stat().st_size
                case['screenshots'].append(focus(session, 3, 'keyboard-to-3'))
                case['screenshots'].append(focus(session, 2, 'keyboard-to-2'))
                if mode == 'rotation':
                    observed = poll(session, lambda: (text if
                        re.search(r'\[hyprimate\] workspace request[^\n]*accepted=true', text) and
                        '[hyprimate] workspace landed' in text else None)
                        if (text := (session.out/'hyprland.log').read_text(errors='replace')[offset:])
                        else None, 'accepted and landed rotation')
                    (session.out/'keyboard-animation.log').write_text(observed)
                # Sample native idle state rather than inferring it from keyboard IPC.
                event(session, case, 'end', expected=False, prior=False)
            elif name in ('forward', 'reverse'):
                complete(session, case, 3 if name == 'forward' else 1, name,
                         20 if name == 'forward' else -20)
            elif name == 'cancel':
                event(session, case, 'begin', expected=False)
                updates(session, case, 2, 20)
                event(session, case, 'end', expected=False)
                case['screenshots'].append(settled(session, 2, 'cancel-retained-2'))
                complete(session, case, 3, 'cancel-recovery')
            elif name == 'interrupt':
                event(session, case, 'begin', expected=False)
                updates(session, case, 2, 20)
                case['screenshots'].append(focus(session, 1, 'interrupt-keyboard'))
                event(session, case, 'end', expected=False, prior=False)
                case['screenshots'].append(settled(session, 1, 'interrupt-trailing-end'))
                complete(session, case, 2, 'interrupt-recovery')
            elif name == 'continuous':
                case['screenshots'].append(focus(session, 1, 'continuous-origin'))
                session.hc('eval', 'hl.config({gestures={workspace_swipe_forever=true}})')
                event(session, case, 'begin', expected=False)
                first = updates(session, case, 16, 20)
                require(2 in first, f'No intermediate workspace 2: {first}')
                second = updates(session, case, 16, 20)
                require(3 in second, f'No intermediate workspace 3: {second}')
                case['intermediate_workspaces'] = first + second
                event(session, case, 'end', expected=False)
                case['screenshots'].append(settled(session, 3, 'continuous-final'))
            require('workspace transitions unavailable' not in logs(session),
                    'Workspace integration reported unavailable')
            case.update(workspace_after=workspace(session),
                        final_gesture_active=case['events'][-1]['active_after'], passed=True)
        except Exception as error:
            case['error'] = str(error)
            # Do not send more callbacks into an interrupted/unknown lifecycle.
            # Independent configurations still run in fresh compositor processes.
            raise
        finally:
            save(session.out/'results.json', result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plugin', required=True, type=lambda p: Path(p).resolve())
    parser.add_argument('--probe', required=True, type=lambda p: Path(p).resolve())
    parser.add_argument('--out', required=True, type=lambda p: Path(p).resolve())
    parser.add_argument('--gdb', action='store_true', help='Launch each private child under GDB')
    args = parser.parse_args()
    try:
        metadata = preflight(args)
    except Exception as error:
        parser.exit(2, f'Preflight failed: {error}\n')
    # SIGTERM must unwind Session.cleanup rather than orphaning its new sessions.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    args.out.mkdir(parents=True, exist_ok=True)
    tested_plugin, tested_probe = args.out/'tested-hyprimate.so', args.out/'tested-workspace-swipe-probe.so'
    shutil.copyfile(args.plugin, tested_plugin)
    shutil.copyfile(args.probe, tested_probe)
    require(evidence.digest(tested_plugin) == metadata['plugin_sha256'] and
            evidence.digest(tested_probe) == metadata['probe_sha256'],
            'Plugin files changed while preserving evidence; no compositor launched')
    metadata.update(requested_plugin=metadata['plugin'], requested_probe=metadata['probe'],
                    plugin=str(tested_plugin), probe=str(tested_probe))
    save(args.out/'environment.json', metadata)
    results = []
    for mode in CONFIGURATIONS:
        folder = args.out/mode
        folder.mkdir()
        result = dict(configuration=mode, plugin=metadata['plugin'], version=metadata['version'],
                      cases=[], passed=False)
        results.append(result)
        result['required_cases'] = list(CASES)
        # AF_UNIX paths include Hyprland's long instance signature (108-byte cap).
        with tempfile.TemporaryDirectory(prefix='ws-') as temporary:
            runtime = Path(temporary)
            runtime.chmod(0o700)
            session = evidence.Session(runtime, folder, tested_plugin, None)
            # Keep the shared session implementation; override boot only.
            session.boot = lambda: boot(session, mode, tested_probe, args.gdb)
            try:
                item = session.boot()
                result['compositor_pid'] = item['pid']
                result['runtime_version'] = json.loads(session.hc('-j', 'version'))
                run_cases(session, mode, result)
                result['passed'] = True
            except Exception as error:
                result['error'] = str(error)
                result['supervisor_exit'] = session.hypr.poll() if hasattr(session, 'hypr') else None
                result['skipped_cases'] = [name for name in CASES
                                           if not any(c['name'] == name for c in result['cases'])]
                print(f'{mode}: FAIL: {error}', flush=True)
            finally:
                session.cleanup()
                save(folder/'results.json', result)
                save(args.out/'results.json', results)
        if result['passed']:
            print(f'{mode}: PASS ({len(result["cases"])} cases)', flush=True)
    return 0 if all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    sys.exit(main())
