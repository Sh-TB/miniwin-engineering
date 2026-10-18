# SWEEP-008 — REA (Reverse Engineer Anything) evaluation (2026-10-18)

User directive: «تو گیت هاب rea برای مهندسی معکوس هست خیلی معرف شده ببین میتونه
بهت کمک بکنه روی چند تا برنامه تست بکن» — the trending GitHub RE tool "REA"
(morluto/rea) was suggested; test it on a few of our programs and see whether
it helps MiniWin.

## Toolchain adopted (all OUTSIDE the repo, user-space)
- `rea` CLI 6.7.0 (npm `rea-agents`, MIT) — installed globally
- Ghidra 12.1.4 PUBLIC — `/home/z/ghidra-dl/ghidra_12.1.4_PUBLIC`
- Temurin full JDK 21.0.12.1 — `/home/z/ghidra-dl/jdk-21.0.12.1+1`
  (system java was a JRE; REA's ghidra-java check requires a FULL JDK)
- Env needed per session: `GHIDRA_INSTALL_DIR`, `JAVA_HOME` (JDK), plus
  `REA_PWNTOOLS_PYTHON` for the pwntools provider (pwntools 4.15.0 +
  pyelftools 0.33 + unicorn 2.1.2 installed into the project venv).
- `rea doctor --provider ghidra` → **healthy: true** after the JDK fix.

## Provider findings (honest)
| provider | verdict |
|---|---|
| Ghidra 12.1.4 | WORKS — all native workflows (function/xrefs/trace/instruction) on our x64 PEs |
| Kuna | PE NOT SUPPORTED (ELF/Mach-O only) — rejected with typed reason |
| pwntools provider | ELF-only workflows (`inspect-binary-layout` rejects PE) |
| Hopper / IDA / jadx | not installed (not needed; Ghidra covers us) |

## What was tested, per program

### 1. puttygen.exe (SW-552 — crash RIP=0) — DEEP DIVE, root-cause class changed
- `rea function puttygen.exe 0x1400028e2` (stack-scan return address from the
  crash dump) → 15,214-char decompilation of WinMain `FUN_1400028b0` in ONE
  command, plus callers/callees graph (FUN_140041140 → WinMain).
- objdump cross-check: 0x1400028e2 is NOT a call — it is the return address
  pushed by WinMain's `call 0x14000be30` at 0x1400028dd.
- pefile: the indirect-call slot **0x14008a8e8 is NOT an IAT entry** — it is a
  .data FUNCTION-POINTER VARIABLE.
- `rea xrefs puttygen.exe 0x14008a8e8` → 19 references (reader 0x14000be4c =
  `mov [rip+..],%rdi` right before `call *%rdi` at 0x14000be56; writer
  candidates clustered at 0x14002c787..0x14002cc57).
- **SW-552 hypothesis REWRITTEN**: the NULL call is a never-initialized .data
  function pointer (an init step that MiniWin's stubs no-op'd was supposed to
  fill it), NOT a bound-import/ordinal IAT slot. Evidence:
  `puttygen_winmain_function.json`, `puttygen_fnptr_xrefs.json`.

### 2. cports.exe (SW-551 — crash 0x14001b338) — instruction-level decode
- `rea function` → crash inside WinMain `FUN_14001ad50` (called straight from
  `entry`), full decompile saved.
- `rea inspect-native-instruction 0x14001b338` → decoded operand facts:
  **`CMP dword ptr [R11 + 0xb8], R13D`** (7 bytes, 45:39:ab:b8:00:00:00).
- SW-551 sharpened: the fault is a DATA read of an invalid structure pointer
  (R11) at offset 0xb8 during the process-enum phase — a corrupted/never-set
  struct base, not a control-flow transfer. Evidence: `cports_crash_*.json`.

### 3. pageant.exe (SW-554 — exit(1) gate) — string-to-code in one command
- `rea trace pageant.exe "security APIs"` → literal at 0x140070414 referenced
  from 0x140001b4e inside **FUN_1400017e0** — the fatal-error path.
- `rea function 0x1400017e0` (18,234-char decompile) → the gate structure:
  main-init `else` branch → `MessageBoxA(NULL,"Unable to access security
  APIs...","Pageant Fatal Error",0x10)` → `return 1` = the exit(1).
- Runtime trace (api_trace.json) confirms the teardown after the dialog:
  LoadLibraryA(api-ms-win-appmodel-runtime-l1-1-2) → GetProcAddress
  (AppPolicyGetProcessTerminationMethod) → LoadLibraryA(mscoree.dll) →
  GetProcAddress(CorExitProcess) → ExitProcess(1).
- `FUN_1400132a0` (a candidate guard) decompiles to `return 1;` — the real
  security-check chain is earlier in init; SW-554's search space is cut to the
  pre-message-loop init of FUN_1400017e0. Evidence: `pageant_*.json`.

### 4. ProduKey.exe — engine-free workflow
- `rea inspect-pe-resources` → structured resource evidence (identity, ranges,
  hashes) without any native engine. Works out of the box on PE.

## Verdict — does REA help MiniWin? YES, adopt for the isolation work
1. **One-command decompilation** (`rea function <pe> <addr>`) replaces the
   manual objdump + UNWIND walk we did for BUG-066; call graphs included.
2. **`rea trace`** (literal → code reference → containing function) is the
   fastest route from a guest's visible error text to the responsible function
   — directly useful for SW-554/555 gate programs.
3. **`rea inspect-native-instruction`** gives typed operand facts for crash
   sites — better than raw objdump lines for the worklist.
4. **`rea xrefs`** on data addresses answers "who writes this pointer" — the
   exact question SW-552 needed.
5. Gaps: Kuna/pwntools providers reject PE; ephemeral Ghidra project per
   invocation (no persistent snapshot reuse in CLI mode without --snapshot);
   first `rea function` on a new PE pays Ghidra auto-analysis (~40-90 s for
   our 0.2-2.5 MB PEs).

## MiniWin impact summary (worklist deltas)
- SW-552: root-cause CLASS corrected (uninitialized .data fn-pointer, reader
  FUN_14000be30 at `call *%rdi` 0x14000be56; writer candidates
  FUN-region 0x14002c787..0x14002cc57).
- SW-551: faulting instruction = `CMP [R11+0xb8],R13D` (data fault).
- SW-554: fatal path = FUN_1400017e0 else-branch → MessageBoxA → return 1;
  teardown probes confirmed; remaining hunt = early-init security chain.
- No loader.c change this sweep (tooling evaluation + isolation progress).

## Files
- REA evidence JSONs (this directory) — 7 files, one per query above
- Toolchain lives in /home/z/ghidra-dl (git-ignored territory, outside repo)
