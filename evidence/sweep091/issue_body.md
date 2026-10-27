## SWEEP-091 — SW-1412 ownership registry kills the guest-garbage→host-free() crash class; registry chain install→installed-app; NEW game flood + NEW program produkey first-contact PASS

**Protocol:** engine fix + registry chain + 1 new game + 1 new program, gates. Commit e29d19f. Engine **changed** — G1 **12/12** (`evidence/master/g1/20261027_142322`, binary sha 38a787f8); corpus parity **21/21 ×3** (rc=0, crash=0, fatal=0).

### SW-1412 root cause (crash CLASS, not one bug)
Installed WinRAR.exe (booted from its SFX-installed tree) SIGSEGV'd inside host libc (`rc=139 ×3, 0 frames`). Decode: `SHGetFolderLocation` was unimplemented — the generic stub answered `0` (S_OK) **without writing the out-param**; the guest read stack garbage as a PIDL and handed it to `CoTaskMemFree`, which called host `free()` on it with **no ownership check**. Five free paths had the same hole: `mw_CoTaskMemFree`, `mw_shmalloc_free/realloc` (SHGetMalloc IMalloc), `mw_ole_malloc_free/realloc` (CoGetMalloc IMalloc).

**Fix (boundary hardening, engine-wide):** a CoTask/IMalloc **ownership registry** (`mw_cotask_track/owned/untrack`, 1024 buckets) — every block handed to a guest through CoTaskMemAlloc/IMalloc::Alloc is tracked; every free/realloc of a foreign pointer is **logged and refused**. `DidAlloc` now answers honestly (1 owned / 0 foreign / −1 NULL). Plus `mw_SHGetFolderLocation` with the real HRESULT contract (E_POINTER on NULL out; E_FAIL + `*ppidl=NULL` otherwise — the guest takes its documented fallback path instead of dereferencing garbage).

**MEASURED:** winrarinstd `rc=139 crash=1 ×3 → rc=124 crash=0 ×3`. Trace shows the **full main-window skeleton**: 2×STATUSBAR + COMBOBOX + 2×TOOLBAR + SysListView32 file list, 3×`ShowWindow(SW_SHOW)`, `TB_SAVERESTOREW`/`TB_INSERTBUTTONA`/`LVM_SETITEMTEXTW` traffic. Remaining frontier registered as **SW-1417** (boot completion + first paint — guest runs to timeout, no frame yet).

### Registry chain install → installed-app (SW-1418/1419)
- `RegOpenKeyExW` **miss path now logs the composed key path** (a 160× poll of `HKCU\Software\WinRAR\General\Toolbar\Buttons` was invisible before).
- **SW-1418:** seeded `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion` (ProgramFilesDir/CommonFilesDir/ProgramFilesPath/DevicePath) — the non-NT twin the SFX installer probes; hive missed it.
- **SW-1419:** `plugin_run.sh` gained `reg_seed=` — stages an `MWREG001` snapshot into the sandbox so the installed app boots with the installer-written registry. Loader hardening: **root-slot reservation on load** (slots 0..6 = HKCR..HKDD are authoritative; the initial parse had the field order wrong — nvals/path swapped — and broke silently at slot 0). Fixture `regseed_firstrun.bin` = 16 first-run `HKCU\Software\WinRAR` keys (the SFX options-dialog writes remain pending as part of SW-1417).
- **Result:** reg misses 296 → **17**, and all 17 are one-time honest probes (`Policy`, `Policies\Explorer`, `Profiles\4`) — the retry loop is gone.

### New wagons (first contact)
| Wagon | Verdict | Evidence |
|---|---|---|
| **flood** (Simon Tatham, 8th collection PASS) | **PASS_INPUT_STATE_CHANGE** | rc=0 ×3, 6 frames / 3 uniq |
| **produkey** (NirSoft x64, registry-heavy) | **PASS_LAUNCH_RENDER** | rc=124 ×3 (live GUI pump), 1 real frame |

### Gates
Corpus parity **21/21 ×3** rc=0 crash=0 fatal=0 (`evidence/sweep091/corpus_parity.tsv`). G1 **12/12** (`evidence/master/g1/20261027_142322`). No force-push.

**Next train:** SW-1417 (installed-WinRAR boot paint + installer options-dialog phase), SW-1413 (7zsetup PE32 CRT-init), SW-1414 (untangle SIGABRT), SW-1415 (uninstaller UI reach), SW-1416 (handle value-space collision).
