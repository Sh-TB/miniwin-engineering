# OPENSRC-7ZIP-3 (OPEN) — 7z.exe full operations need guest LoadLibraryW of 7z.dll

**Status:** OPEN — registered 2026-10-17
**Repro:** ./minwin_loader artifacts/opensource/7zip/full/7z.exe a t.7z f
**Observed:** banner + help work (exit 0, 0 unresolved imports); `a`/`x`
SIGSEGV right after "Scanning the drive for archives:" — 7z.exe dynamically
loads 7z.dll (LoadLibraryW) for format handlers; the loader has no
multi-module PE loading yet (module table, per-module IAT resolution,
DllMain(DLL_PROCESS_ATTACH), dependency graph).
**Next micro-steps:** TASK-LOADER-DLL — PE DLL loader: map section headers,
resolve exports (EAT), relocate (BASE RELOC), run TLS+DllMain, integrate
with GetProcAddress/LoadLibrary/FreeLibrary identity space.
