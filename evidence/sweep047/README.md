# SWEEP-047 (continue the train) — 20 trains (2026-10-22)

Directive: (continue the train) (standing 20-train mandate). Base: origin/main a418e5c
(SWEEP-046); fast-forward (local 0 unique commits), tree clean, baseline
rebuilt byte-exact 848fa108. adexplorer64 corpus re-recovered for the 7th
time (AdExplorer.zip sha 56536bff byte-exact, exe e4512878 pinned).

## Train 1 — SW-791 curl.exe (mingw 8.23.0, Schannel build) — --version exit 0

Four engine wagons, each decoded off the guest's own behavior:

1. UCRT CRT-init tail: 3 unresolved imports failed libcurl's static-CRT init
   (_byteswap_ulong/_byteswap_ushort from api-ms-win-crt-utility,
   freopen_s from api-ms-win-crt-stdio) -> real bswap builtins + the errno_t
   freopen_s contract (*presult, 0/errno).
2. SSPI gate: global_init -> Curl_sspi_global_init -> InitSecurityInterfaceA()
   == NULL -> CURLE_FAILED_INIT ("error initializing curl library"). Landed a
   real SecurityFunctionTableA (28 slots, wine include/sspi.h layout verified,
   dwVersion=1 = SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION): real
   EnumerateSecurityPackagesA/QuerySecurityPackageInfoA/AcquireCredentialsHandle
   (NTLM typed model) + DeleteSecurityContext/FreeContextBuffer; handshake/sign
   slots typed SEC_E_UNSUPPORTED_FUNCTION (0x80090302).
3. BUG-035 twin (bsearch): curl.exe bsearch()es a 284-entry 16-byte table with
   an ms_abi comparator; the raw glibc bsearch called it SysV-style ->
   comparator read Windows-register garbage (crash 0x14000cbf8
   mov (%rcx),%rcx RCX=0x61). Fixed with the proven qsort_r bridge pattern
   (mw_ms_bsearch_bridge + ms_abi-typed function-pointer deref).
   RESULT: `curl --version` exit 0, FULL banner (curl 8.23.0 libcurl/8.23.0
   LibreSSL/4.3.3 zlib-ng brotli zstd nghttp2 HTTP3 ...), x3 deterministic.
4. Open tail (registered): real HTTP fetch — getaddrinfo delegates fine but
   curl's thrdpool resolver job never reaches getaddrinfo (job starvation
   after WakeConditionVariable; WSAStartup WAS called; socket plane is real).
   Needs the thrdpool/condvar interleaving decoded (SW-791 tail).

## Train 2 — SW-1234 engine: win32 drive-path mapping (3 surfaces)

