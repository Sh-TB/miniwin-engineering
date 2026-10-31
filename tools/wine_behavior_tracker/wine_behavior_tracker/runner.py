"""Workflow runner: spec-driven, isolated, honest.

Run directory (never overwritten):
    artifacts/wine-tracker/runs/<run-id>/
        run.json          metadata (exact cmd, env, hashes, times, status)
        raw/              captured stdout/stderr/relay logs
        parsed/           events.jsonl (+ parse stats)
        snapshots/        before/after fs snapshots (sha256 manifest)
        screenshots/      checkpointed captures (degraded honestly if no X)
        checkpoints/      per-step checkpoint records
        reports/          markdown report
        manifest.sha256   hashes of every artifact in this dir
"""
import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

from .schema import EventWriter, now_iso
from . import relay_parse

REPO = Path(__file__).resolve().parents[3]          # .../miniwin-engineering
ARTIFACTS = REPO / "artifacts" / "wine-tracker" / "runs"


def sha256_file(p, bufsize=1 << 20):
    h = hashlib.sha256()
    with open(p, "rb") as fh:
        while True:
            b = fh.read(bufsize)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def snapshot_dir(root, out_path):
    """SHA-256 snapshot of every regular file under root (bounded: follows no
    symlinks; records unreadable/changing files explicitly)."""
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    entries = {}
    root = Path(root)
    if not root.exists():
        out_path.write_text("")
        return entries
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if not os.path.islink(os.path.join(dirpath, d))]
        for name in filenames:
            full = Path(dirpath) / name
            rel = str(full.relative_to(root))
            try:
                if full.is_symlink() or not full.is_file():
                    entries[rel] = {"kind": "special"}
                    continue
                st = full.stat()
                entries[rel] = {"size": st.st_size, "sha256": sha256_file(full)}
            except OSError as e:
                entries[rel] = {"error": str(e)}
    with open(out_path, "w") as fh:
        json.dump(entries, fh, indent=1, sort_keys=True)
    return entries


def diff_snapshots(before, after):
    added = sorted(set(after) - set(before))
    removed = sorted(set(before) - set(after))
    changed = sorted(k for k in set(before) & set(after)
                     if before[k] != after[k]
                     and "sha256" in before.get(k, {}) and "sha256" in after.get(k, {}))
    return {"added": added, "removed": removed, "changed": changed}


def write_manifest(run_dir):
    """manifest.sha256: hash of every file under run_dir (excluding itself)."""
    run_dir = Path(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)
    lines = []
    for dirpath, dirnames, filenames in os.walk(run_dir):
        dirnames[:] = [d for d in dirnames if d != "screenshots" or True]
        for name in sorted(filenames):
            full = Path(dirpath) / name
            rel = full.relative_to(run_dir)
            if str(rel) == "manifest.sha256":
                continue
            try:
                lines.append(f"{sha256_file(full)}  {rel}")
            except OSError:
                lines.append(f"UNREADABLE  {rel}")
    (run_dir / "manifest.sha256").write_text("\n".join(sorted(lines)) + "\n")
    return run_dir / "manifest.sha256"


def load_spec(spec_path):
    import yaml
    with open(spec_path) as fh:
        return yaml.safe_load(fh)


def new_run_dir(spec_name):
    run_id = f"{spec_name}-{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}"
    run_dir = ARTIFACTS / run_id
    if run_dir.exists():  # never overwrite
        raise FileExistsError(f"run dir already exists: {run_dir}")
    for sub in ("raw", "parsed", "snapshots", "screenshots", "checkpoints", "reports"):
        (run_dir / sub).mkdir(parents=True, exist_ok=True)
    return run_id, run_dir


def resolve_engine(spec, run_dir):
    """Return (engine, cmd_prefix_fn, env) for 'wine' or 'miniwin'."""
    engine = spec["engine"]
    env = os.environ.copy()
    if engine == "wine":
        wine = spec.get("wine_path") or os.environ.get(
            "WINE_TRACKER_WINE", "/home/z/my-project/tools/wine-10.0")
        launcher = Path(wine) / "bin" / (spec.get("wine_bin") or "wine64")
        if not launcher.exists():
            return None, None, None
        prefix = spec.get("prefix")
        if spec.get("disposable_prefix", True):
            prefix = str(run_dir / "prefix")
        env["WINEPREFIX"] = str(prefix)
        env.setdefault("WINEDEBUG", spec.get("winedebug", "-all"))
        return engine, [str(launcher)], env
    else:  # miniwin
        loader = spec.get("loader") or str(REPO / "minwin_loader")
        if not Path(loader).exists():
            return None, None, None
        env["MINWIN_RESULTS_DIR"] = str(run_dir / "miniwin-results")
        return engine, [loader], env


