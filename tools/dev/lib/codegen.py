"""Baseline-versus-candidate binary comparison: sections, symbols, exports, per-function code and stack frames.

Generated-code differences are facts, not verdicts. A bigger stack frame or different instruction count says nothing
about speed by itself; the report only calls something a regression when benchmark evidence shows one.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
import tarfile
import io
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

from . import bench as benchlib
from .common import (BUILD_ROOT, POLICY_FILE, REPORT_ROOT, ROOT, Report, ToolError, build, build_dir,
                     configure, require, run, tail, tool_path)

DEFAULT_HOT = [
    r"hyprimate::resolve_edge_frame", r"hyprimate::resolve_close_edge_frame", r"hyprimate::EdgeTransition::",
    r"hyprimate::LiquidMesh::", r"hyprimate::Bubble(Motion|Formation|Foam|Wake)?::", r"hyprimate::bubble_",
    r"hyprimate::Smoke::", r"hyprimate::project_sheet", r"hyprimate::ClothFlutter::", r"hyprimate::Swarm::",
    r"hyprimate::PaperCrumple::",
]


def tools() -> dict[str, str]:
    return {name: require(name, "pacman -S llvm") for name in ("llvm-objdump", "llvm-readelf", "llvm-nm")}


# ----------------------------------------------------------------------------------------- ELF facts
def sections(path: Path, scrub: tuple[bytes, ...] = ()) -> dict[str, dict]:
    """Section sizes and content hashes. `scrub` strings (the two build/source roots, equal length) are blanked first
    so embedded paths such as HYPRIMATE_SPRITE_DIR do not read as code differences."""
    out = subprocess.run(["llvm-readelf", "-S", "-W", path], capture_output=True, text=True, check=True).stdout
    data = path.read_bytes()
    for needle in scrub:
        data = data.replace(needle, b"#" * len(needle))
    result: dict[str, dict] = {}
    for match in re.finditer(r"\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\S+\s+(\S*)", out):
        name, kind, _addr, offset, size, _ = match.groups()
        offset, size = int(offset, 16), int(size, 16)
        digest = None
        if kind not in ("NOBITS", "NULL") and size:
            digest = hashlib.sha256(data[offset:offset + size]).hexdigest()[:16]
        result[name] = {"size": size, "sha": digest, "type": kind}
    return result


def architecture(path: Path) -> str:
    out = subprocess.run(["llvm-readelf", "-h", path], capture_output=True, text=True, check=True).stdout
    machine = re.search(r"Machine:\s+(.*)", out)
    return machine.group(1).strip() if machine else "unknown"


def needed(path: Path) -> list[str]:
    out = subprocess.run(["llvm-readelf", "-d", path], capture_output=True, text=True, check=True).stdout
    return sorted(re.findall(r"\(NEEDED\)\s+Shared library: \[(.*?)\]", out))


def text_symbols(path: Path) -> list[dict]:
    mangled = subprocess.run(["llvm-nm", "-S", "-n", "--defined-only", path], capture_output=True, text=True, check=True).stdout
    demangled = subprocess.run(["llvm-nm", "-S", "-n", "-C", "--defined-only", path], capture_output=True, text=True, check=True).stdout
    rows = []
    for m_line, d_line in zip(mangled.splitlines(), demangled.splitlines()):
        m, d = m_line.split(None, 3), d_line.split(None, 3)
        if len(m) < 4 or len(d) < 4 or m[2] not in "TtWw":
            continue
        rows.append({"mangled": m[3], "name": d[3], "size": int(m[1], 16), "addr": int(m[0], 16)})
    return rows


def dynamic_exports(path: Path) -> set[str]:
    out = subprocess.run(["llvm-nm", "-D", "--defined-only", "-C", path], capture_output=True, text=True, check=False).stdout
    return {line.split(None, 2)[2] for line in out.splitlines() if len(line.split(None, 2)) == 3}


# ------------------------------------------------------------------------------------ disassembly
ADDRESS_ANNOTATION = re.compile(r" <[^>]*\+0x[0-9a-f]+>")
NOP = re.compile(r"^(nop|data16|xchg\s+%ax,\s*%ax)")


def normalise(lines: list[str]) -> list[str]:
    out = []
    for line in lines:
        text = line.strip()
        if not text:
            continue
        if text.startswith("<") and text.endswith(">:"):
            if text.startswith("<L"):
                out.append(text)
            continue
        if NOP.match(text):
            continue
        out.append(ADDRESS_ANNOTATION.sub("", text))
    return out


def stack_frame(instructions: list[str], arch: str) -> dict:
    """Frame size from the prologue. Only reported when the pattern is unambiguous."""
    pushes, sub = 0, None
    if "AArch64" in arch:
        for text in instructions[:12]:
            match = re.match(r"sub\s+sp,\s*sp,\s*#(0x[0-9a-f]+|\d+)", text)
            if match:
                sub = int(match.group(1), 0)
                break
        return {"stack_sub": sub, "reliable": sub is not None}
    for text in instructions[:25]:
        if text.startswith("pushq"):
            pushes += 1
        match = re.match(r"subq\s+\$(0x[0-9a-f]+|\d+),\s*%rsp", text)
        if match:
            sub = int(match.group(1), 0)
            break
    realigned = any(re.match(r"andq\s+\$-\d+,\s*%rsp", t) for t in instructions[:25])
    dynamic = any("%rsp" in t and t.startswith("subq") and "$" not in t for t in instructions)
    return {"stack_sub": sub, "pushes": pushes, "reliable": not realigned and not dynamic}


def disassemble(path: Path, funcs: list[dict], arch: str) -> dict[str, dict]:
    result: dict[str, dict] = {}
    for start in range(0, len(funcs), 400):
        chunk = funcs[start:start + 400]
        out = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn", "--no-addresses", "--symbolize-operands",
                              "--disassemble-symbols=" + ",".join(f["mangled"] for f in chunk), path],
                             capture_output=True, text=True, check=False).stdout
        by_mangled = {f["mangled"]: f for f in chunk}
        current, buffer = None, []
        def flush():
            if current is None:
                return
            body = normalise(buffer)
            real = [l for l in body if not l.startswith("<L")]
            result[by_mangled[current]["name"] + f" @{by_mangled[current]['mangled']}"] = {
                "name": by_mangled[current]["name"], "bytes": by_mangled[current]["size"], "instructions": len(real),
                "hash": hashlib.sha256("\n".join(body).encode()).hexdigest()[:16],
                "mnemonics": Counter(l.split()[0] for l in real),
                **stack_frame(real, arch)}
        for line in out.splitlines():
            header = re.match(r"^<(.+)>:$", line)
            if header and header.group(1) in by_mangled:
                flush()
                current, buffer = header.group(1), []
                continue
            if current is not None:
                buffer.append(line)
        flush()
    return result


# ---------------------------------------------------------------------------------------- compare
@dataclass
class Comparison:
    identical_bytes: bool = False
    size: dict = field(default_factory=dict)
    sections: list = field(default_factory=list)
    text_identical: bool = False
    exports_added: list = field(default_factory=list)
    exports_removed: list = field(default_factory=list)
    needed_changed: dict = field(default_factory=dict)
    symbols_added: list = field(default_factory=list)
    symbols_removed: list = field(default_factory=list)
    functions_changed: list = field(default_factory=list)
    functions_identical: int = 0


def hot_patterns() -> list[re.Pattern]:
    patterns = DEFAULT_HOT
    if POLICY_FILE.exists():
        patterns = json.loads(POLICY_FILE.read_text()).get("hot_functions", DEFAULT_HOT)
    return [re.compile(p) for p in patterns]


def observed_notes() -> list[dict]:
    path = Path(__file__).resolve().parents[1] / "baselines" / "codegen-observations.json"
    return json.loads(path.read_text())["observations"] if path.exists() else []


def compare_binaries(base: Path, cand: Path, function_filter: str,
                     scrub: tuple[bytes, ...] = ()) -> tuple[Comparison, list[dict]]:
    tools()
    comparison = Comparison()
    comparison.identical_bytes = base.read_bytes() == cand.read_bytes()
    comparison.size = {"baseline": base.stat().st_size, "candidate": cand.stat().st_size}
    sa, sb = sections(base, scrub), sections(cand, scrub)
    for name in sorted(set(sa) | set(sb)):
        a, b = sa.get(name), sb.get(name)
        if a is None or b is None or a["size"] != b["size"] or a["sha"] != b["sha"]:
            comparison.sections.append({"section": name, "baseline": a and a["size"], "candidate": b and b["size"],
                                        "delta": (b["size"] if b else 0) - (a["size"] if a else 0),
                                        "content_differs": (a or {}).get("sha") != (b or {}).get("sha")})
    ignorable = (".debug", ".note", ".comment", ".symtab", ".strtab", ".shstrtab", ".gnu_debuglink")
    comparison.text_identical = not [s for s in comparison.sections if not s["section"].startswith(ignorable)]
    ea, eb = dynamic_exports(base), dynamic_exports(cand)
    comparison.exports_added, comparison.exports_removed = sorted(eb - ea), sorted(ea - eb)
    na, nb = needed(base), needed(cand)
    if na != nb:
        comparison.needed_changed = {"baseline": na, "candidate": nb}

    arch = architecture(base)
    if architecture(cand) != arch:
        raise ToolError(f"architectures differ: {arch} vs {architecture(cand)}; comparison would be meaningless")
    pattern = re.compile(function_filter)
    fa = [f for f in text_symbols(base) if pattern.search(f["name"])]
    fb = [f for f in text_symbols(cand) if pattern.search(f["name"])]
    comparison.symbols_added = sorted({f["name"] for f in fb} - {f["name"] for f in fa})
    comparison.symbols_removed = sorted({f["name"] for f in fa} - {f["name"] for f in fb})
    da, db = disassemble(base, fa, arch), disassemble(cand, fb, arch)
    hot = hot_patterns()
    changed = []
    for key in sorted(set(da) & set(db)):
        a, b = da[key], db[key]
        if a["hash"] == b["hash"]:
            comparison.functions_identical += 1
            continue
        delta_mn = {m: b["mnemonics"].get(m, 0) - a["mnemonics"].get(m, 0) for m in set(a["mnemonics"]) | set(b["mnemonics"])}
        delta_mn = {m: d for m, d in sorted(delta_mn.items(), key=lambda kv: -abs(kv[1])) if d}
        changed.append({"function": a["name"], "hot": any(p.search(a["name"]) for p in hot),
                        "bytes": [a["bytes"], b["bytes"]], "instructions": [a["instructions"], b["instructions"]],
                        "stack_sub": [a.get("stack_sub"), b.get("stack_sub")],
                        "stack_reliable": bool(a.get("reliable") and b.get("reliable")),
                        "mnemonic_delta": dict(list(delta_mn.items())[:8])})
    comparison.functions_changed = sorted(changed, key=lambda c: (not c["hot"], -abs(c["bytes"][1] - c["bytes"][0])))
    return comparison, observed_notes()


# --------------------------------------------------------------------------------------- command
def materialise(ref: str, label: str) -> Path:
    """Copy a git ref (or the working tree, for WORKTREE) to build/dev/src-<label>.

    Never builds in the checkout itself and never touches its worktree metadata. Only files whose bytes changed
    get a new mtime, so ninja rebuilds exactly what differs (mtimes from git archive would be commit times)."""
    target = BUILD_ROOT / f"src-{label}"
    target.mkdir(parents=True, exist_ok=True)
    wanted: dict[str, bytes] = {}
    if ref == "WORKTREE":
        names = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT,
                               capture_output=True, check=True).stdout.split(b"\0")
        for raw in filter(None, names):
            source = ROOT / raw.decode()
            if source.is_file() and not source.is_symlink():
                wanted[raw.decode()] = source.read_bytes()
    else:
        archive = subprocess.run(["git", "archive", ref], cwd=ROOT, capture_output=True, check=False)
        if archive.returncode:
            raise ToolError(f"git archive {ref} failed: {archive.stderr.decode(errors='replace')}")
        with tarfile.open(fileobj=io.BytesIO(archive.stdout)) as tar:
            for member in tar:
                if member.isfile():
                    wanted[member.name] = tar.extractfile(member).read()
    for existing in [p for p in target.rglob("*") if p.is_file()]:
        if existing.relative_to(target).as_posix() not in wanted:
            existing.unlink()
    for name, content in wanted.items():
        destination = target / name
        if destination.is_file() and destination.read_bytes() == content:
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(content)
    return target


def cmd_compare_codegen(args) -> int:
    report = Report("compare-codegen")
    try:
        tools()
        if args.baseline_bin or args.candidate_bin:
            if not (args.baseline_bin and args.candidate_bin):
                raise ToolError("--baseline-bin and --candidate-bin must be given together")
            base_bin, cand_bin = Path(args.baseline_bin), Path(args.candidate_bin)
            provenance = {"mode": "prebuilt binaries", "note": "caller is responsible for equivalent toolchain and flags"}
            trees = None
            scrub: tuple[bytes, ...] = ()
        else:
            base_src = materialise(args.baseline, "a")
            cand_src = materialise(args.candidate, "b")
            outputs = {}
            for label, short, src in (("baseline", "a", base_src), ("candidate", "b", cand_src)):
                # Same compiler, flags, optimisation and target for both sides. Source and build directories have
                # equally long names, because the plugin embeds HYPRIMATE_SPRITE_DIR and path length shifts layout.
                directory = build_dir("default", f"codegen-{short}")
                flags = (f"-ffile-prefix-map={src}=/src", f"-ffile-prefix-map={directory}=/build")
                directory = configure("default", label=f"codegen-{short}", source=src, cxxflags_extra=flags)
                built = build(directory, ["hyprimate"])
                if built.returncode:
                    raise ToolError(f"{label} build failed:\n" + tail(built.stdout, 30))
                outputs[label] = directory
            base_bin = outputs["baseline"] / "hyprimate" / "hyprimate.so"
            cand_bin = outputs["candidate"] / "hyprimate" / "hyprimate.so"
            provenance = {"mode": "built from sources", "baseline": args.baseline, "candidate": args.candidate,
                          "candidate_includes_uncommitted_changes": args.candidate == "WORKTREE"}
            trees = outputs
            scrub = tuple(str(p).encode() for p in (base_src, cand_src, outputs["baseline"], outputs["candidate"]))
        for binary in (base_bin, cand_bin):
            if not binary.exists():
                raise ToolError(f"binary not found: {binary}")
        comparison, notes = compare_binaries(base_bin, cand_bin, args.functions, scrub)
    except (ToolError, subprocess.CalledProcessError) as error:
        report.add("codegen", "tool_failed", summary=str(error))
        return report.write()

    evidence = None
    if args.run_bench and trees:
        evidence = bench_evidence(trees, args)
    elif args.bench_baseline and args.bench_candidate:
        evidence = benchlib.compare_files(Path(args.bench_baseline), Path(args.bench_candidate))
    verdict, reason = classify(comparison, evidence)
    changed_hot = [c for c in comparison.functions_changed if c["hot"]]
    for c in comparison.functions_changed[:15]:
        mark = "HOT " if c["hot"] else "    "
        print(f"  {mark}{c['function'][:90]}: instr {c['instructions'][0]}->{c['instructions'][1]}, "
              f"bytes {c['bytes'][0]}->{c['bytes'][1]}, stack sub {c['stack_sub'][0]}->{c['stack_sub'][1]}"
              f"{'' if c['stack_reliable'] else ' (unreliable)'}", file=sys.stderr)
    relevant_notes = [n for n in notes if any(n["function"] in c["function"] for c in comparison.functions_changed)]
    abidiff = None
    if tool_path("abidiff"):
        res = run(["abidiff", "--no-added-syms", "--drop-private-types", base_bin, cand_bin],
                  log=REPORT_ROOT / "abidiff.txt", quiet=True)
        abidiff = {"exit": res.returncode, "log": "build/dev/reports/abidiff.txt",
                   "abi_breaking": bool(res.returncode & 8)}
    bloaty = None
    if tool_path("bloaty"):
        res = run(["bloaty", cand_bin, "--", base_bin, "-d", "sections", "-n", "30"], log=REPORT_ROOT / "bloaty.txt", quiet=True)
        bloaty = "build/dev/reports/bloaty.txt"
    status = "fail" if verdict == 4 else "pass" if verdict in (1, 2, 3) else "advisory"
    if comparison.exports_removed or (abidiff and abidiff["abi_breaking"]):
        status = "fail" if status != "fail" else status
    payload = {
        "verdict_class": verdict,
        "verdict": {1: "identical generated code", 2: "different generated code, no regression demonstrated",
                    3: "measurable performance improvement", 4: "measurable performance regression",
                    5: "inconclusive"}[verdict],
        "reason": reason, "provenance": provenance, "identical_bytes": comparison.identical_bytes,
        "code_sections_identical": comparison.text_identical, "size_bytes": comparison.size,
        "section_changes": comparison.sections, "exports_added": comparison.exports_added,
        "exports_removed": comparison.exports_removed, "needed_changed": comparison.needed_changed,
        "symbols_added": comparison.symbols_added[:50], "symbols_removed": comparison.symbols_removed[:50],
        "functions_unchanged": comparison.functions_identical, "functions_changed": comparison.functions_changed,
        "hot_functions_changed": len(changed_hot), "recorded_observations": relevant_notes,
        "benchmark_evidence": evidence, "abidiff": abidiff, "bloaty": bloaty,
    }
    summary = (f"class {verdict}: {payload['verdict']}; {len(comparison.functions_changed)} functions changed "
               f"({len(changed_hot)} hot), {comparison.functions_identical} identical; "
               f"size {comparison.size['baseline']}->{comparison.size['candidate']}")
    report.add("codegen", status, summary=summary, **payload)
    print("\n".join(f"  note: {n['function']}: {n['observation']} [{n['status']}]" for n in relevant_notes), file=sys.stderr)
    return report.write()


def classify(comparison: Comparison, evidence: list[dict] | None) -> tuple[int, str]:
    unchanged_code = comparison.identical_bytes or (comparison.text_identical and not comparison.functions_changed
                                                    and not comparison.symbols_added and not comparison.symbols_removed)
    if unchanged_code:
        return 1, "no code difference after path normalisation"
    if not evidence:
        return 2, "code differs; no benchmark evidence supplied (rerun with --run-bench). A larger frame or more " \
                  "instructions is a recorded fact, not a demonstrated slowdown"
    verdicts = {r["verdict"] for r in evidence}
    if "regression" in verdicts or "behavior-changed" in verdicts:
        return 4, "benchmark regression or changed output: " + ", ".join(
            r["name"] for r in evidence if r["verdict"] in ("regression", "behavior-changed"))
    if "improvement" in verdicts:
        return 3, "benchmark improvement: " + ", ".join(r["name"] for r in evidence if r["verdict"] == "improvement")
    if "inconclusive" in verdicts:
        return 5, "benchmarks inconclusive for: " + ", ".join(r["name"] for r in evidence if r["verdict"] == "inconclusive")
    return 2, "benchmarks show no measurable change"


def bench_evidence(trees: dict[str, Path], args) -> list[dict]:
    """Alternate baseline/candidate runs so drift hits both sides equally, then merge the samples."""
    merged: dict[str, dict[str, dict]] = {"baseline": {}, "candidate": {}}
    binaries = {label: directory for label, directory in trees.items()}
    for label, directory in binaries.items():
        built = build(directory, ["hyprimate-bench"])
        if built.returncode:
            raise ToolError(f"{label} benchmark failed to build (API drift between refs?):\n" + tail(built.stdout, 20))
    out_dir = benchlib.BENCH_DIR
    out_dir.mkdir(parents=True, exist_ok=True)
    for round_ in range(args.rounds):
        for label in ("baseline", "candidate") if round_ % 2 == 0 else ("candidate", "baseline"):
            out = out_dir / f"codegen-{label}-{round_}.json"
            cmd = [benchlib.bench_binary(binaries[label]), "--samples", str(args.samples), "--warmup", "5", "--json", out]
            result = run(cmd, quiet=True)
            if not out.exists():
                raise ToolError(f"{label} benchmark failed:\n" + tail(result.stdout))
            for scenario in json.loads(out.read_text())["scenarios"]:
                slot = merged[label].setdefault(scenario["name"], {**scenario, "ns_per_run": []})
                slot["ns_per_run"].extend(scenario["ns_per_run"])
    files = {}
    for label in merged:
        path = out_dir / f"codegen-{label}.json"
        path.write_text(json.dumps({"scenarios": list(merged[label].values())}))
        files[label] = path
    return benchlib.compare_files(files["baseline"], files["candidate"])
