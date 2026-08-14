# Mission

Build and improve a lightweight Windows x64 PE compatibility runtime capable of
loading and executing Windows PE applications on Linux without Wine.

## Target

UPX 4.2.4 (upx_decompressed.exe) — `--version` should print "upx 4.2.4" and exit 0.

## Execution Pipeline

```
PE file analysis
    ↓
PE loading (10 sections, relocations)
    ↓
Import resolution (162/162 KERNEL32 + msvcrt)
    ↓
Win32 API compatibility layer (162 stubs)
    ↓
Runtime execution (TEB/PEB, heap, CRT init)
    ↓
Exception handling (SEH/CFEH)
    ↓
Successful program execution
```

## Current Blocker

M8: RtlUnwindEx context restoration incomplete. UPX reaches the catch landing
pad (0x40331c) but crashes with SIGSEGV because nonvolatile registers and/or
stack state are incorrect after the unwind.