Root cause chain (new Sysinternals corpus, shared (null) v(null) banner):
win32_module_path() hands out "C:\..." but w3_normalize_path mapped "X:\a"
to host-root "/a" — unopenable. Fixed THREE surfaces to map the drive prefix
to the loader PROCESS CWD:
  - w3_normalize_path itself (CreateFile/stat/open plane)
  - mw_fopen + mw__wfopen (CRT stdio plane — the Sysinternals self-version
    read goes through fopen)
  - w_ver_read_from_file (GetFileVersionInfoSizeW's own fopen)
Zero regressions: corpus census 15x exit-0 + pageant 124 unchanged.

## Train 3 — SW-1235 version-resource plane (banner class solved)

whois64 decoded end-to-end: GetFileVersionInfoSizeW(self) now succeeds (804
bytes), but the banner stayed (null) then "W v1 - D" then CompanyName-everywhere:
  (a) mw_VerQueryValueA returned pointers INTO the UTF-16 block -> %s read
      one char; A-variant now transcodes the matched wide value to ANSI.
  (b) Binary pass-through classification was position-fragile
      (VarFileInfo\Translation went through the string transcode) -> lang
      table garbage -> (null) again; fixed with last-component detection.
  (c) ALL five A-queries shared ONE static buffer -> every saved pointer
      aliased the last value (all-banner-CompanyName); fixed with a 16-slot
      ring.
  RESULT: whois64 prints the REAL banner "Whois v1.21 - Domain information
  lookup / Copyright (C) 2005-2019 Mark Russinovich / Sysinternals -
  www.sysinternals.com". Shared Sysinternals gate (8 more tools queued).

## Train 4 — SW-1236 gate batch (5 imports, 3 programs)

GetComputerNameExA/W (session identity + honest ERROR_INVALID_PARAMETER
levels), LogonUserA/W (typed 1326 ERROR_LOGON_FAILURE), FindFirstFileExA
(delegates to the proven W walker), FillConsoleOutputCharacterA (vacuous
write), WNetAddConnection2A/W (225) + WNetCancelConnection2A/W (2250
ERROR_NOT_CONNECTED). whois64 unresolved 1->0; pslist64 5->0.

## Train 5 — SW-1237 real DNS (A-plane gethostbyname)

mw_gethostbyname was a typed WSAHOST_NOT_FOUND stub; now delegates to the
host resolver via getaddrinfo(AF_INET) and builds a real winsock hostent
(h_name/h_aliases/h_addr_list, static stable storage). whois64 resolved
COM.whois-servers.net (2 addrs), whois.iana.org (1), EXAMPLE.COM (2) — REAL
host DNS answers. Tail: whois64's socket connect never issues after DNS
(no socket()/connect() traced) — registered as the SW-1237 tail.

## Trains 6-15 — new corpus (16 staged guests, 4 new full lifecycles)

Downloaded/staged 16 x64 guests (artifacts/sweep047/bin/, shas pinned):
  - NEW exit-0 FULL LIFECYCLE x3: listdlls64 (ListDLLs), pipelist64
    (PipeList), psloggedon64 (PsLoggedOn) — zero staging changes needed.
  - psinfo64: prints the REAL SYSINTERNALS SOFTWARE LICENSE TERMS EULA
    (multi-KB, two-language) — the EULA display path works.
  - whois64: real banner + real usage + real DNS (above), socket tail open.
  - du64/strings64/sdelete64/psservice64: real usage text (UTF-16 for
    sdelete/strings banner plane) exit 255 (usage path) — SW-1235 banner
    class benefits them once the remaining key mapping lands.
  - accesschk64 (16 unresolved: NetShareEnum/ACL/LSA families), pslist64
    (PDH "Processor performance object not found" — the procexp64
    perf-counter class), logonsessions64/autologon64 (Winlogon registry
    plane) — registered as SW-1238..1241 frontiers.
  - handle64: 139 crash — registered SW-1242 (undecoded this round).
  - curl staged with libcurl-x64.dll + trurl.exe (trurl exit-7 real error
    path reproduces).

## Measurement (final binary 3dbc56d53bcf90882b8c7789ed532dec34845b78413ae52e9e53b1af510a591d)

- census x3 31 PROGRAMS DETERMINISTIC (artifacts/sweep047/census.tsv,
  scripts/sweep047_census.sh): corpus 15x exit-0 + pageant 124 park (ZERO
  unresolved, ZERO regressions vs SWEEP-046); new-guest tally: 3x exit-0
  (listdlls64/pipelist64/psloggedon64), whois64/pslist64/logonsessions64/
  accesschk64/autologon64 exit-1 gates+usage, 5x 255 usage/EULA paths,
  handle64 139.
- G1 PIN 12/12 (evidence/master/g1/20261022_050538)
- round trips: jq '.a[1]'->2, sqlite3 6*7->42, zstd+lz4 banners, 7za
  create+extract BYTE-EXACT (evidence/sweep047/roundtrips.txt)
- games: snake 30f byte-identical 3x3, breakout 381f/13bricks, calc 15/15
- ssh -V: unchanged vs HEAD falsification (same "failed to initialize
  w32posix wrapper" message + exit 0 x3 — NOT a SWEEP-047 regression;
  fd.exe 9.0.0 --version exit 0 intact)
- no force-push; fast-forward base, single-round increment