def run_spec(spec_path, only_checkpoints=None):
    spec = load_spec(spec_path)
    spec_name = spec["name"]
    run_id, run_dir = new_run_dir(spec_name)
    t0 = time.time()

    engine, prefix_cmd, env = resolve_engine(spec, run_dir)
    meta = {
        "run_id": run_id,
        "spec": spec_name,
        "engine": engine,
        "engine_detail": spec.get("engine_detail", ""),
        "start": now_iso(),
        "cwd": str(run_dir),
        "env_keys": sorted(k for k in env if k.startswith(("WINE", "MINWIN", "DISPLAY"))) if env else [],
        "app": spec.get("app"),
        "checkpoints_planned": [c["id"] for c in spec.get("checkpoints", [])],
        "timeout_s": spec.get("timeout_s", 60),
    }
    (run_dir / "run.json").write_text(json.dumps(meta, indent=2))

    if engine is None:
        meta["status"] = "BLOCKED"
        meta["blocked_reason"] = "engine launcher not found (wine or miniwin)"
        meta["finish"] = now_iso()
        (run_dir / "run.json").write_text(json.dumps(meta, indent=2))
        return run_id, run_dir, meta

    with EventWriter(run_dir / "parsed" / "events.jsonl", run_id,
                     capture_source=f"{engine}-runner") as writer:
        writer.emit("run_meta", **{k: meta[k] for k in
                                   ("spec", "engine", "timeout_s", "app")},
                    confidence="derived")
        writer.emit("process_start", process=spec["app"]["executable"],
                    arguments={"sha256": spec["app"].get("sha256")},
                    confidence="derived")

        # Disposable prefix needs one-time init (wineboot); record honestly.
        # NOTE (measured): wineboot -i may exit 53 while still producing a
        # USABLE prefix (syswow64/rundll32 import noise on this deb layout);
        # usability is decided by probing system32 contents, not by rc alone.
        if engine == "wine" and spec.get("disposable_prefix", True):
            init = subprocess.run(prefix_cmd + ["wineboot", "-i"],
                                  env=env, capture_output=True, text=True,
                                  timeout=spec.get("timeout_s", 60))
            sys32 = Path(env["WINEPREFIX"]) / "drive_c" / "windows" / "system32"
            n_files = len(list(sys32.glob("*"))) if sys32.exists() else 0
            meta["prefix_init"] = {"exit_code": init.returncode, "system32_files": n_files}
            (run_dir / "raw" / "wineboot.log").write_text(
                (init.stdout or "") + "\n--stderr--\n" + (init.stderr or ""))
            if init.returncode != 0 and n_files < 100:
                meta["status"] = "BLOCKED"
                meta["blocked_reason"] = (f"disposable prefix init unusable "
                                          f"(wineboot rc={init.returncode}, "
                                          f"system32 files={n_files})")
                meta["finish"] = now_iso()
                (run_dir / "run.json").write_text(json.dumps(meta, indent=2, default=str))
                writer.emit("parser_error",
                            reason="prefix init failed — engine unusable in this env",
                            confidence="derived")
                return run_id, run_dir, meta
            # Measured repair (recorded, not silent): this deb-extract wine
            # build ships no zlib1.dll, but USER32.dll imports it; without it
            # every guest dies with status 0xc0000135 (exit 53). Seed the
            # fresh prefix system32 with the x64 zlib1.dll from the same
            # toolchain. The main prefix needed the identical fix manually.
            zlib_src = Path(spec.get("wine_path", "/home/z/my-project/tools/wine-10.0")) \
                / "usr/lib/x86_64-linux-gnu/wine/x86_64-windows/zlib1.dll"
            zlib_dst = sys32 / "zlib1.dll"
            if zlib_src.exists() and not zlib_dst.exists():
                import shutil as _sh
                _sh.copy(zlib_src, zlib_dst)
                meta["prefix_init"]["patched"] = f"seeded {zlib_src.name}"

        workdir = run_dir / "work"
        workdir.mkdir(exist_ok=True)
        # Stage fixtures (deterministic bytes; hash recorded)
        fixture_hashes = {}
        for name, content in (spec.get("fixtures") or {}).items():
            data = content.encode("utf-8") if isinstance(content, str) else bytes(content)
            (workdir / name).write_bytes(data)
            fixture_hashes[name] = hashlib.sha256(data).hexdigest()
        if fixture_hashes:
            meta["fixture_hashes"] = fixture_hashes

        # Pre-run snapshot of the workdir
        before = snapshot_dir(workdir, run_dir / "snapshots" / "before.json")

        checkpoints = spec.get("checkpoints", [])
        if only_checkpoints:
            checkpoints = [c for c in checkpoints if c["id"] in only_checkpoints]
        for ck in checkpoints:
            ck_rec = execute_checkpoint(spec, ck, run_dir, workdir, prefix_cmd, env, writer)
            (run_dir / "checkpoints" / f"{ck['id']}.json").write_text(
                json.dumps(ck_rec, indent=2, default=str))

        # Post-run snapshot + diff
        after = snapshot_dir(workdir, run_dir / "snapshots" / "after.json")
        diff = diff_snapshots(before, after)
        for rel in diff["added"]:
            writer.emit("fs_effect", path=rel, effect="added", confidence="derived")
        for rel in diff["changed"]:
            writer.emit("fs_effect", path=rel, effect="changed", confidence="derived")
        (run_dir / "snapshots" / "diff.json").write_text(json.dumps(diff, indent=2))

        # Registry effects: export the disposable prefix's .reg files before
        # the prefix is reclaimed (each full prefix is ~700MB; keeping one
        # per run filled the 10GB sandbox disk and killed later runs with
        # ENOSPC — measured 2026-10-30). Registry captures stay.
        if engine == "wine" and spec.get("disposable_prefix", True):
            reg_dir = run_dir / "snapshots" / "registry"
            reg_dir.mkdir(exist_ok=True)
            for reg in ("system.reg", "user.reg", "userdef.reg"):
                src = Path(env["WINEPREFIX"]) / reg
                if src.exists():
                    shutil.copy(src, reg_dir / reg)
            prefix_dir = Path(env["WINEPREFIX"])
            if prefix_dir.exists():
                shutil.rmtree(prefix_dir, ignore_errors=True)

        writer.emit("process_exit", exit_status=meta.get("status"),
                    confidence="derived")
        meta["duration_s"] = round(time.time() - t0, 2)
        meta["finish"] = now_iso()
        (run_dir / "run.json").write_text(json.dumps(meta, indent=2, default=str))

    write_manifest(run_dir)
    return run_id, run_dir, meta


