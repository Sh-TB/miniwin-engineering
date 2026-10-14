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
              │         │ TASK-BUG-027 (READY, P0)   │         │
              │         │ landing-pad resume state   │         │
              │         │ (subsumes EXP-037/038)     │         │
              │         └──────────┬─────────────────┘         │
              │                    │                           │
              ▼                    ▼                           ▼
 ┌────────────────────┐ ┌──────────────────────┐ ┌────────────────────────┐
 │ TASK-API-006       │ │ TASK-API-005         │ │ TASK-API-003           │
 │ ALLOC +8 rule fix  │ │ EH hacks flag OFF    │ │ UEF wiring (unblocked) │
 │ (ALLOC_SMALL too)  │ │ (EH_FIX fired live)  │ │                        │
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
   TEST-008 ──► RELEASE_CRITERIA G1
```

Rules: a task is READY only when all arrows into it are VERIFIED/TESTED. One task per CONTINUE by default.
