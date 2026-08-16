# BUG-025 and Subsequent Work (from main working tree)

## Commits in working tree (not in engineering repo's main line)

### ee1efa5 - fix(BUG-025): add LoadLibraryA/ExitProcess stubs + GetProcAddress resolver
- Added LoadLibraryA stub (returns fake handle 0x12345678)
- Added ExitProcess stub (_exit wrapper)
- Added GetProcAddress resolver that searches import dispatch table
- These stubs allow UPX's CRT init to proceed past import resolution

### 97eb2dd - worklog: BUG-025 investigation and fix
- Investigation notes for UPX crash at RVA 0x1561
- Root cause: missing import stubs for KERNEL32.DLL functions
- Fix: added minimal stubs for LoadLibraryA, GetProcAddress, ExitProcess

### 76576ff - checkpoint: pre-EXP-035-establisher-frame-fix
- Checkpoint before EXP-035 (establisher frame discovery)
- State: 35/35 synthetic exception dispatch tests passing
- Issue: real UPX execution still crashes

### fa3f9d5 - session continuation checkpoint
- Context continuation from previous session
- 7-phase investigation plan established (not yet executed)

### fdb939c - fix: MAP_FIXED_NOREPLACE -> MAP_FIXED + fix inline asm
- **Critical fix**: Changed mmap flags from MAP_FIXED_NOREPLACE to MAP_FIXED
- MAP_FIXED_NOREPLACE could silently fail if address was occupied
- Fallback path mapped at random address WITHOUT relocations (not implemented)
- This caused all absolute addresses in PE to be wrong -> corruption
- MAP_FIXED forcibly replaces existing mapping at preferred ImageBase
- Also fixed broken inline assembly strings in mw_RaiseException

## Current State (post-fix)
- Build: SUCCESS (warnings only, no errors)
- UPX --version: C++ exception thrown (unhandled 'unsigned long long')
- PE loads at correct ImageBase (0x140000000)
- Import resolution works
- Next: C++ exception handling for UPX's error path
