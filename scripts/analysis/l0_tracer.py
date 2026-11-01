#!/usr/bin/env python3
"""MiniWin L0 tracer — universal Wine Level-0 observation engine.

ONE UNIVERSAL OBSERVATION SYSTEM: any Windows application -> real Wine
behavior -> normalized, attributable, replayable JSONL event streams.

Design (mission Phase 1):
  * Application-independent: driven entirely by an experiment spec (JSON or
    CLI). No per-application code paths.
  * Real Wine only: resolves an actual wine binary (userspace builds fine),
    reports its real --version, and records every capture's raw bytes
    immutably before any parsing.
  * Channel coverage: WINEDEBUG channels captured in SEPARATE disposable
    prefixes so no capture contaminates another's workflow state:
      relay    +relay            (API call/ret, nested, per-thread)
      module   +module+loaddll   (DLL load, sections, relocations, ordinals)
      file     +file             (filesystem operations)
      reg      +reg              (registry operations)
      process  +process          (process creation/termination)
      thread   +thread           (thread creation/termination)
      msg      +message          (window message pump activity)
  * Bounded by default: every raw capture has a byte cap and a wall-clock
    cap; kills the whole wine process group when either fires, and reports
    truncated=True honestly.
  * Pairing: per-thread LIFO stacks; the Call/Ret return addresses are
    VALIDATED — mismatches flag the record, lost inner returns are repaired
    by walking the stack (orphaned frames never mate with a foreign call).
  * No invented data: a missing/malformed retval stays retval=None with
    retval_known=false and a limitation record.
  * Attribution: with the module map from the +module capture, every relay
    event's caller is classified (app / guest-dll / wine-builtin) from its
    return address. Without the map, attribution="unknown" (never guessed).
  * Determinism: same experiment spec + same binary + same channels ->
    same command lines, same prefix lifecycle; repeated-run consistency is
    measured by hashing the first N normalized events (replay_check).

Output layout (raw evidence NEVER rewritten, summaries live beside it):
  <out>/<EXP-ID>/
    experiment.json     config, wine version, binary sha256, caps, facts
    raw/<channel>.log   raw capture bytes (exact, even if truncated)
    events/<channel>.jsonl  normalized events (one JSON object per line)
    summary.md          human-readable Phase-2 report

Usage:
  python3 l0_tracer.py <exe> [args...] --out DIR [--channels relay,module]
      [--wine PATH|--wine-bin PATH] [--timeout 30] [--relay-max-mb 256]
      [--max-events 2000000] [--prefix-mode fresh] [--keep-prefixes]
  or: python3 l0_tracer.py --spec experiment.json
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

TRACER_VERSION = "1.0"

# --- Wine relay grammar (validated against wine-10.1 real captures) ---------
RELAY_CALL_RE = re.compile(
    r'^(?P<tid>[0-9a-fA-F]+):Call\s+(?P<func>.+?)\((?P<args>[^)]*)\)\s+'
    r'ret=(?P<ret>[0-9a-fA-Fx]+)\s*$')
RELAY_RET_RE = re.compile(
    r'^(?P<tid>[0-9a-fA-F]+):Ret\s+(?P<func>.+?)\(\)\s*'
    r'(?:retval=(?P<retval>\S*))?\s*ret=(?P<ret>[0-9a-fA-Fx]+)\s*$')
HEX_TOKEN_RE = re.compile(r'[0-9a-fA-Fx]+')
# Wine 10.1 PE DLL lines carry NO trailing ret= on either kind; separators
# between fields vary (`,' or space). Both tolerated.
PEDLL_RE = re.compile(
    r'^(?P<tid>[0-9a-fA-F]+):(?P<kind>Call|Ret)\s+PE DLL\s*\(\s*'
    r'proc=(?P<proc>[0-9a-fA-Fx]+)[,\s]+'
    r'module=(?P<module>[0-9a-fA-Fx]+)[,\s]+'
    r'L"(?P<name>[^"]*)"[,\s]+'
    r'reason=(?P<reason>[A-Za-z_0-9]+)[,\s]+'
    r'res=(?P<res>[0-9a-fA-Fx]+)\s*\)')
TAIL_RETVAL_RE = re.compile(r'retval=(?P<retval>\S*)')
TAIL_RET_RE = re.compile(r'ret=(?P<ret>[0-9a-fA-Fx]+)')
# Generic debug-channel lines: `tid:trace:channel:text` (also warn/fixme/err)
CHANNEL_RE = re.compile(
    r'^(?P<tid>[0-9a-fA-F]+):(?P<lvl>trace|warn|fixme|err):(?P<ch>[A-Za-z_0-9]+):(?P<text>.*)$')
# +module module map lines
MOD_MAP_RE = re.compile(
    r'map_image_into_view mapping PE file L"([^"]+)" at (0x[0-9a-f]+)-(0x[0-9a-f]+)')
MOD_LOADED_RE = re.compile(r'build_module loaded L"([^"]+)" ([0-9a-f]+) ([0-9a-f]+)')

LIMIT_SAMPLES_MAX = 100
DEFAULT_CHANNEL_CAPS_MB = {
    'relay': 256, 'module': 64, 'file': 64, 'reg': 64,
    'process': 32, 'thread': 32, 'msg': 64,
}


def _clean_retval(token):
    """-> (value, problem). Unknown stays unknown; nothing is fabricated."""
    if token is None or token == '':
        return None, 'missing_retval'
    if not HEX_TOKEN_RE.fullmatch(token):
        return None, 'malformed_retval'
    return token, None


class JsonlWriter:
    def __init__(self, path):
        self.fh = open(path, 'w', encoding='utf-8')
        self.count = 0

    def write(self, obj):
        self.fh.write(json.dumps(obj, ensure_ascii=False, default=str) + '\n')
        self.count += 1

    def close(self):
        self.fh.close()


class RelayNormalizer:
    """Streams one +relay raw log into normalized JSONL events.

    Emits: call_open (with stack depth), call_pair, unmatched_ret,
    pe_dll_event, plus a limitations ledger. When a module map is supplied,
    each event gains `caller_module` / `attribution` (app|guest_dll|wine).
    """

    def __init__(self, out: JsonlWriter, module_map=None):
        self.out = out
        self.module_map = module_map or []
        self.stacks = defaultdict(list)
        self.limitations = defaultdict(lambda: {'count': 0, 'samples': []})
        self.total_lines = 0
        self.skipped_lines = 0
        self.pairs = 0
        self.unmatched_rets = 0
        self.open_at_eof = 0

    def note(self, kind, line_no, detail, raw):
        lim = self.limitations[kind]
        lim['count'] += 1
        if len(lim['samples']) < LIMIT_SAMPLES_MAX and raw is not None:
            lim['samples'].append({'line_number': line_no,
                                   'detail': detail,
                                   'raw': raw[:512]})

    def _attr(self, ret_addr):
        if not self.module_map or not ret_addr:
            return 'unknown', None
        try:
            a = int(ret_addr, 16)
        except ValueError:
            return 'unknown', None
        for m in self.module_map:
            if m['lo'] <= a < m['hi']:
                return m['attr'], m['path']
        return 'other', None

    def _emit_pair(self, frame, tid, ret_addr, retval, raw_ret, line_no):
        attr, mod = self._attr(frame.get('return_address'))
        rec = {
            'kind': 'call_pair', 'tid': tid,
            'function': frame['function'], 'arguments': frame['arguments'],
            'ret_addr_call': frame.get('return_address'),
            'ret_addr_ret': ret_addr,
            'retval': retval, 'retval_known': retval is not None,
            'line_call': frame['line'], 'line_ret': line_no,
            'depth': len(self.stacks[tid]) + 1 if tid in self.stacks else 1,
            'caller_module': mod, 'attribution': attr,
            'flags': frame['flags'],
        }
        if frame.get('ret_addr') and ret_addr and \
                str(frame['ret_addr']).lower() != str(ret_addr).lower():
            rec['flags'].append('ret_addr_mismatch')
            self.note('ret_addr_mismatch', line_no,
                      f"call ret={frame['ret_addr']} vs Ret ret={ret_addr}", raw_ret)
        self.out.write(rec)
        self.pairs += 1

    def feed(self, line):
        self.total_lines += 1
        line = line.rstrip('\r\n')
        if not line.strip():
            return
        m = PEDLL_RE.match(line)
        if m:
            tid = m.group('tid')
            tail = line[m.end():]
            mv = TAIL_RETVAL_RE.search(tail)
            mr = TAIL_RET_RE.search(tail)
            if m.group('kind') == 'Call':
                self.stacks[tid].append({
                    'function': f"DllMain[{m.group('name')}]",
                    'arguments': f"reason={m.group('reason')},res={m.group('res')}",
                    'return_address': mr.group('ret') if mr else None,
                    'line': self.total_lines, 'flags': ['pe_dll_event'],
                })
            else:
                retval, problem = _clean_retval(mv.group('retval') if mv else None)
                if problem:
                    self.note(problem, self.total_lines, f"PE DLL {m.group('name')}", line)
                st = self.stacks[tid]
                if not st:
                    self.unmatched_rets += 1
                    self.note('unmatched_return', self.total_lines,
                              f"PE DLL Ret empty stack {m.group('name')}", line)
                    self.out.write({'kind': 'unmatched_ret', 'tid': tid,
                                    'function': f"DllMain[{m.group('name')}]",
                                    'raw': line[:512]})
                else:
                    fr = st.pop()
                    fr['flags'].append('pe_dll_event')
                    self._emit_pair(fr, tid, mr.group('ret') if mr else None,
                                    retval, line, self.total_lines)
            return

        m = RELAY_CALL_RE.match(line)
        if m:
            self.stacks[m.group('tid')].append({
                'function': m.group('func').strip(),
                'arguments': m.group('args'),
                'ret_addr': m.group('ret'),
                'return_address': m.group('ret'),
                'line': self.total_lines, 'flags': [],
            })
            return

        m = RELAY_RET_RE.match(line)
        if m:
            tid = m.group('tid')
            ret_addr = m.group('ret')
            retval, problem = _clean_retval(m.group('retval'))
            if problem:
                self.note(problem, self.total_lines, m.group('func'), line)
            st = self.stacks[tid]
            if not st:
                self.unmatched_rets += 1
                self.note('unmatched_return', self.total_lines, m.group('func'), line)
                self.out.write({'kind': 'unmatched_ret', 'tid': tid,
                                'function': m.group('func'),
                                'raw': line[:512]})
                return
            top = st[-1]
            match_idx = None
            if top.get('ret_addr') and ret_addr and \
                    top['ret_addr'].lower() == ret_addr.lower():
                st.pop()
                self._emit_pair(top, tid, ret_addr, retval, line, self.total_lines)
            else:
                if ret_addr:
                    for i in range(len(st) - 1, -1, -1):
                        ra = st[i].get('ret_addr')
                        if ra and ra.lower() == ret_addr.lower():
                            match_idx = i
                            break
                if match_idx is None:
                    st.pop()
                    self._emit_pair(top, tid, ret_addr, retval, line, self.total_lines)
                else:
                    while len(st) > match_idx + 1:
                        orphan = st.pop()
                        orphan['flags'].append('orphaned_by_retaddr_repair')
                        self.note('orphaned_call', self.total_lines,
                                  'lost inner return (ret-addr repair)', orphan.get('raw', ''))
                        self._emit_pair(orphan, tid, None, None, line, self.total_lines)
                    fr = st.pop()
                    fr['flags'].append('retaddr_repaired_pair')
                    self._emit_pair(fr, tid, ret_addr, retval, line, self.total_lines)
            return

        self.skipped_lines += 1
        self.note('skipped_line', self.total_lines, 'no grammar match', line)

    def finish(self):
        for tid, st in self.stacks.items():
            for fr in st:
                self.open_at_eof += 1
                self.note('unmatched_call', fr['line'],
                          f"call never returned ({fr['function']})", fr.get('raw', ''))
        return {
            'parser': 'l0-relay', 'version': TRACER_VERSION,
            'total_lines': self.total_lines, 'skipped_lines': self.skipped_lines,
            'completed_pairs': self.pairs, 'unmatched_rets': self.unmatched_rets,
            'open_calls_at_eof': self.open_at_eof,
            'limitations': {k: v['count'] for k, v in self.limitations.items()},
            'limitation_samples': self.limitations,
        }


class ChannelNormalizer:
    """Streams a generic Wine debug-channel raw log into JSONL events."""

    def __init__(self, out: JsonlWriter, channel, module_map=None):
        self.out = out
        self.channel = channel
        self.module_map = module_map or []
        self.total_lines = 0
        self.events = 0

    def feed(self, line):
        self.total_lines += 1
        line = line.rstrip('\r\n')
        if not line.strip():
            return
        m = CHANNEL_RE.match(line)
        if not m:
            return
        text = m.group('text')
        rec = {'kind': 'channel_event', 'channel': m.group('ch'),
               'level': m.group('lvl'), 'tid': m.group('tid'), 'text': text[:512]}
        # structured enrichment per channel
        if self.channel == 'module' or m.group('ch') == 'module':
            mm = MOD_MAP_RE.search(text)
            ml = MOD_LOADED_RE.search(text)
            if mm:
                rec['event'] = 'module_map'
                rec['path'] = mm.group(1).replace('\\??\\', '')
                rec['base'] = mm.group(2)
                rec['end'] = mm.group(3)
            elif ml:
                rec['event'] = 'module_loaded'
                rec['path'] = ml.group(1).replace('\\??\\', '')
        elif m.group('ch') == 'file':
            rec['event'] = 'file_op'
        elif m.group('ch') == 'reg':
            rec['event'] = 'reg_op'
        elif m.group('ch') == 'process':
            rec['event'] = 'process_op'
        elif m.group('ch') == 'thread':
            rec['event'] = 'thread_op'
        elif m.group('ch') == 'message':
            rec['event'] = 'message'
        self.out.write(rec)
        self.events += 1

    def finish(self):
        return {'parser': 'l0-channel', 'channel': self.channel,
                'total_lines': self.total_lines, 'events': self.events}


def resolve_wine(args):
    cand = getattr(args, 'wine_bin', None) or getattr(args, 'wine', None) \
        or os.environ.get('WINE_BIN') or os.environ.get('WINE')
    if cand:
        p = Path(cand).expanduser()
        if p.is_dir():
            for sub in ('bin/wine64', 'bin/wine'):
                q = p / sub
                if q.is_file():
                    return str(q), f"--wine dir/{sub}"
            raise FileNotFoundError(f'no wine binary under {p}')
        if p.is_file():
            return str(p), '--wine file'
        raise FileNotFoundError(f'--wine not found: {p}')
    from shutil import which
    w = which('wine64') or which('wine')
    if not w:
        raise FileNotFoundError('no wine: pass --wine or set WINE/WINE_BIN')
    return w, 'PATH'


def wine_version(wine_exe, env):
    try:
        r = subprocess.run([wine_exe, '--version'], capture_output=True,
                           text=True, env=env, timeout=20)
        return (r.stdout or r.stderr).strip()
    except Exception as e:
        return f'error: {e}'


def _run_capped(cmd, env, out_path, timeout_s, max_bytes):
    info = {'timed_out': False, 'truncated': False, 'bytes': 0}
    start = time.time()
    with open(out_path, 'wb') as fh:
        proc = subprocess.Popen(cmd, stdout=fh, stderr=subprocess.STDOUT,
                                env=env, start_new_session=True)
        while True:
            rc = proc.poll()
            try:
                size = os.path.getsize(out_path)
            except OSError:
                size = 0
            info['bytes'] = max(info['bytes'], size)
            if rc is not None:
                break
            if time.time() - start > timeout_s:
                info['timed_out'] = True
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
                break
            if max_bytes and size >= max_bytes:
                info['truncated'] = True
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
                break
            time.sleep(0.05)
        info['exit_code'] = proc.returncode
    info['elapsed_ms'] = round((time.time() - start) * 1000, 2)
    try:
        if max_bytes and os.path.getsize(out_path) > max_bytes:
            info['truncated'] = True
    except OSError:
        pass
    return info


WINEDEBUG_FOR = {
    'relay': '+relay',
    'module': '+module,+loaddll',
    'file': '+file',
    'reg': '+reg',
    'process': '+process,+start',
    'thread': '+thread',
    'msg': '+message',
}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def normalize_capture(raw_path, events_path, channel, module_map):
    jw = JsonlWriter(events_path)
    if channel == 'relay':
        norm = RelayNormalizer(jw, module_map)
    else:
        norm = ChannelNormalizer(jw, channel, module_map)
    with open(raw_path, 'r', errors='replace') as f:
        for line in f:
            norm.feed(line)
    report = norm.finish()
    jw.close()
    return report


def run_experiment(args):
    t0 = datetime.now(timezone.utc)
    wine_exe, how = resolve_wine(args)
    exe_path = str(Path(args.exe).resolve())
    if not os.path.exists(exe_path):
        print(f'FATAL: exe not found: {exe_path}')
        return 1
    with open(exe_path, 'rb') as f:
        if f.read(2) != b'MZ':
            print('FATAL: not a PE (no MZ)')
            return 1

    channels = [c for c in (args.channels or 'relay,module').split(',') if c]
    for c in channels:
        if c not in WINEDEBUG_FOR:
            print(f'FATAL: unknown channel {c} (have {sorted(WINEDEBUG_FOR)})')
            return 1

    exp_id = f"L0-{t0.strftime('%Y%m%d-%H%M%S')}-{Path(exe_path).stem}"
    out = Path(args.out).resolve() / exp_id
    (out / 'raw').mkdir(parents=True, exist_ok=True)
    (out / 'events').mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env.pop('WINEDEBUG', None)
    spec = {
        'experiment_id': exp_id,
        'tracer_version': TRACER_VERSION,
        'created_utc': t0.isoformat(),
        'exe': exe_path,
        'exe_sha256': sha256_file(exe_path),
        'exe_args': [str(a) for a in args.args],
        'wine_binary': wine_exe,
        'wine_resolution': how,
        'prefix_mode': args.prefix_mode,
        'channels': channels,
        'caps': {'relay_max_mb': args.relay_max_mb,
                 'channel_max_mb': args.channel_max_mb,
                 'timeout_s': args.timeout},
        'max_events': args.max_events,
    }
    if args.spec:
        spec['spec_file'] = str(Path(args.spec).resolve())

    base_prefix = Path(args.prefix).resolve() if getattr(args, 'prefix', None) \
        else (out / 'prefix-template')
    base_prefix.parent.mkdir(parents=True, exist_ok=True)
    env['WINEPREFIX'] = str(base_prefix)

    spec['wine_version'] = wine_version(wine_exe, env)
    print(f'== L0 {exp_id}: wine={wine_exe} ({spec["wine_version"]})')
    print(f'   exe={exe_path} channels={channels} prefix={base_prefix}')

    # ---- boot the prefix template once (deterministic shared seed) -------
    if not (base_prefix / 'system.reg').exists():
        print('   booting prefix template ...')
        boot = _run_capped([wine_exe, 'wineboot', '--init'], env,
                           str(out / 'raw' / 'wineboot.log'), 120, 32 << 20)
        spec['prefix_boot'] = {'exit_code': boot['exit_code'],
                               'timed_out': boot['timed_out'],
                               'elapsed_ms': boot['elapsed_ms']}

    module_map = []
    capture_facts = {}
    for ch in channels:
        if args.prefix_mode == 'fresh' or ch != channels[0]:
            pfx = out / 'prefixes' / ch
            if pfx.exists():
                shutil.rmtree(pfx)
            pfx.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(base_prefix, pfx, symlinks=True)
            env['WINEPREFIX'] = str(pfx)
        else:
            env['WINEPREFIX'] = str(base_prefix)
        cmd = [wine_exe] + [exe_path] + [str(a) for a in args.args]
        env['WINEDEBUG'] = WINEDEBUG_FOR[ch]
        # GUI applications need a display; xvfb-run wraps the whole wine
        # process group (applies to every capture of this experiment).
        if os.environ.get('L0_XVFB') == '1':
            from shutil import which as _which
            xv = _which('xvfb-run')
            xauth_missing = xv and not _which('xauth')
            if xv and not xauth_missing:
                cmd = [xv, '-a'] + cmd
            elif xv and xauth_missing:
                # xvfb-run refuses without xauth; spawn Xvfb directly and
                # point DISPLAY at it (no auth needed on /tmp sockets).
                import subprocess as _sp
                xvfb_proc = _sp.Popen(['Xvfb', ':97', '-screen', '0',
                                       '1280x1024x24'],
                                      stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL,
                                      start_new_session=True)
                time.sleep(1.0)
                env['DISPLAY'] = ':97'
                env['_l0_xvfb_pid'] = str(xvfb_proc.pid)
                print('   Xvfb :97 spawned (pid %d)' % xvfb_proc.pid)
            else:
                print('   WARN: L0_XVFB=1 but xvfb-run not found')
        cap_mb = args.relay_max_mb if ch == 'relay' else args.channel_max_mb
        tmo = args.timeout
        print(f'   capture {ch}: WINEDEBUG={env["WINEDEBUG"]} cap={cap_mb}MB t={tmo}s')
        info = _run_capped(cmd, env, str(out / 'raw' / f'{ch}.log'), tmo,
                           int(cap_mb) << 20)
        capture_facts[ch] = info
        print(f'     -> {info["bytes"]} bytes, exit={info["exit_code"]}'
              f'{" TRUNCATED" if info["truncated"] else ""}'
              f'{" TIMEOUT" if info["timed_out"] else ""}')

    # ---- module map for attribution (from the module capture if present) --
    mod_raw = out / 'raw' / 'module.log'
    if 'module' in channels and mod_raw.exists():
        pat = re.compile(r'([0-9a-fA-F]+):trace:module:map_image_into_view '
                         r'mapping PE file L"([^"]+)" at 0x([0-9a-f]+)-0x([0-9a-f]+)')
        with open(mod_raw, 'r', errors='replace') as f:
            for line in f:
                m = pat.search(line)
                if m:
                    lo = int(m.group(3), 16)
                    hi = int(m.group(4), 16)
                    path = m.group(2).replace('\\??\\', '')
                    attr = 'app' if path.lower().endswith(
                        Path(exe_path).name.lower()) else \
                        ('wine' if '/wine-' in path.lower() or
                         path.lower().endswith('.dll') and 'C:\\windows' in path.lower()
                         else 'guest_dll')
                    module_map.append({'lo': lo, 'hi': hi, 'path': path,
                                       'attr': attr})
        spec['module_map_entries'] = len(module_map)

    # ---- normalize every capture (streaming) -----------------------------
    reports = {}
    for ch in channels:
        raw = out / 'raw' / f'{ch}.log'
        if not raw.exists() or raw.stat().st_size == 0:
            continue
        print(f'   normalize {ch} ...')
        reports[ch] = normalize_capture(raw, out / 'events' / f'{ch}.jsonl',
                                        ch, module_map)

    spec['captures'] = capture_facts
    spec['normalization'] = reports
    with open(out / 'experiment.json', 'w') as f:
        json.dump(spec, f, indent=2, default=str)

    # ---- replay checks -----------------------------------------------------
    # head hash: raw bytes of the first events (includes volatile addresses)
    # seq hash: ASLR-normalized shape hash — hex tokens collapsed, so the
    # same application run yields the same sequence across repeated runs.
    HEX_TOK = re.compile(r'\b[0-9a-fA-F]{4,}\b')

    def seq_shape(path, n=20000):
        # Per-THREAD call-sequence hash: cross-thread interleaving is
        # scheduler-dependent (wineserver races), so a global order is not
        # a stable invariant — but each thread's own order is. Threads are
        # hashed independently and combined in sorted-tid order.
        per_tid = {}
        cnt = 0
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                if cnt >= n:
                    break
                try:
                    ev = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if ev.get('kind') != 'call_pair':
                    continue
                tid = ev.get('tid') or '?'
                per_tid.setdefault(tid, [])
                per_tid[tid].append(ev.get('function') or '')
                cnt += 1
        h = hashlib.sha256()
        for tid in sorted(per_tid):
            h.update(tid.encode() + b':')
            h.update('\n'.join(per_tid[tid]).encode('utf-8', 'replace'))
            h.update(b'\n##\n')
        return h.hexdigest()[:16]

    replay = {}
    seqhash = {}
    for ch in channels:
        ev = out / 'events' / f'{ch}.jsonl'
        if ev.exists():
            with open(ev, 'rb') as f:
                head = f.read(1 << 20)
            replay[ch] = hashlib.sha256(head).hexdigest()[:16]
            seqhash[ch] = seq_shape(ev)
    spec['replay_head_hash'] = replay
    spec['replay_seq_hash'] = seqhash
    with open(out / 'experiment.json', 'w') as f:
        json.dump(spec, f, indent=2, default=str)

    # ---- human summary (Phase 2 report) -----------------------------------
    lines = [f'# L0 experiment {exp_id}', '',
             f'- exe: `{exe_path}` (sha256 `{spec["exe_sha256"][:16]}…`)',
             f'- wine: `{spec["wine_version"]}` via {how}',
             f'- channels: {", ".join(channels)}', '']
    for ch, rep in reports.items():
        if ch == 'relay':
            lines += [f'## relay', '',
                      f'- lines: {rep["total_lines"]}, pairs: {rep["completed_pairs"]},'
                      f' unmatched_rets: {rep["unmatched_rets"]},'
                      f' open_calls: {rep["open_calls_at_eof"]},'
                      f' skipped: {rep["skipped_lines"]}',
                      f'- limitations: {rep["limitations"]}', '']
        else:
            lines += [f'## {ch}', '', f'- events: {rep["events"]}', '']
    with open(out / 'summary.md', 'w') as f:
        f.write('\n'.join(lines))

    if not args.keep_prefixes:
        for p in [out / 'prefixes']:
            shutil.rmtree(p, ignore_errors=True)
    # shut down the shared Xvfb if we spawned one (xauth-less path)
    if '_l0_xvfb_pid' in env:
        try:
            os.kill(int(env['_l0_xvfb_pid']), signal.SIGTERM)
        except (ValueError, ProcessLookupError):
            pass

    print(f'== done: {out}')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description='Universal Wine L0 tracer')
    ap.add_argument('exe', nargs='?', help='Windows PE to observe')
    ap.add_argument('args', nargs='*', help='application arguments')
    ap.add_argument('--spec', help='experiment spec JSON (overrides CLI exe/args)')
    ap.add_argument('--out', default='l0-experiments')
    ap.add_argument('--channels', default='relay,module')
    ap.add_argument('--wine', help='wine install dir or binary')
    ap.add_argument('--wine-bin', help='wine binary (wins over --wine)')
    ap.add_argument('--prefix', help='prefix template dir (default: per-run template)')
    ap.add_argument('--prefix-mode', choices=['fresh', 'template'], default='fresh',
                    help='fresh: clone template per channel (default, contamination-free)')
    ap.add_argument('--timeout', type=float, default=30)
    ap.add_argument('--relay-max-mb', type=float, default=256)
    ap.add_argument('--channel-max-mb', type=float, default=64)
    ap.add_argument('--max-events', type=int, default=2_000_000)
    ap.add_argument('--keep-prefixes', action='store_true')
    # Application arguments may themselves start with '-' (e.g. `upx
    # --version`); split the command line at the `--` sentinel like
    # standard Unix tools do.
    if argv is None:
        argv = sys.argv[1:]
    if '--' in argv:
        sep = argv.index('--')
        argv, app_args = argv[:sep], argv[sep + 1:]
    else:
        app_args = []
    args, extra = ap.parse_known_args(argv)
    if extra:
        app_args = extra + app_args
    args.args = app_args

    if args.spec:
        with open(args.spec) as f:
            s = json.load(f)
        args.exe = s.get('exe', args.exe)
        args.args = s.get('args', args.args)
        args.channels = s.get('channels', args.channels)
        args.timeout = s.get('timeout', args.timeout)
        args.relay_max_mb = s.get('relay_max_mb', args.relay_max_mb)
        args.channel_max_mb = s.get('channel_max_mb', args.channel_max_mb)
        if not args.exe:
            print('FATAL: spec lacks exe')
            return 1
    if not args.exe:
        ap.print_usage()
        return 1
    try:
        return run_experiment(args)
    except FileNotFoundError as e:
        print(f'FATAL: {e}')
        return 1


if __name__ == '__main__':
    sys.exit(main())
