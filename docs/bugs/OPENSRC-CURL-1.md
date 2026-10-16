# OPENSRC-CURL-1 (OPEN) — curl.exe UCRT state-pointer crash after InitOnce wave

**Status:** OPEN — registered 2026-10-17
**Repro:** ./minwin_loader artifacts/opensource/curl/curl.exe --version
**Signature:** SIGSEGV at RIP=0x10000002b (a 0x100000000-tagged integer used
as a function pointer); 290/290 imports resolved; CRT init complete
(_initterm_e/atexit/__acrt_iob_func all ran); crash is deep inside UCRT
locale/state machinery where the UCRT encodes per-thread state pointers
with a 0x100000000 tag.
**Suspects:** UCRT `__acrt_locale_state` / `__acrt_get_locale_downlevel`
class accessors using our __p_-style data imports (state cell must contain
a pointer to a state STRUCT, not a value); _initterm_e initializers that
must return 0; missing `_initialize_onexit_table` /
`_register_thread_local_exe_atexit_callback`.
**Next micro-steps:** (1) disassemble the caller of the bogus pointer;
(2) map the UCRT state layout for the exact accessor; (3) implement the
missing _initialize_onexit_table pair; (4) re-run --version.
