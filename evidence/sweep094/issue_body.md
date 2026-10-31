# SWEEP-094 — Run WinRAR, Notepad++ and popular Windows programs

Commit: `e699cb0` — G1 PIN OK (12/12 regression, no crash, binary sha `de2b1f2ca962d153…`)

## Goal

User directive (translated): "Continue — run WinRAR, Notepad++ and several popular Windows programs." This sweep pushed the install → run chains for the named programs through real frontiers, honestly (no fabricated success states).

## What moved

### WinRAR 7.12 (installer + installed payload)
- **SW-1462** (`scripts/plugin_run.sh`): derived installed payloads resolve in-tree FIRST. The SWEEP-092 source-only cleanup deleted `artifacts/`, which had silently satisfied the legacy existence check for `winrarinstd` before its in-tree override ran — the install→run chain was dead on a fresh clone. Fixed.
- **SW-1463** (`scripts/regseed_extend.py`): the reg_seed fixture now carries the post-install state (`HKCU\Software\WinRAR\Setup`), which the SFX leaves behind on real Windows. Result: the first-run setup poll loop passed, and the installed `WinRAR.exe` reached **main-window initialization for the first time** — window shown, toolbar children created, settings timer armed, alive at timeout, crash=0. Previously stuck forever at the SW-1417 setup dialog.
- Registry read/write contract re-verified (install writes 15 values; the installed exe reads back its own installer-written profile keys + `ProgramFilesDir`).

### Notepad++ 8.9.8.1
- **URLs re-pinned honestly**: both GitHub release assets (`npp.installer.x64`, `npp.portable.x64`) were downloaded and their SHA-256 re-measured — both matched the previously pinned hashes byte-for-byte. Manifest rows now carry exact release-asset URLs (previously `UNVERIFIED_URL`).
- **SW-1466** (`loader.c`): x86 guests must get arena32 VAs from the GlobalAlloc family. The NSIS installer asked `GlobalAlloc(GHND, 0x800000)`, received a 64-bit host mmap pointer (glibc mmap threshold), truncated it to 32 bits and SIGSEGV'd (`si_addr=0xdb000f`). All seven Global/Local alloc-family entry points now follow the SW-1281 arena contract. This was a general engine bug affecting every 32-bit guest allocating ≥128KB.
- **SW-1467** (`loader.c`): `MessageBoxIndirectW` decoded the host x64 `MSGBOXPARAMS` layout against the guest's 40-byte 32-bit struct → garbage text pointers → crash inside `MessageBoxW`. With the 32-bit decode in place the installer rendered its real dialog: `evidence/sweep094/nppsetup/messagebox_1.png`.
- **SW-1468** (`loader.c`): unified configurable OS version plane. The version story was inconsistent — `GetVersionEx` claimed 10.0 while the KUSD page and the registry seed reported 5.2; the installer read the registry, saw Server 2003 and showed its honest "This version of Notepad++ doesn't support [this OS]" dialog. Now `MINWIN_OSVER=6.1` (set per-plugin via the new `env` manifest field) drives KUSD + registry seed + GetVersion/Ex coherently ("Windows 7 6.1 b7601"). Defaults unchanged — XP-era corpus untouched. With the 6.1 plane the version gate passed and the installer reached the file-extraction phase.
- **SW-1470** (`scripts/plugin_run.sh`): portable (zip, non-sandbox) apps now run with cwd = their own extracted directory, like a real user double-clicking inside the folder. Notepad++ portable then loaded `langs.xml` (previously: its real "Load langs.xml failed!" recovery dialog) and exits **rc=0, three runs, no crash, no error dialog**.
- **SW-1469** (documented frontier): the unicode NSIS stub loads `C:\Windows\System32\<garbled>.dll` 23× (names full of literal U+003F chars — hex-dump diagnostic proved the '?' bytes are already in the guest buffer, i.e. an earlier A→W conversion replaced bytes with default chars), fails ERROR 126 each time, then NULL-derefs at ~139.9M steps. Next sweep: find the conversion that produced the '?'s.

### Main-window rendering (shared root fix)
- **SW-1471** (`loader.c`): `CW_USEDEFAULT` (INT_MIN) clamped at the 32-px minimum — every default-sized main window was a 32×32 sliver (Notepad++ portable AND installed WinRAR both hit this). First top-level `CW_USEDEFAULT` now sizes **1024×768** (same metric as SW-1269b `SW_MAXIMIZE`). Notepad++ frames went 32×32 → 1024×768; content paint (Scintilla children) is the next frontier.
- **SW-1472/1472b** (`loader.c`): base-image registry seeds for the standard `EventLog` service trio and Sysinternals `EulaAccepted` markers. psloglist64 went exit-2 ("Could not open the specified event log Registry key") → rc=0 with honest stub output.

### Popular-programs battery (all crash=0)
| program | verdict | notes |
|---|---|---|
| winrar (installer) | rc=0 ×3 | provisions sandbox tree |
| winrarinstd (installed) | alive @timeout | main window init reached (SW-1463) |
| npp64 (portable) | rc=0 ×3 | langs.xml loads; no dialogs |
| nppsetup (installer) | FRONTIER SW-1469 | real dialogs rendered; extraction phase reached |
| 7zsetup | rc=86 ×3 | program's own decision code, no crash |
| everything | alive @timeout | paint pending |
| tcpview64 | alive @timeout | paint pending |
| produkey | alive @timeout | paint pending |
| resourcesextract | rc=0 ×3 | clean |
| idman (installer) | rc=0 ×3 | clean |

Zero crashes across the entire battery.

## Gates

- `scripts/corpus_gate.sh`: **17 rows, rc=0 in 12 (was 9), crash total 0.** The 5 `xp.*` rows report MISSING honestly — those binaries may only come from licensed Windows XP media (operator places them in `.cache/miniwin/seed/`; never redistributed).
- `scripts/g1_accept.sh`: **G1 PIN OK** — clean build, no crash, regression 12/12.
- `scripts/repo_size_guard.sh`: **GUARD OK** (2 documented re-pin WARNs remain: shexview, hxd).

## Limitations (honest)

- Notepad++/WinRAR main windows paint full-size but their child content (Scintilla, rebar) is the next rendering frontier.
- The NPP installer stops at the unicode-NSIS system-DLL name garbling (SW-1469) — documented with diagnostic evidence.
- `xp.*` corpus seeds remain MISSING until licensed media is provided locally.
- Evidence stored: gate TSV, two real dialog screenshots, compact run logs (source-only policy respected — sandboxes/binaries stay untracked).
