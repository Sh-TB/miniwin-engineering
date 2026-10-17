# Android Feasibility Study — can MiniWin run Windows PE on Android?

**Status:** ACTIVE (ADR-013). **Created:** 2026-10-17. **Driver:** user directive —
"ببین میشه تو اندروید اجراشون کرد و چه پیش‌نیازهای نصبی لازم دارن؛ شاید راه‌های
میانبری باشه؛ گیت‌هاب رو عمیق بررسی کن" — answered with a deep GitHub survey.

---

## 1. The core question, stated precisely

MiniWin today = a Linux x86_64 program that (a) parses/loads PE32+ x64 and (b)
**executes guest machine code natively** (it just `mmap`s the image and jumps in).
On Android/ARM64, (a) ports easily (portable C + mmap), but (b) is impossible
natively — ARM64 CPUs cannot execute x86_64 machine code. So every viable Android
path needs an x86_64 **CPU engine**. The whole feasibility question is: *which
engine, and how much glue?*

## 2. GitHub survey (deep dive, license-mapped)

| Project | What it proves | Engine | License | Take for us |
|---|---|---|---|---|
| **Winlator** (brunodev85, 19.5k★) | x86_64 Windows apps run on ARM64 Android **today, in one APK** | Wine + Box86/Box64 + PRoot + Ubuntu rootfs + Mesa/DXVK | LGPL-2.1 | pattern-only; proof the stack works on Android 7+ |
| **Mobox** (olegos2) | same via **Termux** (no APK packaging needed) | Box64 + Wine + Termux-X11 | open | prereq list + install flow to copy |
| **Hangover 11.0** (André Zwing, Jan 2026) | Wine-on-aarch64 running x86/x64 PE via **FEX-Emu or Box64** | FEX/Box64/qemu | LGPL | architectural blueprint: *loader process (ARM) + translator for guest code* = exactly MiniWin+engine shape |
| **Box64** (ptitSeb, 5.7k★) | x86_64→ARM64 userspace dynarec, **works on Android via Termux**, 5-10× interpreter speedup | dynarec | BSD-3 | the engine to target for a native port later |
| **FEX-Emu** | alternate x86→ARM64 translator, JIT, used by Hangover | JIT | MIT | engine option B (license-clean) |
| **qemu-user** (via Termux `qemu-user-x86_64`) | full x86_64 userland emulation on-device, **zero porting** | TCG JIT | GPL-2 (external process) | Path A engine (run as separate process → no license coupling) |
| **Unicorn** | CPU-emulator library (QEMU-based); powers Qiling, angr, PE emulators (x86-64-pe-emu, PyAna…) | TCG | **GPL-2 — embedding contaminates license** | only as external process or under a future GPL decision |
| **proot-distro** (Termux) | rootless Debian/Alpine containers on Android incl. cross-arch via qemu-user | proot+qemu | GPL | Path A delivery vehicle |
| **Termux** | the platform: **Android 7.0+** (F-Droid/GitHub build; 5.0-12.0 "with issues"), AArch64/ARM/x86_64 | — | GPL-ish | prereq baseline |
| **otvdm/winevdm** | 16-bit Windows on 64-bit hosts | VMM | GPL | future Win16 rung (LADDER-094), inspiration only |
| **wibo / loadlibrary / retrowin32** | minimal PE loaders on Linux (our design kin) | native x86 | MIT / Apache-2 | architecture patterns, some MIT code |

## 3. The three viable paths

### Path A — راه میانبر (the shortcut, zero porting): Termux → proot-distro → Debian x86_64 + qemu-user-x86_64 → MiniWin unmodified
MiniWin already builds and runs on Debian x86_64; inside an emulated x86_64
Debian it runs *unchanged*. Guest PE code executes under qemu's TCG JIT, which
itself runs as ARM64 code on the phone. Two emulation layers (TCG + native),
so expect ~10-30× slowdown — still fine for T0-T2 ladder rungs (console tools:
7za/jq/sqlite3/zstd banners and round trips are second-scale even emulated).
**Prerequisites (install list):**
1. Termux (F-Droid or GitHub APK; Android 7.0+; ~300 MB)
2. `pkg install proot-distro qemu-user-x86_64` (+`git clang make` inside the distro)
3. `proot-distro install debian` → `proot-distro login debian`
4. Inside: clone repo, `make`, run regression + ladder T0/T1 as usual.
**Android version matrix:** 7-9 clean; 10-12 works (Termux handles the W^X /
exec restrictions — see wiki "Termux and Android 10"); 13+ works;
**16 KB page-size devices** (newer Pixels): qemu handles guest pages, but MiniWin
must not hardcode 4 KB — audit `MINWIN_PAGE_SIZE`/section alignment (task ANDROID-004).

### Path B — native aarch64 MiniWin + external x86_64 engine (the real port, medium effort)
Build MiniWin itself for aarch64 (loader/pe/tracing code is portable C; verify
no x86 asm dependencies — the ms_abi stubs ARE x86 asm and must be behind the
engine boundary). Guest CPU execution is delegated to an engine process/protocol:
- B1: **qemu-user plugin process** (same engine as A, but MiniWin runs natively on ARM64 — faster orchestration, still TCG for guest).
- B2: **FEX-Emu** (MIT) as engine — license-clean JIT, used by Hangover.
- B3: **Box64** (BSD-3) — fastest on Android (dynarec), proven by Winlator/Mobox.
License rule: engines run as **separate processes** (GPL qemu) or are **BSD/MIT**
(linkable). Unicorn embedding stays forbidden until a GPL decision (ADR-014).
Key design work: split MiniWin's "loader+API host" from "CPU execution" behind an
interface (the Hangover architecture: Wine-loader + FEX/Box64-engine is the proof
this decomposition works in production).

### Path C — comparison baseline (borrow the ecosystem, don't rebuild it)
Install Winlator/Mobox and run ladder T0/T1 programs there; record their behavior
as the reference for what "should" work (and which API quirks Wine papers over).
This costs zero engineering and gives us a ground-truth oracle on the same hardware.

## 4. Verdict

- **Feasible today via Path A with zero code changes** — this is the shortcut the
  user asked for; document a runbook (ANDROID-001..003 tasks) and a CI-optional
  smoke test.
- **Path B is the strategic port**: MiniWin's value on Android = tiny footprint +
  full API trace + our measure-driven API subset, vs Wine's weight. Hangover
  proves the loader/engine split; FEX (MIT) is the license-clean engine to target
  first, Box64 second.
- **Path C** gives a free comparison oracle.
- Not feasible / blocked: Unicorn embedding (GPL), UWP packaging (out of scope),
  16-bit on-device (needs VMM sub-project).

## 5. Prerequisites summary (what the user asked for explicitly)

| Path | Device req | Software prereqs | Effort | Speed |
|---|---|---|---|---|
| A | Android 7.0+ (5-6 legacy), arm64 or x86_64 device, ~1.5 GB free | Termux, proot-distro, qemu-user-x86_64, Debian image | minutes | slow (10-30×) |
| B | Android 7.0+ arm64 | Termux or NDK build; FEX/Box64 engine | weeks (engine protocol) | fast w/ dynarec |
| C | Android 7.0+ | Winlator APK or Termux+Mobox | minutes | n/a (oracle) |
