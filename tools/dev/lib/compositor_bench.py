"""Profile normal animation workloads in disposable nested Hyprland sessions."""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess as sp
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
SCENARIOS = {
    "windows.many": "Concurrent Construction closes on one output (16 windows by default)",
    "capture.blend": "Planet Hop preview and live capture on two outputs",
    "burn.close": "Burn close, detached debris and shared ground",
    "planet.handoff": "Planet Hop native handoff with one textured neighbour",
}
SECONDARY = "BENCH-SECONDARY"
CONSTRUCTION_SECONDS = .85
CAPTURE_SECONDS = .43
PLANET_SECONDS = .80
PLANET_MARK_REGION = (450, 30, 720, 470)
BURN_SECONDS = .65
NATIVE_SETTLE = .13


def _require_tools() -> None:
    missing = [name for name in ("dbus-run-session", "kwin_wayland", "Hyprland", "hyprctl")
               if shutil.which(name) is None]
    if missing:
        raise RuntimeError("private compositor benchmark requires: " + ", ".join(missing))


class CompositorBench:
    def __init__(self, binary: Path, windows: int):
        _require_tools()
        self.plugin = (binary.resolve(strict=True).parent.parent / "hyprimate" / "hyprimate.so").resolve(strict=True)
        if windows not in (4, 16):
            raise ValueError("windows.many supports exactly 4 or 16 windows")
        self.windows = windows
        # Keep Hyprland's generated Wayland/IPC socket paths below AF_UNIX path limits.
        self.temp = tempfile.TemporaryDirectory(prefix="hb-", dir="/tmp")
        self.root = Path(self.temp.name)
        self.runtime = self.root
        self.home = self.root / "home"
        self.home.mkdir()
        self.processes: list[sp.Popen] = []
        self.workspace_routes = None
        self.burn_ready = False
        self.scene: sp.Popen | None = None
        self.base_env = dict(os.environ)
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "HYPRLAND_INSTANCE_SIGNATURE",
                    "DBUS_SESSION_BUS_ADDRESS", "SESSION_MANAGER", "XDG_SESSION_ID", "XDG_VTNR",
                    "DBUS_STARTER_ADDRESS", "DBUS_STARTER_BUS_TYPE", "DBUS_SESSION_BUS_PID",
                    "LD_PRELOAD", "LD_LIBRARY_PATH"):
            self.base_env.pop(key, None)
        self.base_env.update(
            XDG_RUNTIME_DIR=str(self.runtime), HOME=str(self.home),
            XDG_CONFIG_HOME=str(self.home / "config"), XDG_CACHE_HOME=str(self.home / "cache"),
            XDG_DATA_HOME=str(self.home / "data"), XDG_STATE_HOME=str(self.home / "state"),
            XDG_SESSION_TYPE="wayland", NO_AT_BRIDGE="1", GDK_BACKEND="wayland",
            QT_QPA_PLATFORM="wayland", HYPRLAND_NO_SD_VARS="1", HYPRLAND_NO_SD_NOTIFY="1")
        self.hypr_env = dict(
            self.base_env, AQ_BACKENDS="wayland", WAYLAND_DISPLAY="bench-parent",
            LIBSEAT_BACKEND="seatd", SEATD_SOCK=str(self.runtime / "no-seat"))
        self.instance = ""
        self.compositor: sp.Popen | None = None
        try:
            self._start()
        except BaseException:
            self._print_logs()
            self.close()
            raise

    def _spawn(self, name: str, command: list[str], env: dict[str, str], stdin=sp.DEVNULL) -> sp.Popen:
        log_path = self.root / f"{name}.log"
        with log_path.open("w") as log:
            proc = sp.Popen(command, env=env, stdin=stdin, stdout=log, stderr=sp.STDOUT, text=True,
                            start_new_session=True, cwd=ROOT)
        self.processes.append(proc)
        return proc

    def _until(self, predicate, process: sp.Popen, what: str, timeout: float = 25):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and process.poll() is None:
            value = predicate()
            if value:
                return value
            time.sleep(.05)
        raise RuntimeError(f"private compositor timed out waiting for {what}; exit={process.poll()}")

    def _print_logs(self) -> None:
        paths = [self.root / "parent.log", self.root / "hyprland.log", self.root / "scene.log"]
        paths.extend(sorted((self.runtime / "hypr").glob("*/hyprland*.log")))
        for path in paths:
            try:
                with path.open("rb") as log:
                    log.seek(0, os.SEEK_END)
                    size = log.tell()
                    log.seek(max(0, size - 16384))
                    text = log.read().decode(errors="replace")
            except OSError:
                continue
            print(f"--- {path.relative_to(self.root)} (last 16 KiB) ---", file=sys.stderr)
            print(text, file=sys.stderr, end="" if text.endswith("\n") else "\n", flush=True)

    def _start(self) -> None:
        parent = self._spawn("parent", ["dbus-run-session", "--", "kwin_wayland", "--virtual",
            "--width", "1280", "--height", "960", "--no-lockscreen", "--no-global-shortcuts",
            "--socket", "bench-parent"], self.base_env)
        self._until(lambda: (self.runtime / "bench-parent").is_socket(), parent, "private KWin socket")
        config = self.root / "hyprland.lua"
        text = (ROOT / "tests/nested/hyprland.lua").read_text()
        text += '''
hl.monitor({output="",mode="1280x960@60",position="0x0",scale=1,bitdepth=8})
hl.config({debug={enable_stdout_logs=true},general={border_size=0},
           decoration={rounding=0,wobble={enabled=false},shadow={enabled=false},blur={enabled=false}}})
for _,leaf in ipairs({"windowsIn","windowsOut","fadeOut"}) do
    hl.animation({leaf=leaf,enabled=true,speed=leaf=="windowsIn" and 1 or 3,bezier="default"})
end
hl.animation({leaf="workspaces",enabled=true,speed=1,bezier="default"})
'''
        text += 'hl.window_rule({name="bench-fixtures",match={title="^PaperEvidence-.*$"},float=true,move="32 32",no_screen_share=false})\n'
        text += 'hl.window_rule({name="bench-burn",match={title="^PaperEvidence-20000$"},float=true,move="300 240"})\n'
        text += 'hl.window_rule({name="bench-anchor",match={title="^PaperEvidence-9000$"},float=true,move="1200 880",no_screen_share=false})\n'
        for index in range(16):
            x, y = 20 + (index % 4) * 310, 20 + (index // 4) * 220
            text += (f'hl.window_rule({{name="bench-window-{index}",match={{title="^PaperEvidence-{10000 + index}$"}},'
                     f'float=true,move="{x} {y}"}})\n')
        config.write_text(text)
        self.compositor = self._spawn("hyprland", ["Hyprland", "-c", str(config)], self.hypr_env)
        instance = self._until(self._identify, self.compositor, "private Hyprland IPC")
        self.instance = instance["instance"]
        # IPC may precede the private parent's first configured output.
        outputs = self._until(lambda: (values if len(values := self.monitors()) == 1 else None),
                              self.compositor, "one private output", 10)
        self.primary = outputs[0]
        self._start_scene(instance["wl_socket"])

    def _identify(self):
        try:
            result = sp.run(["hyprctl", "instances", "-j"], env=self.base_env,
                            text=True, capture_output=True, timeout=2)
            if result.returncode:
                return None
            for entry in json.loads(result.stdout):
                if entry.get("pid") == self.compositor.pid:
                    directory = self.runtime / "hypr" / entry["instance"]
                    if ((directory / ".socket.sock").is_socket()
                            and (self.runtime / entry["wl_socket"]).is_socket()):
                        return entry
        except (OSError, ValueError, sp.TimeoutExpired):
            return None
        return None

    def hc(self, *args: str, timeout: float = 8) -> str:
        result = sp.run(["hyprctl", "-i", self.instance, *args], env=self.base_env,
                        text=True, capture_output=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError((args, result.stdout.strip(), result.stderr.strip()))
        reply = result.stdout.strip()
        if args and args[0] in ("plugin", "eval", "dispatch", "output") and reply.lower() != "ok":
            raise RuntimeError((args, reply, result.stderr.strip()))
        return reply

    def monitors(self) -> list[dict]:
        values = (monitor for monitor in json.loads(self.hc("-j", "monitors"))
                  if monitor["name"] != "FALLBACK" and monitor["width"] > 0 and monitor["height"] > 0)
        return sorted(values, key=lambda monitor: (monitor["x"], monitor["y"]))

    def configure(self, settings: str) -> None:
        self.hc("eval", 'hl.config({plugin={hyprimate={' + settings + '}}})')
        errors = self.hc("configerrors")
        if errors:
            raise RuntimeError(("private compositor rejected benchmark configuration", errors))

    def _start_scene(self, socket_name: str) -> None:
        self.scene_env = dict(self.base_env, WAYLAND_DISPLAY=socket_name, GTK_USE_PORTAL="0")
        self.scene = self._spawn("scene", [sys.executable, str(ROOT / "tests/nested/paper_scene.py")],
                                 self.scene_env, stdin=sp.PIPE)
        self.hc("plugin", "load", str(self.plugin))
        log = self.root / "scene.log"
        self._until(lambda: '"ready": true' in log.read_text(errors="replace"), self.scene,
                    "private GTK fixture controller")

    def _scene_send(self, operation: str, identifiers: list[int], **kwargs) -> None:
        if self.scene is None or self.scene.poll() is not None or self.scene.stdin is None:
            raise RuntimeError("private GTK fixture controller exited")
        self.scene.stdin.write("".join(json.dumps({"op": operation, "id": ident, **kwargs}) + "\n"
                                       for ident in identifiers))
        self.scene.stdin.flush()

    def clients(self) -> list[dict]:
        return json.loads(self.hc("-j", "clients"))

    def _wait_clients(self, predicate, label: str) -> None:
        self._until(lambda: predicate(self.clients()), self.compositor, label, 10)

    def _focus_workspace(self, monitor: str, workspace: int) -> None:
        self.hc("dispatch", f'hl.dsp.focus({{monitor="{monitor}"}})')
        self.hc("dispatch", f'hl.dsp.focus({{workspace={workspace}}})')

    def _open(self, identifiers: list[int], size=(250, 150), workspace=1, monitor=None,
              **fixture_options) -> None:
        name = monitor or self.primary["name"]
        self._focus_workspace(name, workspace)
        before = {entry["title"] for entry in self.clients()}
        self._scene_send("open", identifiers, width=size[0], height=size[1], **fixture_options)
        titles = {f"PaperEvidence-{ident}" for ident in identifiers}
        self._wait_clients(lambda rows: titles <= {entry["title"] for entry in rows if entry["mapped"]},
                           "fixture windows mapped")
        if titles & before:
            raise RuntimeError("benchmark fixture ID collided with an existing native window")

    def _close(self, identifiers: list[int]) -> None:
        self._scene_send("close", identifiers)
        titles = {f"PaperEvidence-{ident}" for ident in identifiers}
        self._wait_clients(lambda rows: not (titles & {entry["title"] for entry in rows if entry["mapped"]}),
                           "native fixture unmaps")

    @staticmethod
    def _topology(outputs: list[dict]) -> list[dict]:
        return [{key: output[key] for key in ("name", "x", "y", "width", "height")}
                for output in outputs]

    def sample(self, scenario: str) -> dict:
        if scenario == "windows.many":
            return self._windows()
        if scenario == "capture.blend":
            return self._workspace(two_outputs=True)
        if scenario == "burn.close":
            return self._burn()
        if scenario == "planet.handoff":
            return self._workspace(two_outputs=False)
        raise RuntimeError(f"unknown compositor scenario: {scenario}")

    def _start_profile(self) -> None:
        if self.hc("hyprimate:profile", "start") != "ok":
            raise RuntimeError("could not start the production live profiler")

    def _stop_profile(self) -> dict:
        profile = json.loads(self.hc("hyprimate:profile", "stop", timeout=10))
        if profile.get("enabled") is not False:
            raise RuntimeError("production live profiler did not stop")
        return profile

    @staticmethod
    def _profile_row(profile, domain, style, operation):
        try:
            return next(row for row in profile["styles"]
                        if (row["domain"], row["style"], row["operation"]) == (domain, style, operation))
        except StopIteration:
            raise RuntimeError(f"no live profile for {domain}/{style}/{operation}") from None

    @staticmethod
    def _receipt(semantic, profile, started, metric, value_ns):
        if not math.isfinite(value_ns) or value_ns <= 0:
            raise RuntimeError(f"{metric}: no positive live timing observation")
        digest = hashlib.sha256(json.dumps(semantic, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        return {"checksum": digest, "semantic": semantic, "profile": profile,
                "scope_metric": metric, "ns_per_run": value_ns,
                "runner_wall_ns": time.perf_counter_ns() - started}

    def _windows(self) -> dict:
        started = time.perf_counter_ns()
        if len(outputs := self.monitors()) != 1:
            raise RuntimeError(f"windows.many requires exactly one active output, got {len(outputs)}")
        primary = outputs[0]
        if primary.get("activeWorkspace", {}).get("id") != 1:
            raise RuntimeError("windows.many requires workspace 1 to be active")

        identifiers = list(range(10_000, 10_000 + self.windows))
        titles = [f"PaperEvidence-{ident}" for ident in identifiers]
        present = {entry["title"] for entry in self.clients()}
        if not present:
            self.configure('workspace={enabled=false},window={style="construction",open={enabled=false},'
                           f'close={{enabled=true,options={{construction={{duration={CONSTRUCTION_SECONDS * 1000}}}}}}},'
                           'options={construction={worker_size=1}}}')
        missing = [ident for ident, title in zip(identifiers, titles) if title not in present]
        if missing:
            self._open(missing, size=(250, 150), workspace=1, monitor=primary["name"])
        if "PaperEvidence-9000" not in present:
            self._open([9_000], size=(80, 60), workspace=1, monitor=primary["name"])

        clients = {entry["title"]: entry for entry in self.clients()}
        fixtures = [clients.get(title) for title in titles]
        anchor = clients.get("PaperEvidence-9000")
        if (any(entry is None for entry in fixtures) or anchor is None
                or any(tuple(entry["size"]) != (250, 150) for entry in fixtures if entry is not None)
                or len({tuple(entry["at"]) for entry in fixtures if entry is not None}) != self.windows
                or tuple(anchor["size"]) != (80, 60) or tuple(anchor["at"]) != (1200, 880)):
            raise RuntimeError("windows.many mapped fixtures lack their distinct expected geometry and anchor")
        geometry = [{"title": entry["title"], "at": list(entry["at"]), "size": list(entry["size"])}
                    for entry in sorted(fixtures, key=lambda item: item["title"]) if entry is not None]
        anchor_geometry = {"title": anchor["title"], "at": list(anchor["at"]), "size": list(anchor["size"])}

        # Native opening poses are setup, not a profiled workload.
        time.sleep(NATIVE_SETTLE)
        self._start_profile()
        try:
            self._close(identifiers)
            # Wait for the configured close plus the next natural refresh and query drain.
            time.sleep(CONSTRUCTION_SECONDS + .05)
        finally:
            profile = self._stop_profile()
        close_row = self._profile_row(profile, "window", "construction", "close")
        setup_cpu = close_row["cpu"]["setup"]["thread_cpu"]
        setup_wall = close_row["cpu"]["setup"]["wall"]
        setup_samples = setup_cpu.get("samples", 0)
        if setup_samples != self.windows or setup_wall.get("samples") != self.windows:
            raise RuntimeError(f"construction.close setup sample count mismatch: "
                               f"thread_cpu={setup_samples}, wall={setup_wall.get('samples')}, "
                               f"expected={self.windows}")
        update = close_row["cpu"]["update"]["thread_cpu"]
        draw = close_row["cpu"]["draw"]["thread_cpu"]
        frames = close_row["drawn_frames"]
        if (frames < 2 or update["samples"] < self.windows
                or draw["samples"] <= (self.windows - 1) * frames):
            raise RuntimeError("windows.many did not observe the concurrent native close load")
        remaining = {entry["title"] for entry in self.clients()}
        if remaining != {"PaperEvidence-9000"}:
            raise RuntimeError(f"unexpected windows after close: {sorted(remaining)}")
        closed_windows = [title for title in titles if title not in remaining]
        semantic = {
            "scenario": "windows.many",
            "topology": self._topology(outputs),
            "windows": geometry,
            "anchor": anchor_geometry,
            "closed_windows": closed_windows,
            "construction_close_setup_samples": setup_samples,
        }
        # These are sums of actual scoped CPU observations, divided by the number
        # of output frames that presented Construction. The frame count is not fixed.
        cpu_ms = sum(phase["mean_ms"] * phase["samples"] for phase in (update, draw))
        return self._receipt(semantic, profile, started,
                             "Construction close update+draw thread CPU per drawn output frame",
                             cpu_ms * 1_000_000 / frames)

    def _screenshot(self, region):
        from io import BytesIO
        try:
            from PIL import Image
        except ImportError as error:
            raise RuntimeError("native visual benchmark witnesses require Pillow") from error
        if shutil.which("grim") is None:
            raise RuntimeError("native visual benchmark witnesses require grim")

        x, y, width, height = region
        image = sp.run(["grim", "-g", f"{x},{y} {width}x{height}", "-t", "ppm", "-"],
                       env=self.scene_env, capture_output=True, check=True, timeout=5).stdout
        with Image.open(BytesIO(image)) as decoded:
            return decoded.convert("RGB").tobytes()

    def _burn(self) -> dict:
        started = time.perf_counter_ns()
        if not self.burn_ready:
            wallpaper = self._spawn("wallpaper",
                                    [sys.executable, str(ROOT / "tests/nested/burn_evidence.py"), "--wallpaper"],
                                    self.scene_env)
            self._until(lambda: "burn-evidence" in self.hc("-j", "layers"), wallpaper, "Burn wallpaper")
            self.configure('workspace={enabled=false},window={style="burn",open={enabled=false},'
                           f'close={{enabled=true,options={{burn={{duration={BURN_SECONDS * 1000},'
                           'closing_style="consume",debris_direction="wind"}}},'
                           'options={burn={ember_density=1,fragment_density=1,wind_strength=800,'
                           'scorch_lifetime=.05,healing_duration=.11}}}')
            self.burn_ready = True
        self.configure("window={enabled=true}")
        outputs = self.monitors()
        if len(outputs) != 1:
            raise RuntimeError("burn.close requires one private output")
        ident, title = 20_000, "PaperEvidence-20000"
        self._open([ident], size=(640, 400))
        time.sleep(NATIVE_SETTLE)
        fixtures = [c for c in self.clients() if c["title"] == title and c["mapped"]]
        if len(fixtures) != 1 or fixtures[0]["size"] != [640, 400]:
            raise RuntimeError("burn.close fixture lacks the expected native geometry")
        fixture = fixtures[0]
        x, y = fixture["at"]
        right = x + fixture["size"][0] + 14
        # Outside the sheet and the scorch field: the close front, native
        # fadeout and wallpaper char cannot add warm pixels here.
        region = (right, y - 10, min(220, outputs[0]["width"] - right), fixture["size"][1] + 40)
        pristine = self._screenshot(region)
        self._start_profile()
        try:
            close_started = time.monotonic()
            self._close([ident])
            ember_visible = False
            for offset in (.32, .53):
                time.sleep(max(0, close_started + offset - time.monotonic()))
                current = self._screenshot(region)
                hot_pixels = sum(
                    current[i] >= pristine[i] + 28 and current[i] > current[i + 1] + 24
                    and current[i + 1] > current[i + 2] + 6 for i in range(0, len(current), 3))
                ember_visible |= hot_pixels >= 4
            time.sleep(max(0, close_started + BURN_SECONDS + .10 - time.monotonic()))
        finally:
            profile = self._stop_profile()
        remaining = self.clients()
        # The scenario measures active combustion and detached debris, not
        # idle wallpaper recovery. Clear the remaining tail before reuse.
        self.configure("window={enabled=false}")
        close = self._profile_row(profile, "window", "burn", "close")
        ground = self._profile_row(profile, "window", "burn", "shared")
        if (remaining or close["cpu"]["setup"]["thread_cpu"]["samples"] != 1 or
                close["drawn_frames"] < 2 or not (close["gpu"]["samples"] and ground["gpu"]["samples"])
                or not ember_visible):
            raise RuntimeError("burn.close did not witness a native close, shared ground and detached embers")
        semantic = {"scenario": "burn.close", "topology": self._topology(outputs),
                    "window": {key: fixture[key] for key in ("title", "at", "size")},
                    "mapped_after": [c["title"] for c in remaining],
                    "native_burn_setups": close["cpu"]["setup"]["thread_cpu"]["samples"],
                    "detached_embers_visible": ember_visible}
        return self._receipt(semantic, profile, started, "Burn close mean scoped GPU time per sample",
                             close["gpu"]["mean_ms"] * 1_000_000)

    def _prepare_workspace(self, two_outputs: bool, duration: float):
        layout = ('planet_layout="all"' if two_outputs else
                  'planet_layout="monitor",planet_separation=2.5,solar_background_scale=1')
        self.configure('window={enabled=false},workspace={enabled=false,style="planet_hop",'
                       f'options={{planet_hop={{duration={duration * 1000},{layout},live_boundary=0.22,'
                       'stars_enabled=false,clouds_enabled=false,atmosphere_enabled=false,'
                       'lighting_enabled=false,depth_enabled=false}}}')
        primary = self.primary["name"]
        for workspace in (1, 2, 3):
            if not two_outputs and workspace == 3:
                # Only the neighbour carries these saturated purple marks.
                marks = [([71, 13, 250], (column + 1) / 9, (line + 1) / 7)
                         for column in range(8) for line in range(6)]
                self._open([30_003], size=(600, 400), workspace=3, monitor=primary,
                           construction="dark", evidence_marks=marks)
            else:
                self._open([30_000 + workspace], workspace=workspace, monitor=primary)
        routes = [(primary, 1, 2)]
        if two_outputs:
            self.hc("eval", f'hl.monitor({{output="{SECONDARY}",mode="1280x720@60",position="1280x0",scale=1,bitdepth=8}})')
            self.hc("output", "create", "wayland", SECONDARY)
            self._until(lambda: any(m["name"] == SECONDARY for m in self.monitors()),
                        self.compositor, "second private output")
            for workspace in (4, 5):
                self._open([30_000 + workspace], workspace=workspace, monitor=SECONDARY)
            routes.append((SECONDARY, 4, 5))
        self.workspace_routes = routes

    def _workspace(self, two_outputs: bool) -> dict:
        started = time.perf_counter_ns()
        duration = CAPTURE_SECONDS if two_outputs else PLANET_SECONDS
        if self.workspace_routes is None:
            self._prepare_workspace(two_outputs, duration)
        routes = self.workspace_routes
        for monitor, source, _ in routes:
            self._focus_workspace(monitor, source)
        before = self.monitors()
        if {m["name"]: m["activeWorkspace"]["id"] for m in before} != {m: a for m, a, _ in routes}:
            raise RuntimeError("workspace benchmark source topology does not match the fixture")
        windows = sorted((client for client in self.clients() if client["mapped"]), key=lambda c: c["title"])
        expected = {workspace: before[0]["id"] for workspace in (1, 2, 3)}
        if two_outputs:
            expected.update({workspace: before[1]["id"] for workspace in (4, 5)})
        if {c["workspace"]["id"]: c["monitor"] for c in windows} != expected or len(windows) != len(expected):
            raise RuntimeError("workspace benchmark windows are missing or on unexpected outputs")
        if not two_outputs:
            # The fixture reset is a short native slide, not part of the hop.
            time.sleep(NATIVE_SETTLE)
            if self._purple_pixels():
                raise RuntimeError("planet.handoff source presents the neighbour's exclusive marker")

        self.configure("workspace={enabled=true}")
        # Keep both outputs live, but profile their turns separately. The live
        # profiler only provides a style-wide resource high-water mark.
        profiles, gpu_means = [], []
        for output, (_, _, destination) in zip(before, routes):
            profile, gpu_mean = self._workspace_turn(output, destination, duration, not two_outputs)
            profiles.append(profile)
            gpu_means.append(gpu_mean)
        after = self.monitors()
        if {m["name"]: m["activeWorkspace"]["id"] for m in after} != {m: b for m, _, b in routes}:
            raise RuntimeError("workspace benchmark failed to reach the native destination topology")
        if not two_outputs and self._purple_pixels():
            raise RuntimeError("planet.handoff did not return to the unmarked native destination")
        self.configure("workspace={enabled=false}")

        scope = "Planet Hop mean scoped GPU duration per sample"
        if two_outputs:
            scope += " (equal weights per output)"
        semantic = {
            "scenario": "capture.blend" if two_outputs else "planet.handoff",
            "topology": self._topology(after),
            "before": [(m["name"], m["activeWorkspace"]["id"]) for m in before],
            "after": [(m["name"], m["activeWorkspace"]["id"]) for m in after],
            "windows": [{key: c[key] for key in ("title", "at", "size")} | {"workspace": c["workspace"]["id"]}
                        for c in windows],
            "presented": [m for m, _, _ in routes],
            "live_preview_and_stale_capture": [m for m, _, _ in routes],
            **({} if two_outputs else {"textured_neighbours": [windows[-1]["title"]]}),
        }
        return self._receipt(semantic, {"outputs": profiles}, started, scope,
                             sum(gpu_means) * 1_000_000 / len(gpu_means))

    def _purple_pixels(self) -> int:
        pixels = self._screenshot(PLANET_MARK_REGION)
        return sum(pixels[i + 2] > pixels[i + 1] + 55 and pixels[i] > pixels[i + 1] + 14
                   for i in range(0, len(pixels), 3))

    def _workspace_turn(self, output: dict, destination: int, duration: float, require_texture: bool):
        monitor = output["name"]
        log = self.root / "hyprland.log"
        with log.open("rb") as stream:
            stream.seek(0, os.SEEK_END)
            lines = []

            def landed():
                lines.extend(line.decode(errors="replace").rstrip() for line in stream.readlines())
                return any("[hyprimate] workspace landed monitor=" + monitor in line for line in lines)

            self._start_profile()
            try:
                first = time.monotonic()
                self._focus_workspace(monitor, destination)
                if require_texture:
                    for offset in (.24, .36):
                        time.sleep(max(0, first + offset - time.monotonic()))
                        if self._purple_pixels() >= 8:
                            break
                    else:
                        raise RuntimeError("planet.handoff did not present the neighbour's live window texture")
                self._until(landed, self.compositor, f"Planet Hop native landing on {monitor}", duration + 1.0)
            finally:
                profile = self._stop_profile()

        accepted = [line for line in lines if "[hyprimate] workspace request " in line
                    and f"monitor={monitor} " in line and " accepted=true " in line]
        draws = [line for line in lines if "[hyprimate] workspace presented " in line
                 and f"monitor={monitor} " in line and " style=planet_hop" in line]
        if len(accepted) != 1 or len(draws) != 1:
            raise RuntimeError(f"no unambiguous Planet Hop request and presentation on {monitor}")
        request, draw = accepted[0].split(), draws[0].split()
        for request_key, draw_key in (("before=", "source="), ("after=", "destination=")):
            identity = next(field.removeprefix(request_key) for field in request if field.startswith(request_key))
            if f"{draw_key}{identity}" not in draw:
                raise RuntimeError(f"Planet Hop presented the wrong composition on {monitor}")

        row = self._profile_row(profile, "workspace", "planet_hop", "transition")
        gpu = row["gpu"]
        if row["drawn_frames"] < 3 or not gpu["samples"] or profile["gpu_disjoint_events"]:
            raise RuntimeError(f"Planet Hop has no valid GPU observations on {monitor}")
        # This 8-bit surface turn owns up to eleven full-size targets before
        # blending. The stale endpoint adds four; a live preview contributes
        # a 384px thumbnail in addition to any 256px wallpaper fallback.
        full_size = output["width"] * output["height"] * 4
        preview_size = 384 * round(384 * output["height"] / output["width"]) * 4
        peak = row["memory"]["known_gpu_resource_peak_bytes"]
        full_targets, thumbnail_bytes = divmod(peak, full_size)
        if not 14 <= full_targets <= 15 or thumbnail_bytes < preview_size:
            raise RuntimeError(f"no live preview and stale capture overlap on {monitor}: {peak} bytes")
        return {"output": monitor, "profile": profile}, gpu["mean_ms"]

    def close(self) -> None:
        for process in reversed(self.processes):
            # Signal the group even when the launcher exited before its children.
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=2)
            except ProcessLookupError:
                process.wait()
            except sp.TimeoutExpired:
                pass
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        self.temp.cleanup()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, *_):
        if exc_type is not None:
            self._print_logs()
        self.close()
