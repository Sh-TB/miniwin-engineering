# SWEEP-092 — Repository size reduction: source-only, hash-pinned, reproducible test corpus

Commit: 5603693 (`SWEEP-092: source-only repository — hash-pinned on-demand guest artifacts`) — https://github.com/Sh-TB/miniwin-engineering/commit/5603693

## Root cause

Across 90 sweeps, downloaded guests, installers, extracted install trees and
per-run sandbox copies were committed as "evidence": `artifacts/` held
406 MB of downloads and guest trees, `evidence/**/sandbox/` held extracted
programs (often duplicated per sweep), `checkpoints/` held source backups.
Measured before: **555.8 MB tracked across 9,646 files**; pack 213.97 MiB.

## Main paths cleaned

- `artifacts/**` — all downloads, installers, extracted guests (untracked, worktree copies kept local)
- `evidence/**/sandbox/`, `evidence/**/Installers/`, `evidence/**/dl/` — runtime-only
- `evidence` binary categories (exe/dll/zip/7z/sfx/pdb/bmp/tar.gz), text dumps > 256 KiB (sweep008_rea function dumps, ghidra resources/functions TSVs)
- `checkpoints/`, `working-tree/`, `samples/` except the UPX regression fixture, tests guest copy + `.pdb`
- 6,803 tracked paths removed; source, tests, scripts, plugin manifests, docs and compact evidence untouched

## Manifest and tool paths

| Artifact | Path |
|---|---|
| Canonical hash-pinned manifest | `tests/corpus/manifest.tsv` (24 entries: id/version/arch/URLs/sha256/size/license/provenance/extraction/launch/profile) |
| On-demand artifact manager | `scripts/fetch_guest.sh` |
| Corpus parity gate (manifest-resolved) | `scripts/corpus_gate.sh` |
| Plugin runner (guest resolution, provision chain, `$SANDBOX_OF`/`$SNAPSHOT_OF` placeholders) | `scripts/plugin_run.sh` |
| Size guardrail | `scripts/repo_size_guard.sh` |
| Size audit tool + report | `scripts/repo_size_audit.sh`, `docs/audit/repository_size_audit_20261027.md` |
| Guest workflow documentation | `docs/GUEST_ARTIFACTS.md` |
| Install/run contract extractor | `scripts/install_contract.py` |
| Guest compiler/language/prereq profiler | `scripts/guest_profile.py` |

## Tests executed and results

| Check | Result | Class |
|---|---|---|
| Clean fresh clone (`git clone --no-local`) | 2,843 files / 21.8 MB | TESTED |
| Build from source in the clone | `make` rc=0, binary 4.63 MB | TESTED |
| G1 acceptance pin (clean build + UPX + regression) | PIN OK, binary sha 38a787f8, regression 12/12 | TESTED (evidence/master/g1/20261027_151640) |
| Corpus parity via manifest pipeline | 21/21 rows rc=0, crash=0, fatal=0 (xp.winmine/sol/freecell/calc/spider + breakout/snake) | TESTED (evidence/sweep092/corpus_parity.tsv) |
| Fresh-clone GUI run from verified cache | ProduKey: fetch → verify → PASS_LAUNCH_RENDER (frames=1) | TESTED |
| Cache reuse (second run) | VERIFIED (cached), no re-download | TESTED |
| Planted bad SHA-256 | refused rc=3, artifact not staged, execution refused | TESTED (evidence/sweep092/fetch_tests.tsv) |
| Offline cache miss | clear BLOCKED rc=2, actionable message | TESTED |
| Guardrail catches planted artifact | GUARD FAIL with exact path + remediation (disposable clone) | TESTED |
| Untracked-after-tests | only `evidence/<sweep>/` outputs remain untracked; no guest ever tracked | TESTED |
| Install→run chain (WinRAR 7.12) | provision run rc=0 crash=0 → installed into `Program Files\WinRAR` (SW-1418 ProgramFilesDir seed honored) → reg seed 1756 B staged (SW-1419) → installed payload launched in-tree, rc=124 TIMEOUT crash=0 (SW-1417 first-run dialog frontier) | OBSERVED |
| Simple games actually running | winmine (4 frames), sol, freecell, breakout, snake rc=0 ×3 each | OBSERVED |
| Simon Tatham puzzle family (flood/fifteen/mines/towers/lightup) | all rc=139 at one unresolved-import frontier during CRT `_initterm` (VCRUNTIME140/UCRT guests) | PARTIAL — SW-1415 queued |
| shexview / hxd manifest rows | URL reachable but full hash unavailable (artifact deleted pre-pin / download unavailable) | BLOCKED — documented re-pin follow-up |

## Before / after sizes

Measured with:
```bash
git ls-files -z | xargs -0 -r stat -c%s | awk '{s+=$1} END {print s}'   # tracked bytes
git count-objects -vH                                                    # pack size
git ls-files | wc -l                                                     # file count
```

| Metric | Before | After |
|---|---|---|
| Tracked bytes | 555.8 MB (9,646 files) | **21.8 MB (2,843 files)** — 96% reduction |
| Fresh-clone tree | — | 21.8 MB + on-demand verified guests |
| Guest binaries tracked | 198 exe / 55 dll / 46 zip | 0 guests (only whitelisted unit-test fixtures + UPX sample) |
| Git pack (history) | 213.97 MiB | 213.97 MiB (unchanged — see limitation) |

## Remaining limitations

1. **Historical blobs (~214 MiB pack) remain reachable in git history.**
   Removing them requires `git filter-repo` + force-push, which conflicts
   with the project's standing no-force-push discipline and would invalidate
   every commit SHA referenced in this issue. Current-tree policy + guardrail
   prevent any re-introduction. A one-time history rewrite remains available
   if the owner approves it.
2. Five manifest rows are `UNVERIFIED_URL` (hash pinned from previously
   verified artifacts; exact release URL to re-confirm: npp ×2, notepad2mod,
   renamer, winnc) — `fetch_guest` refuses substitutes until re-pinned.
3. SEED guests (XP-era binaries) have no legitimate public download; they
   verify against pinned SHA-256 from a locally supplied copy, so a fresh
   clone reports them honestly as `MISSING` until seeded.

## Engineering addendum (install/run contracts)

- WinRAR 7.12 installer: 29 payload files under `Program Files\WinRAR`
  (WinRAR/UnRAR/Rar/Uninstall exes, 3 DLLs, 6 SFX modules, RarExt shell
  extension + msix); registry: HKLM ×14 (incl. `ProgramFilesDir`) +
  `HKCU\Software\WinRAR SFX`; the installed WinRAR then reads back its
  installer-written profile/settings keys at launch — the install→run
  registry chain is now measured end to end (`evidence/sweep092/winrar_install_contract.tsv`).
- Guest profiles: WinRAR.exe = C/C++ static CRT; notepad++.exe = C/C++
  static CRT; NPP setup = NSIS; flood.exe = MSVC 2015+ UCRT
  (`docs/analysis/guest_profiles.tsv`).

 distinction: TESTED = executed here with recorded output; OBSERVED =
 ran and measured but frontier-limited; PARTIAL = runs partially (documented
 frontier); BLOCKED = external dependency unavailable.
