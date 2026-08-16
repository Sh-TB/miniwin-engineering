# BUG-025: Packed UPX Crash — Missing Import Stubs (LoadLibraryA, ExitProcess)

## Status: ROOT CAUSE IDENTIFIED

## Summary
Packed UPX (`upx.exe`) crashes at RIP=0x215682 with SIGSEGV immediately upon entering its decompression stub. The crash is caused by two unresolved imports (`LoadLibraryA` and `ExitProcess`) whose IAT slots retain original name-RVA values, which the UPX stub calls as function pointers.

## Crash Evidence

```
[CRASH] Signal 11 at RIP=0x215682
  RAX=0x74 RCX=0x61564c RDX=0x15 R8=0x0 R9=0x0
  RSP=0x7ffcd60e9fe8 RBP=0xffffffffffffffff
  g_image_base=0x400000 g_image_size=0x216000
  (NOT inside loaded PE)
```

### IAT State Before EP Jump
```
IAT at image+0x215614 (KERNEL32.DLL):
  [0] 0x0000000000215682   ← LoadLibraryA (UNRESOLVED — name RVA, not a function pointer)
  [1] 0x0000000000215664   ← ExitProcess (UNRESOLVED — name RVA, not a function pointer)
  [2] 0x000000000200af80   ← GetProcAddress (resolved)
  [3] 0x0000000002006f60   ← VirtualProtect (resolved)
```

### Import Resolution Trace
```
Import DLL: KERNEL32.DLL (IAT RVA=0x215614, ILT RVA=0x0)
  [0] UNRESOLVED: LoadLibraryA
  [1] UNRESOLVED: ExitProcess
  [2] GetProcAddress -> 0x200af80
  [3] VirtualProtect -> 0x2006f60
Import DLL: msvcrt.dll (IAT RVA=0x21563c, ILT RVA=0x0)
  [0] atoi -> 0x2008a20
Import resolution: 3 resolved, 2 unresolved (total 5)
```

## Root Cause

The packed UPX PE imports 5 functions through its stub import table:
- KERNEL32.DLL: `LoadLibraryA`, `ExitProcess`, `GetProcAddress`, `VirtualProtect`
- msvcrt.dll: `atoi`

MiniWin's `g_import_table[]` contains stubs for `GetProcAddress`, `VirtualProtect`, and `atoi`, but **NOT** for `LoadLibraryA` and `ExitProcess`.

When the loader fails to resolve an import, it leaves the IAT slot unchanged. The original IAT slot value for an import-by-name entry is the **RVA of the Hint/Name entry** (bit 63 = 0, bits 0-30 = name RVA). For `LoadLibraryA`, this is `0x215682`.

When the UPX decompression stub calls `LoadLibraryA` through the IAT (`call qword ptr [IAT+0]`), the CPU reads `0x215682` from the IAT and jumps to that **absolute address**. Since the PE image is mapped at `0x400000` (covering `0x400000`–`0x616000`), address `0x215682` is **outside the image** and **not mapped** → SIGSEGV.

The correct absolute address for the name entry would be `0x400000 + 0x215682 = 0x615682`, but even that is the Hint/Name string — not executable code. The IAT slot must contain a function pointer, not a name RVA.

## Crash Chain

```
EP = 0x614450 (UPX decompression stub in UPX1 section)
  → UPX stub calls LoadLibraryA through IAT[0]
  → IAT[0] = 0x215682 (name RVA, not patched)
  → CPU jumps to 0x215682 (unmapped address)
  → SIGSEGV (Signal 11)
```

## Rejected Hypotheses

### H1: PE Relocation Issue (REJECTED)
**Hypothesis**: Image mapped at wrong base, relocations not applied, pointers invalid.

**Evidence against**:
- `.reloc` data directory is EMPTY: `RVA=0x0, Size=0x0` — packed UPX has no relocation table
- Image mapped at preferred base `0x400000` (MAP_FIXED_NOREPLACE succeeded)
- No delta between preferred and actual base (both 0x400000)
- The crash address 0x215682 is an IAT name RVA, not a relocated pointer
- All section data was correctly copied (verified with instrumentation)

**Verdict**: Relocation is completely irrelevant. The PE has no relocations and loads at its preferred base.

### H2: Image Mapping / Section Copy Error (REJECTED)
**Hypothesis**: Section data not correctly mapped, import directory contains garbage.

**Evidence against**:
- `.rsrc` section `PointerToRawData` = `0x89a00` (not 0x800 as initially misread)
- Data at `filedata+0x89a00` contains valid PE import directory entries
- After section copy, `image+0x2155d8` contains valid import directory with `KERNEL32.DLL`
- The import walk correctly found and resolved 3 of 5 imports

**Verdict**: Section mapping is correct. The initial confusion was caused by a Python analysis script that had swapped `SizeOfRawData` and `PointerToRawData` in its output format.

### H3: UPX0 BSS Not Writable (REJECTED)
**Hypothesis**: UPX0 section (BSS, where decompressed code goes) lacks write permission.

**Evidence against**:
- UPX0 characteristics: `0xe0000080` = MEM_WRITE | MEM_READ | MEM_EXECUTE | CNT_UNINITIALIZED
- mprotect correctly applies `PROT_READ | PROT_WRITE | PROT_EXEC`
- UPX0 is properly zeroed (verified: all zeros at `image+0x14f0`)
- The crash never reaches UPX0 — it crashes in the stub (UPX1 section) before decompression completes

**Verdict**: UPX0 permissions are correct. The crash happens before any UPX0 code runs.

## Proposed Minimal Fix

Add `LoadLibraryA` and `ExitProcess` stubs to `g_import_table[]`:

### LoadLibraryA
UPX uses this to dynamically resolve additional APIs from DLLs during decompression. The stub should:
1. Accept `(LPCSTR lpLibFileName)` parameter
2. Return a fake non-NULL HMODULE (so UPX doesn't treat it as failure)
3. Optionally: log the DLL name for diagnostics

Minimal implementation:
```c
static void* mw_LoadLibraryA(const char* name) {
    MW_TRACE("LoadLibraryA(%s) -> fake handle", name);
    return (void*)0xdead0001; /* Non-NULL module handle */
}
```

### ExitProcess
UPX calls this to terminate after printing output. The stub should:
1. Accept `(UINT uExitCode)` parameter
2. Call `_exit(uExitCode)` to terminate the process

Minimal implementation:
```c
static void mw_ExitProcess(unsigned int code) {
    MW_TRACE("ExitProcess(%u)", code);
    _exit(code);
}
```

### Risk Assessment
- **Scope**: 2 new entries in import table, 2 new stub functions
- **Regression risk**: Minimal — only affects code that calls these specific imports
- **Side effects**: `LoadLibraryA` returns a fake handle; if UPX later calls `GetProcAddress` with this handle, the existing `mw_GetProcAddress` stub will handle it

## Verification

After fix:
1. UPX should pass the IAT call to `LoadLibraryA` without crashing
2. UPX decompression stub should proceed further
3. Expect additional missing imports to surface as UPX decompresses and resolves its full import table
4. Ultimate goal: UPX reaches `RaiseException(0x20474343)` (GCC C++ exception during decompression)

## Related

- UPX packed PE structure: 3 sections (UPX0/BSS, UPX1/compressed, .rsrc)
- No .reloc directory (EXE loaded at preferred base)
- Exception dispatch chain (35/35 synthetic tests pass) — not involved in this crash
