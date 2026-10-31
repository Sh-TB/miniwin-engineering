#!/bin/bash
# SWEEP-083 Issue #1 progress comment
cd "$(dirname "$0")/.." || exit 1
TOKEN=$(git remote get-url origin | sed -n 's|.*://\([^:]*\):\(.*\)@github.*|\2|p')
BODY=$(cat <<'EOF'
## SWEEP-083 — the o32/SEH class fix set; WinNc 551→14556 steps into real Delphi SEH; renamer un-aborted; NEW program mines = PASS first run

**Base:** origin/main 9bc8009 == local at session start; G1 pre-change PIN 12/12 with clean rebuild `4f9cc889` (evidence/master/g1/20261026_184437). Final engine `80bca026`; G1 post-change PIN 12/12 (20261026_200439). Commit af624e3. Corpus parity 3×: **21/21 green, zero regressions** across all seven engine changes.

### Engine fixes (all generic; zero app special-casing)
- **SW-1387 — cpu32 o32 modrm-group plane**: `66 81 c7 ef 00` (add di,0xef) ran the 32-bit grp1 handler — imm32 read consumed 7 bytes instead of 5, eip landed mid-instruction on the modrm byte (0xe7) → PE32-FATAL rc=86. The whole modrm imm-group (81/83), ALU r/m↔r family (01/03/09/0B/…/39/3B), mov r/m16↔r16 (89/8B), test (85), imul-imm (69/6B), shift groups (C1/D1/D3) and grp3 (F7) were 66-blind (only B8-BF/C7/accumulator-ALU were o32-aware since SW-1385/86). One dedicated pre-dispatch plane, 16-bit semantics (low-16 write-back, imm16, alu_*16 flag helpers), B8-BF/C7/acc-ALU stay inline. WinNc: 551 → 12068 steps.
- **SW-1388 — i386 arity-table audit**: plain `InitializeCriticalSection` was MISSING from `g_api32_arity` → non-CRT fallback `MW32_ARITY_DEFAULT(32)` → the gate over-popped 28 bytes per CS init → the whole rest of init ran with esp=true+32 → the Delphi lazy-helper's `ret` consumed the *installed SEH handler slot* (0x409482) → the except-handler body entered with ER*=[esp+4]=0 → `test [0+4],6` host SIGSEGV. Audit of all 883 entries vs canonical stdcall sizes: 0 wrong, 18 missing (InitializeCriticalSection 4, InterlockedCompareExchange 12, MoveFileA/W, GetFileTime 16, HeapDestroy/Validate, LocalLock/Unlock, VirtualLock/Unlock, CreateMutexW, CreateSemaphoreA, ReleaseSemaphore, IsBad*, SetCurrentDirectoryW).
- **SW-1389 — bitness-aware SYSTEM_INFO**: `mw_GetSystemInfo` wrote the **48-byte x64 layout** through the guest's 32-bit pointer — offset 36 (dwAllocationGranularity=65536=0x10000) landed exactly on the caller's saved return address (winnc wrapper 0x40f570: `add esp,-0x24; push esp; call GetSystemInfo; mov eax,[esp+0x14]; add esp,0x24; ret`) → the epilogue ret jumped to 0x00010000 → SIGSEGV. i386 plane now gets the 36-byte x86 layout (armed on t_cpu32_live ‖ g_image_base32 — top-level runs never set t_cpu32_live).
- **SW-1390 — real x87 DB mod=3 + FWAIT**: FNCLEX=DB E2 and FNINIT=DB E3 have reg field **4** — the old checks (`m.reg==7`) never fired, so every guest FPU re-init died with [PE32-FATAL] rc=86. Real group: FNINIT (CW=0x037F, SW=0, top=0, regs cleared), FNCLEX, FUCOMI/FCOMI (C0/C2/C3 + ZF/PF/CF), legacy FENI/FDISI/FSETPM no-ops; FWAIT (0x9B) added.
- **SW-1391 — idempotent modrm_apply_seg**: NEEDM() applied the FS/GS override and the LD_RorM* helpers applied it AGAIN — `push %fs:(%eax)` (Delphi's SEH install `push fs:[0]`) read **2×fsbase** (heap zero) instead of the chain head → every SEH frame registered with prev=0 → the chain collapsed to empty at the first try/finally pop → RaiseException saw no handler. Also: LEA must ignore segment prefixes (`64 66 8d 80 00 00 00 00` padding-lea corrupted a copy source 0x3fefed9c→0x4fefed9c → movups host SIGSEGV). `seg_applied` flag added.
- **SW-1392 — i386 SEH dispatch**: RaiseException used to RETURN — the unreachable `ret` after the call site executed heap garbage (eip=0x1112a680). Real x86 dispatch now: ER (code/flags/params from the guest stack) + i386 CONTEXT (0x2cc, full reg image) built in guest scratch; FS:[0] chain walk; handlers run **CDECL on the guest stack** (ret-sentinel pump, esp restored per disposition); ContinueSearch / ContinueExecution / RtlUnwind protocols. **RtlUnwind = a normal stdcall call that unwinds frames above the target (flags|=EH_UNWINDING) and RETURNS to its caller** — the handler owns the resume; proof: the guest passed TargetIp = the RA (0x408e78) as a formality and the post-unwind code reads [esp+0x28] — a slot in the *handler's own frame* (the earlier resume-at-target+8 stomped it → edi=0 → SIGSEGV). Pump protocol: MW32_SEH_RESUME_CODE rides up through nested pumps; the top loop re-enters cpu32_run at the new eip. **SW-1392e** (found during 1392): the modrm idempotence fix above made the chain real for the first time — SEH-CALL trace shows 0x3fefef70 → 0x3fefef7c with real dispositions.
- **SW-1394 — real 0F AE group**: /3 was mislabeled a fence (it is **stmxcsr**) and /2 (**ldmxcsr**) was refused outright — Delphi's CPU-detect `ldmxcsr -0x4(%ebp)` (0x404248) died rc=86. Intel map implemented: fxsave (FCW/FSW/MXCSR honest, rest zero), fxrstor (MXCSR taken), ldmxcsr, stmxcsr, lfence/mfence/sfence.

### Results (3× cold runs each)
| Guest | Before | After |
|---|---|---|
| **winnc** (user-named, Inno/Delphi) | interpreter abort rc=86 @551 steps (SWEEP-082 mis-bucketed as "EXIT clean" — the classifier only greps [CRASH], PE32-FATAL passed through) | **14556 steps**: SEH chain intact, dispatch→unwind→**Delphi finally machinery reached** for the first time. New frontier SW-1393: the finally-retry router (`call *ecx`, ecx=[node+4]+5 → 0x4a8a2c → jmp 0x4a899b) re-entry stack contract. |
| **renamer** (Delphi) | interpreter abort rc=86 (ldmxcsr refused) | **clean guest exit rc=204** (RunError(204) = EInvalidPointer class; SetUnhandledExceptionFilter(NULL) → ExitProcess(204) tail; pre-render) |
| **mines (NEW — Simon Tatham, SWEEP-072 A-group U10)** | — | **PASS_INPUT_STATE_CHANGE on first contact** (rc=0 ×3, 6 frames / 3 unique states, official portable zip sha a5b00f63) |
| npp64 (user-named) | PASS_INPUT_STATE_CHANGE | unchanged ×3 (13.8k APIs, rc=0) |
| idman (user-named) | PASS_LAUNCH_RENDER | unchanged ×3 |
| winrar (user-named) | parity frontier SW-1361 | unchanged ×3 |
| notepad2mod | clean exit | unchanged (UCRT finalization probes in trace — x64 wagon queued) |
| everything / tcpview64 | TIMEOUT frames=0 | unchanged (poll-loop wagons queued) |
| hxd | BLOCKED_DOWNLOAD | unchanged (mh-nexus refused, 8th attempt on record) |

### Gates
G1 12/12 ×2 (pre 4f9cc889 / post 80bca026); corpus parity 3× 21/21 with deterministic frames (spider timing variance only); trio re-verified before the fix set; no force-push (9bc8009 → af624e3, fast-forward).

### Next wagons
SW-1393 winnc finally-retry router decode (0x4a899b/0x408d3c contract) · renamer rc=204 decode (why the invalid-pointer path fires pre-render) · notepad2mod x64 "exits before WinMain renders" · everything/tcpview64 poll loops · more SWEEP-072 A-group puzzles as new-program wagons (the collection fielded a first-run PASS).
EOF
)
gh api repos/Sh-TB/miniwin-engineering/issues/1/comments -f body="$BODY" 2>/dev/null | python3 -c "import json,sys; d=json.load(sys.stdin); print('comment posted:', d['id'])" || curl -s -H "Authorization: token $TOKEN" -H "Accept: application/vnd.github+json" https://api.github.com/repos/Sh-TB/miniwin-engineering/issues/1/comments -d "$(python3 -c 'import json;print(json.dumps({"body":open("/tmp/issue_body.txt").read()}))')" | head -c 120
