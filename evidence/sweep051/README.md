# SWEEP-051 — SW-1261 LibreOffice (ODF display program) under miniwin

Deliverable: LibreOffice 26.8.1.1 x64 (the user-mandated ODF display program)
bootstrapped end-to-end under minwin_loader for the first time.

## Payload provenance (sha256 in payloads_sha256.txt)
- LibreOffice.msi 26.8.1.1 x64 = c6298b10 (sha-exact re-fetch, SWEEP-050 staging)
- payload CAB = the MSI's largest OLE stream (354,681,357 B), MS-CAB/LZX (typeComp 0x1503)
- extraction: libmspack (github kyz/libmspack master) built from source with gcc
  (no root, no system cabextract); scripts/sweep051_lo_extract.c -> 19427/19427
  files, 0 failures. Guest staging: artifacts/sweep051/guests/soffice/ (57 DLLs
  + soffice.bin + vcruntime/msvcp family; hyphen-renames: orcus-parser.dll,
  libcrypto-3.dll, libssl-3.dll, libxmlsec-mscng.dll)

## Root causes landed (all proven with instruments, no guessing)
1. **_get_stream_buffer_pointers was 8-arg, real UCRT is 4-arg** (wine
   ucrtbase.spec L380). Proof chain: MINWIN_DLL_VERIFY (image clean at load,
   391,175 reloc targets checked on mergedlo) -> ptrace DR0 watchpoint on
   msvcp140 img+0x1110 -> the writer = engine mw__get_stream_buffer_pointers
   `*wbufsz=0` -> [SBPTR] arg log showed wbufsz = 0x65e8's RETURN ADDRESS
   (msvcp140 stdio-init thunk RVA 0x65e8 forwards its frame; guest reads back
   ONLY the 3 register-arg slots). The v1 stub zeroed 8 bytes of msvcp140
   .text at the call's return point -> guest executed zeros -> SIGSEGV.
2. **Disk-DLL static TLS was skipped** (documented SW-509 v1 limit): sal3's CRT
   init read TEB->ThreadLocalStoragePointer[slot] = NULL -> SIGSEGV sal3 RVA
   0x6D654. Fixed: slot allocation, __tls_index write, g_tls_ptr_array publish,
   DLL_PROCESS_ATTACH callbacks, and a module registry (g_mod_tls_*) so guest
   threads get pristine per-thread copies (SW-1230 pattern extended to slots
   1..63).
3. **MW_DLL_MAX=12 + late slot registration**: re-entrant dep chains
   (clucene -> msvcp140 mid-load) duplicated images and exhausted the slot
   table, silently dropping mergedlo's tail deps (icuuc78/libxml2/skialo/
   pdfiumlo). Fixed: register the slot right after headers land (export dir
   parsed early), MW_DLL_MAX 12 -> 128. 57 DLLs now map once each.
4. **Alloc registry 8192 -> 524288**: mergedlo's DllMain init wave alone
   exceeds 8K live allocations; overflow made frees untracked -> HEAP-TRAP
   exit(4).
5. **DLL .pdata sanity cap 65536 -> 4M**: mergedlo's .pdata = 756,578
   RUNTIME_FUNCTIONs; the cap truncated it so the C++ EH unwinder ended the
   walk at mergedlo frames ("walk ends", unhandled throw). Now FH4 (real
   vcruntime140_1.dll) unwinds THROUGH mergedlo frames end-to-end.
6. **TlsAlloc collision**: v1 TlsAlloc wrote g_tls_ptr_array[idx]=NULL — that
   array is the static-TLS plane; the guest's TlsAlloc()=1..4 nulled cppu3's
   slot-4 block (SIGSEGV cppu3 RVA 0x13fc5). TlsAlloc now touches only the
   TEB+0x1480 TlsSlots space (real Windows separation).

## Measured state (binary 0d3b46b9)
- soffice.bin x3 DETERMINISTIC rc=99, 10 stub calls, ZERO crashes: 57 DLLs
  mapped (full ICU/NSS/OpenSSL/libxml2/pdfium/skia/clucene graph), msvcp140
  CRT init survives, sal3 TLS init survives, mergedlo 10K-initializer DllMain
  wave completes, CreateWindowExW -> HWND 0x10001 + WM_CREATE delivered, a
  guest C++ throw (0xe06d7363, cppuhelper3msc) unwinds through DLL frames via
  REAL __CxxFrameHandler4 to the UCRT _seh_filter_exe -> _exit(0xE06D7363).
- REMAINING FRONTIER (SW-1262): cross-DLL C++ catch-type matching — the
  uno::Exception catch handlers in mergedlo do not yet claim the throw, so
  the bootstrap exits via the UCRT unhandled-exception path instead of
  continuing into the VCL message pump.
- Census x3: 19 SWEEP-050 guests byte-identical to baseline (all exit-0, net
  rc=2, ping rc=1); DL corpus unchanged vs HEAD (winmergeu/putty/produkey/
  rammap/searchmyfiles/puttygen/bginfo/coreinfo/cports/diskmon exit-0,
  pageant/procexp/procmon 124 park, autoruns64/zoomit64 139 = A/B-verified
  pre-existing at HEAD c1d45aac, NOT a SWEEP-051 regression).
- G1 PIN 12/12 (evidence/master/g1/20261022_184613); calc battery 15/15.

## Instruments added (env-gated, honest)
- MINWIN_DLL_VERIFY=1: per-DLL full section verify vs file (reloc-aware,
  binary-searched targets)
- MINWIN_DLL_TRACE=1: memcpy args, [SBPTR], [WCSDUP] call logs
- [DLLSLOT] crash-dump lines: name every mapped DLL range + RIP attribution
- scripts/sweep051_watch.c: ptrace DR0 hardware-watchpoint tracer (catches
  the exact guest RIP of a store to any address)
