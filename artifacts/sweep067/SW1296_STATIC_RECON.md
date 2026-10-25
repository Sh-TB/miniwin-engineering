# SW-1296 — Static Recon: 4 Tiny NirSoft PE32 Guests (capstone + pefile)

- Task ID: 2-a (recon only, no source edits)
- Date: 2026-10-25
- Toolchain: python3.13 + pefile 2024.8.26 + capstone 5.0.7
- Script: `artifacts/sweep067/scripts/recon4.py` (+ `static_entry.py`, `static_entry2.py`)

## 1. Summary table

| exe | size | sha256 (16) | ImageBase | EP | sections | UPX? | imports | DLLs | `ff 15` sites in exec sections | sites resolving to real IAT |
|---|---|---|---|---|---|---|---|---|---|---|
| DriverView.exe | 36,352 | c5611a45ae087277 | 0x400000 | 0x414250 | UPX0/UPX1/.rsrc | **yes** | 13 | 10 | 1 | 0 |
| HeapMemView.exe | 42,080 | 1296d8731ad803bc | 0x400000 | 0x4140c0 | UPX0/UPX1/.rsrc | **yes** | 13 | 8 | 1 | 0 |
| HtmlAsText.exe | 22,016 | 42624eb45a966980 | 0x400000 | 0x40d120 | UPX0/UPX1/.rsrc | **yes** | 13 | 8 | 2 | 0 |
| faview.exe | 46,176 | a68fae179a57064a | 0x400000 | 0x415f60 | UPX0/UPX1/.rsrc | **yes** | 14 | 9 | 2 | 0 |

**All four are UPX-packed.** Every `ff 15` byte-pattern found in executable
sections is a coincidence inside UPX1 (compressed/stub bytes); **zero** static
indirect call sites resolve to a real import — e.g. DriverView's only hit is
`0x40D04B → [0x40B05C]` (a decompressed-data address, not the packed IAT),
HtmlAsText's are `[0xDB407018]`/`[0x83F48E99]` (garbage VAs).

## 2. The "tiny tool" import signature (UPX stub table)

Every file's *packed* import table is the UPX stub's minimal set: the 4 API
slots the stub needs (LoadLibraryA / ExitProcess / GetProcAddress / VirtualProtect
— heapmemview, htmlastext and faview also carry VirtualAlloc/VirtualFree) plus
**exactly one dummy named import per system DLL** (forces the OS loader to map
each DLL so the stub can LoadLibrary/GetProcAddress it):

| exe | kernel32 slots | dummy-per-DLL imports |
|---|---|---|
| DriverView | LoadLibraryA, ExitProcess, GetProcAddress, VirtualProtect | ADVAPI32!RegCloseKey, COMCTL32!Ordinal_17, comdlg32!FindTextA, GDI32!SetBkMode, msvcrt!exit, ole32!DoDragDrop, SHELL32!ShellExecuteA, USER32!GetDC, VERSION!VerQueryValueA |
| HeapMemView | + VirtualAlloc, VirtualFree | ADVAPI32!RegDeleteKeyA, COMCTL32!Ordinal_17, GDI32!PatBlt, msvcrt!exit, SHELL32!SHGetMalloc, USER32!GetDC, comdlg32!FindTextA |
| HtmlAsText | + VirtualAlloc, VirtualFree | ADVAPI32!RegSetValueExA, COMCTL32!Ordinal_17, GDI32!LineTo, msvcrt!exit, SHELL32!ShellExecuteA, USER32!GetDC, comdlg32!GetOpenFileNameA |
| faview | + VirtualAlloc, VirtualFree | ADVAPI32!RegCloseKey, COMCTL32!Ordinal_17, GDI32!SetBkMode, msvcrt!exit, ole32!CoInitialize, SHELL32!SHGetMalloc, USER32!GetDC, comdlg32!FindTextA |

GUI-dialog markers present in all four: COMCTL32!Ordinal_17
(= InitCommonControls, per SW-1204c), a comdlg32 common-dialog import,
USER32!GetDC, GDI32 output call. This is the classic Borland/Delphi-ish tiny
NirSoft GUI tool profile — **class confirmed for all 4**.

## 3. Runtime call shape (DriverView, proven by trace + step log)

The real import call graph does not exist statically. At runtime the stub:
1. decompresses 0xC000+ bytes to 0x401000.. (~794k interpreted steps),
2. `LoadLibraryA(dll)` per import-array entry (first: "KERNEL32.DLL"),
3. `GetProcAddress(hmod, name)` per import, storing results into the original
   IAT (0x40B064.. for DriverView), ExitProcess on failure,
4. `VirtualProtect`, then jumps to the unpacked OEP.

DriverView died at (3) before its second API completed (see
SW1293b_DRIVERVIEW_DECODE.md — REP-SCAS ESI bug, not an import problem).

## 4. Class-test verdict (does static capstone mapping substitute for Ghidra discovery?)

**No — for UPX-packed tiny tools, static import-call mapping is vacuous.**
- 13–14 pseudo imports are stub plumbing, not the program's API surface.
- 0 of 1–2 `ff 15` sites resolve to imports; the stub calls its 4 slots via
  register-relative forms (`call [esi+0x15364]` etc.), invisible to ff 15 scans
  and only resolvable by emulating the stub.
- Ghidra on the packed file sees the same stub, not the tool.

**What does substitute adequately:** the loader's own dynamic surface —
`MW32_TRACE=1` [API32] lines see every stub GetProcAddress resolution and every
guest API call with args (that is exactly how the DriverView decode was closed);
optionally `upx -d` the artifacts once, then re-run pefile/capstone on the
unpacked images for a true static view. Recommendation for SW-1296: treat
"static IAT map" as N/A for the UPX class and standardize on (a) one
`upx -d`-unpacked static pass, plus (b) the [API32] runtime trace as ground truth.

## 5. Reproduction commands

```
python3.13 artifacts/sweep067/scripts/recon4.py          # table above
cd /home/z/my-project/workspace/miniwin-engineering
MW32_TRACE=1 MW32_MAX_STEPS=800000 timeout 120 ./minwin_loader \
    artifacts/sweep062/x/driverview/DriverView.exe \
    > /tmp/dv67_stdout.txt 2> /tmp/dv67_stderr.txt; echo rc=$?   # rc=139, 794,071 steps
```
