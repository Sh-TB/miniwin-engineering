# NEXT ACTION (single, most-restrictive next step)

**Root cause:** BUG-038 lineage — C++ catch clause selection for TRIVIAL types
(int / std::string) inside a function with MULTIPLE catch clauses lands on the
wrong handler clause; a stack slot gets clobbered with a heap pointer during
same-frame catch (probe: ok → 46681368 = 0x2C9xxxx). Cross-frame std::exception
catch + destructor unwinding are CORRECT (t08: dtors=5, what()=="deep-boom").

**Reproduce:**
./minwin_loader tests/corpus/bin/t08_probe_exc.exe   # expect p2 len=0, ok counter clobbered
./minwin_loader tests/corpus/bin/t08_big_cpp_stl.exe # expect score=159 (missing r0=32, r1=64)

**Evidence to collect next:** LSDA catch-clause type-matching table walk for the
multi-clause case; the value delivered as catch selector (RDX per API-005
4-param protocol ER) at the landing pad for throw 42 vs throw std::string.

**Acceptance criteria:** t08_big_cpp_stl exit 0 with score=255, 3× deterministic,
regression 12/12 stays green, UPX G1 stays green. References: MS x64 EH doc +
LLVM Win64EH.h (Issue #1 §7), Wine dlls/ntdll/unwind.c personality paths.
