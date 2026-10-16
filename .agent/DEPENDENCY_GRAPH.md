# DEPENDENCY GRAPH — task-level

```
                        ┌────────────────────────────┐
                        │ TASK-BUG-026 (TESTED ✓)    │
                        │ g_cap_rip operand fix      │
                        │ regression 12/12 restored  │
                        └──────────┬─────────────────┘
                                   │
                        ┌──────────▼─────────────────┐
              ┌─────────│ TASK-EXP-036 (TESTED ✓)    │─────────┐
              │         │ live re-run: new failure = │         │
              │         │ BUG-027 (RSP+8, RDX sel.)  │         │
              │         └──────────┬─────────────────┘         │
              │                    │                           │
              │         ┌──────────▼─────────────────┐         │
              │         │ TASK-BUG-027 (TESTED ✓)    │         │
              │         │ slot-stride decode fix     │         │
              │         │ landing pad resumes OK     │         │
              │         └──────────┬─────────────────┘         │
              │                    │                           │
              │         ┌──────────▼─────────────────┐         │
              │         │ TASK-BUG-029 (READY, P0)   │         │
              │         │ terminate after correct    │         │
              │         │ landing-pad resume         │         │
              │         └──────────┬─────────────────┘         │
              │                    │                           │
              │         ┌──────────▼─────────────────┐         │
              │         │ TASK-API-005 (first step)  │         │
              │         │ EH hacks flag OFF          │         │
              │         └──────────┬─────────────────┘         │
              │                    │                           │
              ▼                    ▼                           ▼
 ┌────────────────────┐ ┌──────────────────────┐ ┌────────────────────────┐
 │ TASK-API-006       │ │ TASK-API-003         │ │ TASK-TEST-004/005      │
 │ ALLOC_LARGE rules  │ │ UEF wiring           │ │ dispatch tests +       │
 │ (4 copies + −2)    │ │                      │ │ Tier-B builder         │
 └─────────┬──────────┘ └──────────┬───────────┘ └───────────┬────────────┘
           └──────────────┬────────┴──────────────────────────┘
                          ▼
        ┌─────────────────────────────────────────────┐
        │ PHASE 04 exception correctness exit gate    │
        │ (Tier-B exception suite green, no hacks)    │
        └───────┬─────────────────────┬───────────────┘
                │                     │
   ┌────────────▼───────┐   ┌─────────▼──────────────────────┐
   │ TASK-TEST-002      │   │ TASK-TEST-004/005              │
   │ ALLOC unit tests   │   │ dispatch tests + Tier-B builder│
   └────────────────────┘   └────────────────────────────────┘

 Independent small tasks (no upstream deps; runnable anytime):
   TASK-API-002 (trace flush)      TASK-API-003 (UEF wiring; after BUG-026)
   TASK-TEST-001 (unit port)       TASK-DOC-001/002 (doc hygiene)

 Gate chains:
   D-MAP-A/B/C ──► MAPPING-01..07 ──► G5 ──► PHASE 06 generalization
   API-007 ──► TEST-006 (Tier-D)
   TEST-008 ──► RELEASE_CRITERIA G1   [G1 MET 2026-10-15 — EXP-039]

 Post-G1 guard chain (new, CONTINUE #5):
   TEST-004 ──► TEST-005 ──► G2 Tier-B ──► release
   G1-001 (pin script) ──► any loader.c change
   TEST-002 ──► API-006 ──► TEST-009 (decoder fuzz)
   BUG-033 (continuation walk) ──► TEST-005 cases
```

Rules: a task is READY only when all arrows into it are VERIFIED/TESTED. One task per CONTINUE by default.

## CONTINUE #6 additions (2026-10-15, appcompat wave)
```
BUG-037 (CRT-startup UNWIND decode) ──► BUG-038 (G2 C++ EH acceptance)
TASK-TLS-001 (DONE) ──► TLS-002 (thread callbacks) ──► API-011 (threads)
API-010 (callback bridges: signal/beginthreadex/UEF/AVEH) ──► API-011
API-009 (wave 3: 44 APIs) ──► TEST-011 (suite to 15) ──► TEST-012 (golden)
TEST-013 (ABI boundary unit tests) ──► TEST-014 (printf repack fuzz)
API-019 (VirtualAlloc) ──► API-022 (Heap family)
INFRA-003 (sweep runner repo-local) ──► BENCH-001 (API db automation) ──► BENCH-002 (trend)
INFRA-004 (toolchain pin script) ──► DOC-010 (toolchain guide)
```
Rule unchanged: READY only when all arrows in are TESTED/VERIFIED. Sweep suite (tests/appcompat/)
is now the API-tier guard: any new ENTRY row needs a program that exercises it.
