# MiniWin Engineering Rules v1.0

> Based on SharpEmuT24 PS5 Emulator Lessons

## Mission

Build a self-evolving Windows compatibility runtime.

The goal is NOT to clone Windows.
The goal is:

> **"Run real Windows applications by implementing only the minimum behavior required, while continuously learning from evidence."**

---

## RULE 1 — Evidence First, Code Second

Never implement an API because documentation says it exists.

Every implementation requires evidence:
- Application name
- Binary hash
- Import list
- Runtime trace
- Arguments
- Return values
- Memory behavior
- Failure location

**No guess-based compatibility.**

---

## RULE 2 — Never Hide Failures

Every failure is a discovery.

A crash must create `BUG-XXX.md` containing:
- Application
- RIP
- Fault address
- Missing API
- Expected behavior
- Root cause
- Fix
- Regression test

**A crash that is ignored becomes a future unknown.**

---

## RULE 3 — Build From Smallest Working Target

Never jump directly to complex applications.

Required progression:
1. **Stage 1:** Hello PE
2. **Stage 2:** Simple C executable
3. **Stage 3:** CLI tools
4. **Stage 4:** Small GUI apps
5. **Stage 5:** Complex applications

> Example: Do not test Photoshop before Notepad.

---

## RULE 4 — Separate Layers

Never mix problems.

```
PE Loader
    |
API Resolver
    |
Win32 Compatibility Layer
    |
CRT Runtime
    |
GUI Layer
```

- A failure in CRT is not a loader bug.
- A missing API is not an application bug.

---

## RULE 5 — Every Feature Needs a Test

Adding `VirtualAlloc` requires `VirtualAlloc_test.exe`.
Adding `CreateWindowEx` requires GUI regression test.

**No untested features.**

---

## RULE 6 — Checkpoint Before Major Changes

Before any major change (loader rewrite, API table rewrite, architecture change):

Create `miniwin_checkpoint_<date>.zip` containing:
- `src/`
- `docs/`
- `tests/`
- `logs/`
- Binary hashes

**Never destroy the only working version.**

---

## RULE 7 — Oracle Comparison

Use Wine and Windows as references.
Wine is not the target. Wine is an oracle.

Compare MiniWin (API calls, arguments, returns, memory) against Windows/Wine behavior.
Find differences.

---

## RULE 8 — Implement Behavior, Not Names

A function name is not compatibility.

Example: `VirtualProtect()` is not finished when it returns TRUE.
Need:
- Correct memory permissions
- Correct failure cases
- Correct alignment
- Correct side effects

---

## RULE 9 — AI Assisted Evolution

The runtime must collect knowledge.

Every application run produces API requirements:
```
app.exe needs:
  kernel32: VirtualAlloc, HeapAlloc, CreateFileA
  user32:   CreateWindowExA
```

This becomes the **MiniWin Knowledge Database**.

---

## RULE 10 — Prefer Minimal Implementation

Do not implement Windows completely.

Implement: **"The smallest Windows that runs the maximum software."**

Priority:
1. Kernel32
2. Ntdll
3. CRT
4. User32
5. GDI
6. Network

---

## RULE 11 — No Fake Success

A program starting is not proof.

Success requires:
- **CLI:** correct output
- **GUI:** correct rendering screenshot
- **Tool:** correct operation

---

## RULE 12 — Knowledge Must Survive

Every discovery goes into `docs/`:
- `API_DATABASE.md`
- `BUG_DATABASE.md`
- `COMPATIBILITY_MATRIX.md`
- `ARCHITECTURE.md`

**The project must be understandable years later.**

---

## FINAL PRINCIPLE

> **SharpEmu lesson:** "We do not know the whole system. We discover the system through controlled experiments."

> **MiniWin principle:** "We do not recreate Windows. We discover the minimum Windows required by applications."

Every application is a new experiment.
Every crash is new knowledge.
Every fix becomes permanent capability.