def execute_checkpoint(spec, ck, run_dir, workdir, prefix_cmd, env, writer):
    """Run one checkpoint's argv; record honestly; verify assertions.

    ck["host"]=true runs argv directly on the host (no engine prefix) —
    used for verification steps that must not depend on the engine.
    """
    rec = {"id": ck["id"], "desc": ck.get("desc", ""), "start": now_iso(),
           "host": bool(ck.get("host"))}
    writer.emit("workflow_checkpoint", arguments={"id": ck["id"], "phase": "start"},
                confidence="derived")
    argv = list(ck["argv"])
    if not ck.get("host"):
        argv = prefix_cmd + [a.format(workdir=workdir, run_dir=run_dir) for a in argv]
    else:
        argv = [a.format(workdir=workdir, run_dir=run_dir) for a in argv]
    rec["argv"] = argv
    rec["engine_used"] = "none (host)" if ck.get("host") else spec["engine"]
    timeout = ck.get("timeout_s", spec.get("timeout_s", 60))
    try:
        t0 = time.time()
        proc = subprocess.run(argv, cwd=str(workdir), env=env,
                              capture_output=True, text=True, timeout=timeout)
        rec["exit_code"] = proc.returncode
        rec["duration_s"] = round(time.time() - t0, 2)
        (run_dir / "raw" / f"stdout_{ck['id']}.log").write_text(proc.stdout or "")
        (run_dir / "raw" / f"stderr_{ck['id']}.log").write_text(proc.stderr or "")
        rec["stdout_bytes"] = len(proc.stdout or "")
        rec["stderr_bytes"] = len(proc.stderr or "")
        if ck.get("capture_stdout_head"):
            rec["stdout_head"] = (proc.stdout or "")[:ck["capture_stdout_head"]]
    except subprocess.TimeoutExpired as e:
        rec["timed_out"] = True
        rec["exit_code"] = None
        rec["stdout_bytes"] = len((e.stdout or b"") if isinstance(e.stdout, bytes) else (e.stdout or ""))
        (run_dir / "raw" / f"stdout_{ck['id']}.log").write_text(
            e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or ""))
    except OSError as e:
        rec["infrastructure_failure"] = str(e)

    # Verification: python expressions evaluated with rec/workdir in scope
    rec["verifications"] = []
    for v in ck.get("verify", []):
        name, expr = v["name"], v["expr"]
        scope = {"rec": rec, "workdir": str(workdir), "run_dir": str(run_dir),
                 "sha256_file": sha256_file,
                 "Path": Path, "os": os, "json": json, "re": __import__("re")}
        try:
            ok = bool(eval(expr, {"__builtins__": __builtins__}, scope))  # noqa: S307
            rec["verifications"].append({"name": name, "ok": ok})
        except Exception as e:
            rec["verifications"].append({"name": name, "ok": False, "error": str(e)})
    rec["pass"] = (rec.get("exit_code") in (0, ck.get("expected_exit", [0]))
                   or ck.get("allow_any_exit", False)) \
        and all(v["ok"] for v in rec["verifications"]) if rec["verifications"] \
        else rec.get("exit_code") == 0 or ck.get("allow_any_exit", False)
    # Distinguish assertion failure from infrastructure failure
    if rec.get("infrastructure_failure"):
        rec["outcome"] = "infrastructure_failure"
    elif rec.get("timed_out"):
        rec["outcome"] = "timeout"
    elif not rec["pass"]:
        rec["outcome"] = "assertion_failure"
    else:
        rec["outcome"] = "pass"
    rec["finish"] = now_iso()
    writer.emit("workflow_checkpoint", arguments={"id": ck["id"], "phase": "end",
                                                  "outcome": rec["outcome"],
                                                  "exit_code": rec.get("exit_code")},
                confidence="derived")
    return rec
