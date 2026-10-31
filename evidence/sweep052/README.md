# SWEEP-052 — Train 1: SW-1262 cross-DLL C++ catch matching (the SWEEP-051 frontier)

Binary sha256 69a015b97a00b6b18c2a74a80233caf4d96563f6c571b4a9d9eb96a10a930c98
(G1 PIN 12/12 at evidence/master/g1/20261022_195936).

## The frontier as inherited

SWEEP-051 SW-1261 stage 1 left soffice.bin deterministic rc=99: the guest's own
C++ throw (0xE06D7363 from cppuhelper3MSC) unwound through real FH4 frames but
"the uno::Exception catch handlers in mergedlo do not yet claim the throw",
escaping to the CRT top-level __except (_seh_filter_exe -> _exit(0xE06D7363),
rc 99 = 0x63).

## Decode chain (instruments, not guesses)

1. **FH4 throw-record format is IMAGE-RELATIVE.** Disassembly of the guest's
   REAL vcruntime140.dll!_CxxThrowException (RVA 0x5230, scripts decode in
   evidence/sweep052/ README chain) proves: EI[0] = 0x01994000 when the
   thrower's own RtlPcToFileHeader(ThrowInfo) FAILS, EI[0] = 0x19930520 when
   it succeeds — and in BOTH cases EI[2] = ThrowInfo4 (four DWORD RVAs) and
   **EI[3] = the THROWING IMAGE BASE** (the 4th RaiseException parameter).
   TheThrowInfo4/CatchableTypeArray4/TypeDescriptor chain decodes cleanly from
   disk (scripts/sweep052_eh_decode.py): cppuhelper3MSC RVA 0x145bd8 throws
   '.?AVDeploymentException@uno@star@sun@com@@' with the 3-entry base chain
   DeploymentException -> RuntimeException -> Exception.
   __CxxFrameHandler4 (vcruntime140_1 RVA 0x3fd0) resolves the handler's
   FuncInfo4 as dc->ImageBase + *(DWORD*)dc->HandlerData (confirmed in its
   prologue) and needs EI[3] for the throw side.

2. **ROOT CAUSE #1 — engine RtlPcToFileHeader knew only the EXE image.**
   mw_RtlPcToFileHeader v1 returned NULL for guest-DLL addresses, so the
   guest's own _CxxThrowException passed EI[3]=0 and the real FH4 matcher
   resolved every ThrowInfo4 RVA against base 0 — the CatchableTypeArray at
   ~0x145b90 (unmapped) — so NO catch could EVER match, not even
   catch(uno::Exception&) with the base chain right there in the throw's own
   CatchableTypeArray. Fix: consult the guest-DLL registry (mw_module_of)
   exactly like real Windows consults its module list.

3. MEASURED after fix #1: the first DeploymentException throw is now CLAIMED
   — mergedlo's FH4 handler returns ExceptionExecuteHandler and the dispatch
   longjmps to the catch landing pad (RtlUnwindEx target RIP in mergedlo,
   evidence/sweep052/catch_claimed.txt). Cross-DLL C++ catch-type matching
   WORKS end to end: cppuhelper3MSC throw -> mergedlo catch(uno::Exception&).

4. **ROOT CAUSE #2 — AccessCheck v1 answered FALSE unconditionally.** With the
   first throw claimed, bootstrap proceeded and threw a SECOND
   DeploymentException. The [CXEH] dispatch decoder (message dump for the
   non-polymorphic uno::Exception whose object+0 is the Message rtl_uString*)
   printed the exact chain:
     throw 1: 'Cannot open uno ini file:///C:/.../uno.ini'   (SW-1261 staged
       only soffice.ini; uno.ini/fundamental.ini/types.rdb/services.rdb were
       left in lo_extracted — staged now)
     throw 2: 'Cannot obtain UNO_SERVICES from uno ini' — sal3's pre-open
       access check (GetNamedSecurityInfoW -> DuplicateToken -> AccessCheck
       0x120116) got FALSE from the engine and concluded the files were
       inaccessible. Fix: mw_AccessCheck v2 implements REAL DACL semantics
       over the engine's absolute SECURITY_DESCRIPTOR (DACL present + NULL =
       grant everyone, per the Windows contract; real ACE walk with DENIED
       first, ALLOWED second, SIDs matched against the session user +
       Administrators; generic->file-specific mapping). mw_DuplicateTokenEx
       v2 mints a REAL new handle to the session token; DuplicateToken (the
       3-arg form sal3 calls) added to the advapi32 table.

5. MEASURED after fixes #1+#2+staging: soffice x3 DETERMINISTIC rc=99 with
   stub_calls 10 -> 8 (DuplicateToken resolved); bootstrap reaches the
   services.rdb stage. New frontier SW-1263 (next train): sal3's
   osl_openFile receives PROGRESSIVELY TRUNCATED directory prefixes of the
   correct path — 'C:\artifacts\s' / 'C:\artifacts\sweep051\guests\' /
   'C:\artifacts\sweep051\' / 'C:\artifacts\sweep051\gu' — the cut point
   moves with allocator layout (MINWIN_HEAP_GUARD=1 moves it to '...gu'),
   pointing at a corrupt/short rtl_uString length in the URL->system-path
   chain inside sal3's bootstrap (62 identical failed opens, then
   'Cannot obtain UNO_SERVICES'). DR0 watchpoint data collected
   (evidence/sweep052/ watch runs): the buffer is written byte-wise as
   "file:///" by sal3's converter, then re-read as UTF-16 — an
   allocator/length-header corruption class, SW-1263's target.

## Instruments added (engine)

- [CXEH] dispatch-time FH4 throw decoder: throwing image + ThrowInfo4 fields
  + CatchableType names + uno::Exception Message (UTF-16) + object RTTI probe.
- [STUB] lines now carry the import NAME (bind-time slot registry keyed by
  the ff 15 call slot; both EXE and guest-DLL bind paths).
- CreateFileW caller attribution + path probe (diagnostic, kept env-cheap).
- GetModuleFileNameW output logging (bootstrap provenance).

## Regression gate (evidence/sweep052/regression.tsv)

All rows byte-comparable to the SWEEP-051 baseline: mspaint/mshearts/wordpad/
drwtsn32/calc/winmine/notepad/write/winver/sol/freecell/everything/putty/
winmergeu exit-0 x3; pageant 124 x3; zoomit64 139 x3 (pre-existing);
procexp64 124 x3 (its SWEEP-051 baseline value — line 77 of
artifacts/sweep051/census/census.tsv); soffice 99 x3 (stub 8 vs 10 = the
DuplicateToken fix, the only delta). G1 PIN 12/12. calc battery unchanged.
