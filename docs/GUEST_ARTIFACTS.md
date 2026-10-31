# Guest Artifacts — On-Demand, Hash-Pinned Workflow

The repository stores **guest metadata only** (URLs, versions, SHA-256 pins,
sizes, licenses). Downloaded Windows guests, installers, archives, extracted
trees, sandboxes, and generated binaries are **never tracked** in Git.

## Fetch a guest

```bash
scripts/fetch_guest.sh <guest-id>              # download + SHA-256 verify + cache
scripts/fetch_guest.sh --list                  # all manifest entries
scripts/fetch_guest.sh --json <guest-id>       # machine-readable report
scripts/fetch_guest.sh <guest-id> --extract    # also extract (path-traversal guarded)
```

Cache location: `.cache/miniwin/guests/<id>/` (Git-ignored). A verified cache
entry is reused; it is never overwritten by a partial or unverified download.

## Guest classes (tests/corpus/manifest.tsv)

| class           | meaning                                                        |
|-----------------|----------------------------------------------------------------|
| `https://` URL  | official upstream download; hash-pinned                        |
| `UNVERIFIED_URL`| hash pinned from a prior verified download; URL must be re-pinned before fetch (fetch_guest refuses substitutes) |
| `SEED`          | no legitimate public download (Microsoft Windows XP binaries). Copy the file into `.cache/miniwin/seed/` (or `$MINWIN_GUEST_SEED`) from your own licensed media; fetch_guest verifies the pinned SHA-256 before use |
| `GENERATED`     | produced in-sandbox by running the producing plugin (e.g. the WinRAR installer run) |
| `BLOCKED`       | acquisition page recorded; artifact unavailable                |

## Run tests

```bash
SWEEP=sweepNNN bash scripts/plugin_run.sh <plugin> [runs] [timeout]   # one plugin
bash scripts/corpus_gate.sh [sweep]                                   # 7-guest parity battery
bash scripts/g1_accept.sh                                             # build + UPX + regression pin
```

Plugins resolve guests via their manifest `guest` field →
`tests/corpus/manifest.tsv` → verified cache. Installer-produced trees are
expressed with placeholders (`$SANDBOX_OF:winrar`, `$SNAPSHOT_OF:winrar`) and
generated on demand by the `provision` chain.

## Clear the cache

```bash
rm -rf .cache/miniwin/guests/     # everything re-downloads + re-verifies
rm -rf .cache/miniwin/            # includes seed staging area
```

## Reproduce a failed run

1. `scripts/fetch_guest.sh --json <id>` — shows expected vs observed SHA-256.
2. A hash mismatch **fails closed** (exit 3): the artifact is not staged and
   never executed.
3. Offline mode: `MINWIN_OFFLINE=1` uses only the verified cache and fails
   with an actionable `BLOCKED` message otherwise.
4. Runtime outputs (sandboxes, frames, traces) stay under
   `evidence/<sweep>/` and `miniwin-results/` — Git-ignored, local-only.

## Size policy enforcement

```bash
scripts/repo_size_guard.sh            # fails with exact path + remediation
scripts/repo_size_audit.sh            # compact inventory report
```
