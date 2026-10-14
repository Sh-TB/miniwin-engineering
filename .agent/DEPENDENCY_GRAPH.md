# DEPENDENCY GRAPH — task-level

```
                        ┌────────────────────────────┐
                        │ TASK-BUG-026 (READY, P0)   │
                        │ g_cap_rip operand fix      │
                        └──────────┬─────────────────┘
                                   │
                        ┌──────────▼─────────────────┐
              ┌─────────│ TASK-EXP-036 live re-run   │─────────┐
              │         └──────────┬─────────────────┘         │
              ▼                    ▼                           ▼
 ┌────────────────────┐ ┌──────────────────────┐ ┌────────────────────────┐
 │ TASK-EXP-037       │ │ TASK-EXP-038         │ │ TASK-API-005           │
 │ EXP-035 arithmetic │ │ OriginalContext/RDX  │ │ EH hacks flag OFF      │
 │ live verify        │ │ (ADR-008)            │ │ (API-005)              │
 └─────────┬──────────┘ └──────────┬───────────┘ └───────────┬────────────┘
           └──────────────┬────────┴──────────────────────────┘
                          ▼
        ┌─────────────────────────────────────────────┐
        │ PHASE 04 exception correctness exit gate    │
        │ (Tier-B exception suite green, no hacks)    │
        └───────┬─────────────────────┬───────────────┘
                │                     │
   ┌────────────▼───────┐   ┌─────────▼──────────────────────┐
   │ TASK-API-006       │   │ TASK-TEST-004/005              │
   │ ALLOC_LARGE fix    │   │ dispatch tests + Tier-B builder│
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
