# PROGRAM LADDER — 100 real programs, smallest/easiest → largest/hardest

**Purpose (user directive 2026-10-17):** the goal is OLD software, small-to-medium first.
We climb the ladder one rung at a time; every failure becomes a BUG-* with evidence.
This file is the canonical queue. Each entry is also a backlog task (Wave-12:
`LADDER-<n>`), so the ladder doubles as a 100-task execution plan.

**Rules per entry (acceptance discipline, unchanged):**
1. Legal acquisition only — official site / MS servers / open-source release. Record SHA-256.
2. Census first with pefile+capstone: arch, toolchain fingerprint, CRT imports, full import surface diff vs API_COMPATIBILITY_DATABASE.md.
3. Run 3×: command, exit code, stdout capture, api_trace.json events, crash signature.
4. Status vocabulary: `PLANNED → CENSUSED → RUNS-PARTIAL → RUNS-3x → VERIFIED` (irreversible, like bug states).
5. Every new import that blocks a rung = one BUG-* + one API-* backlog item (measure-driven, as before).

**Legend:** CRT column = dominant runtime expectation. S=console subsystem, G=GUI.
Statuses as of 2026-10-17 (session CONTINUE #10): only T0 entries have evidence so far.

---

## T0 — Proven baseline (already conquered; keep pinned in CI)
| # | ID | Program | Obtain | Era | CRT | I/O | Difficulty | Status |
|---|----|---------|--------|-----|-----|-----|-----------|--------|
| 1 | LADDER-001 | UPX 4.x `upx.exe` | github.com/upx/upx | modern | mingw/GCC SEH | S | ★ | VERIFIED — G1 banner exit 0, C++ EH chain proven |
| 2 | LADDER-002 | 7-Zip 26.04 `7za.exe` | 7-zip.org extra pack | 2026 | MSVC C++ | S | ★★ | VERIFIED — a/x/t/l/i + round trip 3×, SHA-256 byte-perfect |
| 3 | LADDER-003 | jq 1.7.1 | github.com/jqlang/jq | modern | mingw | S | ★★ | RUNS-3x — CRT-init frontier (malloc crash) |
| 4 | LADDER-004 | sqlite3 3.50.4 | sqlite.org | modern | MSVC/mingw | S | ★★ | RUNS-3x — file-DB round trip |
| 5 | LADDER-005 | curl 8.x | curl.se/windows | modern | MSVC UCRT | S | ★★★ | RUNS-PARTIAL — UCRT frontier OPENSRC-CURL-1, IAT page-cross BUG-059, InitOnce BUG-060 |
| 6 | LADDER-006 | 7z.exe (full, w/ 7z.dll) | 7-zip.org | 2026 | MSVC C++ | S | ★★★ | RUNS-PARTIAL — LoadLibraryW frontier OPENSRC-7ZIP-3 |
| 7 | LADDER-007 | busybox64.exe | frippery busybox win64 | old | mingw | S | ★★ | PLANNED — multi-call argv[0] dispatch test |
| 8 | LADDER-008 | zstd.exe | github.com/facebook/zstd | modern | MSVC UCRT | S | ★ | VERIFIED — v+round trip 3x SHA byte-perfect after BUG-062 threads |
| 9 | LADDER-009 | lz4.exe | github.com/lz4/lz4 | modern | MSVC UCRT | S | ★ | PLANNED |
| 10 | LADDER-010 | gzip/gunzip (mingw) | gnuwin / mingw builds | old | msvcrt.dll | S | ★ | PLANNED |

## T1 — Trivial single-exe console tools (CRT-light, fs-only)
| # | ID | Program | Obtain | Era | CRT | Notes | Difficulty | Status |
|---|----|---------|--------|-----|-----|-------|-----------|--------|
| 11 | LADDER-011 | wget64.exe | brechtsanders/winlibs wget64 | modern | static mingw | winsock-lite HTTPS | ★ | PLANNED |
| 12 | LADDER-012 | xxd.exe (vim rt) | vim.org / tuxproject builds | old | msvcrt.dll | hex round trip | ★ | PLANNED |
| 13 | LADDER-013 | md5deep 4.4 | md5deep.sf.net | 2014 | msvcr100 | hashing, recursive walk | ★★ | PLANNED |
| 14 | LADDER-014 | hashdeep 4.4 | md5deep.sf.net | 2014 | msvcr100 | audit files | ★★ | PLANNED |
| 15 | LADDER-015 | Tree 1.7 | old free utility | 2000s | msvcrt.dll | fs walk | ★ | PLANNED |
| 16 | LADDER-016 | GNU grep 2.5.4 (GnuWin32) | gnuwin32.sf.net | 2003 | msvcrt.dll | regex engine | ★★ | PLANNED |
| 17 | LADDER-017 | GNU sed 4.2.1 (GnuWin32) | gnuwin32.sf.net | 2009 | msvcrt.dll | -i temp-file dance | ★★ | PLANNED |
| 18 | LADDER-018 | GNU gawk 3.1.6 (GnuWin32) | gnuwin32.sf.net | 2007 | msvcrt.dll | scripting+fs | ★★★ | PLANNED |
| 19 | LADDER-019 | GNU diffutils 2.8.7 (GnuWin32) | gnuwin32.sf.net | 2005 | msvcrt.dll | | ★★ | PLANNED |
| 20 | LADDER-020 | Info-ZIP unzip 6.0 | info-zip.org | 2009 | msvcrt.dll | archive extract | ★★ | PLANNED |

## T2 — Console + winsock / deeper fs
| # | ID | Program | Obtain | Era | CRT | Notes | Difficulty | Status |
|---|----|---------|--------|-----|-----|-------|-----------|--------|
| 21 | LADDER-021 | Info-ZIP zip 3.0 | info-zip.org | 2008 | msvcrt.dll | archive create | ★★ | PLANNED |
| 22 | LADDER-022 | plink.exe (PuTTY) | putty.org | 1999→ | VC6-era/mingw | raw winsock SSH | ★★★ | PLANNED |
| 23 | LADDER-023 | pscp.exe (PuTTY) | putty.org | 1999→ | same | file transfer | ★★★ | PLANNED |
| 24 | LADDER-024 | psftp.exe (PuTTY) | putty.org | 1999→ | same | interactive console | ★★★ | PLANNED |
| 25 | LADDER-025 | netcat 1.11 NT | vulnners/varion builds | 2004 | msvcrt.dll | winsock + `-L` listen loop | ★★★ | PLANNED |
| 26 | LADDER-026 | tcping | eli fulkerson | old | msvcrt.dll | winsock connect timing | ★★ | PLANNED |
| 27 | LADDER-027 | iperf3 3.x | iperf.fr | modern | msvcr140/ucrt | TCP throughput loopback | ★★★ | PLANNED |
| 28 | LADDER-028 | Sysinternals du.exe | learn.ms/sysinternals | old | msvcrt.dll | NT fs accounting | ★★ | PLANNED |
| 29 | LADDER-029 | Sysinternals handle.exe | learn.ms/sysinternals | old | msvcrt.dll | NT handle query (NtQuerySystemInformation) | ★★★ | PLANNED |
| 30 | LADDER-030 | Sysinternals strings.exe | learn.ms/sysinternals | old | msvcrt.dll | wide+ascii scan | ★★ | PLANNED |

## T3 — NT-API classics (Sysinternals wave; exposes our NT-layer gaps)
| # | ID | Program | Obtain | Era | CRT | Notes | Difficulty | Status |
|---|----|---------|--------|-----|-----|-------|-----------|--------|
| 31 | LADDER-031 | PsList | sysinternals | old | msvcrt.dll | perf counters | ★★★ | PLANNED |
| 32 | LADDER-032 | PsKill | sysinternals | old | msvcrt.dll | OpenProcess/Terminate | ★★★ | PLANNED |
| 33 | LADDER-033 | PsInfo | sysinternals | old | msvcrt.dll | registry read | ★★★ | PLANNED |
| 34 | LADDER-034 | LogonSessions | sysinternals | old | msvcrt.dll | LSA-ish reads | ★★★ | PLANNED |
| 35 | LADDER-035 | AccessChk | sysinternals | old | msvcrt.dll | security descriptors | ★★★★ | PLANNED |
| 36 | LADDER-036 | sigcheck (console) | sysinternals | old | msvcrt.dll | cert APIs (stretch) | ★★★★ | PLANNED |
| 37 | LADDER-037 | autorunsc (console) | sysinternals | old | msvcrt.dll | registry sweep | ★★★★ | PLANNED |
| 38 | LADDER-038 | tcpvcon | sysinternals | old | msvcrt.dll | TCP table | ★★★ | PLANNED |
| 39 | LADDER-039 | psloglist | sysinternals | old | msvcrt.dll | event log read | ★★★★ | PLANNED |
| 40 | LADDER-040 | PsExec | sysinternals | old | msvcrt.dll | service create (stretch) | ★★★★★ | PLANNED |

## T4 — Heavy console codecs / parsers
| # | ID | Program | Obtain | Era | CRT | Notes | Difficulty | Status |
|---|----|---------|--------|-----|-----|-------|-----------|--------|
| 41 | LADDER-041 | ffmpeg.exe (essentials) | gyan.dev / BtbN | modern | ucrt+vcruntime140 | demux/mux to png | ★★★★ | PLANNED (prior user requirement) |
| 42 | LADDER-042 | ffprobe.exe | gyan.dev | modern | ucrt | JSON of a wav | ★★★ | PLANNED |
| 43 | LADDER-043 | magick.exe (portable) | imagemagick.org | modern | ucrt+msvcp140 | C++ std lib surface | ★★★★ | PLANNED (prior user requirement) |
| 44 | LADDER-044 | cjpeg/djpeg (libjpeg-turbo) | github libjpeg-turbo | modern | mingw | jpeg round trip | ★★★ | PLANNED |
| 45 | LADDER-045 | pngcheck 3.0.3 | pngcheck.sf.net | old | msvcrt.dll | png validate | ★★ | PLANNED |
| 46 | LADDER-046 | xz.exe 5.x | tukaani.org/xz.html | old | msvcr100 | lzma round trip | ★★★ | PLANNED |
| 47 | LADDER-047 | brotli.exe | github google/brotli | modern | mingw/ucrt | round trip | ★★ | PLANNED |
| 48 | LADDER-048 | openssl.exe 3.x | slprowej / official light | modern | ucrt | self-signed cert gen | ★★★★ | PLANNED |
| 49 | LADDER-049 | xmlstarlet 1.6.1 | sourceforge | 2014 | msvcr90 | VS2008 surface | ★★★ | PLANNED |
| 50 | LADDER-050 | iconv.exe (GnuWin32 libiconv 1.9.2) | gnuwin32 | 2004 | msvcrt.dll | CP conversions | ★★ | PLANNED |

## T5 — First GUI wave (user32/gdi32 basics, message loop, dialogs)
| # | ID | Program | Obtain | Era | CRT | Notes | Difficulty | Status |
|---|----|---------|--------|-----|-----|-------|-----------|--------|
| 51 | LADDER-051 | 7zFM.exe (7-Zip File Manager) | 7-zip.org | 2026 | MSVC C++ | common controls, menus | ★★★ | PLANNED (we own the DLL surface already) |
| 52 | LADDER-052 | NirCmd | nirsoft.net | old | msvcrt.dll | headless utility, minimal GDI | ★★★ | PLANNED |
| 53 | LADDER-053 | ProduKey | nirsoft.net | old | msvcrt.dll | listview + registry | ★★★ | PLANNED |
| 54 | LADDER-054 | CurrPorts | nirsoft.net | old | msvcrt.dll | TCP table + GUI | ★★★★ | PLANNED |
| 55 | LADDER-055 | TCPView (old 2.x) | sysinternals archive | old | msvcrt.dll | small GUI classic | ★★★ | PLANNED |
| 56 | LADDER-056 | metapad 3.51 | liquidninja archive | 2000s | msvcrt.dll | notepad clone | ★★★ | PLANNED |
| 57 | LADDER-057 | Edxor 1.x | old freeware | 2000s | msvcrt.dll | tiny editor | ★★★ | PLANNED |
| 58 | LADDER-058 | Regshot 1.8.2 | sourceforge | 2006 | msvcrt.dll | registry+fs diff | ★★★★ | PLANNED |
| 59 | LADDER-059 | IrfanView 3.97 | irfanview archive | 2005 | Delphi-ish? verify | image viewer classic | ★★★★ | PLANNED |
| 60 | LADDER-060 | nconvert (XnView console) | xnview.com | old | msvcrt-era | bridge GUI→codec | ★★★ | PLANNED |

## T6 — GUI wave 2 (common controls v6, GDI+, accelerators, Scintilla-class)
| # | ID | Program | Obtain | Era | CRT | Notes | Difficulty | Status |
|---|----|---------|--------|-----|-----|-------|-----------|--------|
| 61 | LADDER-061 | Process Explorer 16.x (old) | sysinternals archive | 2006 | msvcrt.dll | iconic classic | ★★★★ | PLANNED |
| 62 | LADDER-062 | AutoHotkey 1.0.48 | autohotkey archive | 2008 | msvcrt.dll | scripting VM — huge value | ★★★★★ | PLANNED |
| 63 | LADDER-063 | SciTE 2.x | scintilla.org | old | msvcrt.dll | Scintilla control | ★★★★ | PLANNED |
| 64 | LADDER-064 | Notepad2 2.x | old releases | 2005 | msvcrt.dll | Scintilla-lite | ★★★★ | PLANNED |
| 65 | LADDER-065 | Resource Hacker 3.x | angusj.com | old | msvcrt.dll | PE resource parsing — meta value | ★★★★ | PLANNED |
| 66 | LADDER-066 | PeaZip 5.x | peazip.github.io | old | FreePascal RTL | non-MSVC runtime! | ★★★★ | PLANNED |
| 67 | LADDER-067 | XnView 1.70 classic | xnview archive | 2000s | msvc-era | viewer classic | ★★★★ | PLANNED |
| 68 | LADDER-068 | FastStone Capture 5.x | faststone archive | 2005 | msvc-era | GDI capture | ★★★★ | PLANNED |
| 69 | LADDER-069 | IcoFX 1.6 | old freeware | 2005 | msvc-era | icon editor | ★★★★ | PLANNED |
| 70 | LADDER-070 | Picasa 2.x (no-login mode) | archive builds | 2005 | custom | photo viewer classic | ★★★★★ | PLANNED |

## T7 — Framework wave 1 (MFC / Delphi / wx / Qt4)
| # | ID | Program | Obtain | Era | CRT | Notes | Difficulty | Status |
|---|----|---------|--------|-----|-----|-------|-----------|--------|
| 71 | LADDER-071 | HxD 1.7.7.0 | mh-nexus archive | 2008 | Delphi RTL | hex editor classic | ★★★★★ | PLANNED |
| 72 | LADDER-072 | WinDirStat 1.1.2 | windirstat.net | 2006 | MFC 8 (msvcr80 SxS!) | MFC + listview owner-draw | ★★★★★ | PLANNED |
| 73 | LADDER-073 | WinMerge 2.8 | winmerge.org archive | 2008 | msvcr90 | diff GUI | ★★★★ | PLANNED |
| 74 | LADDER-074 | Audacity 1.2.6 | old release | 2004 | msvcrt/wxWidgets | audio editor classic | ★★★★★ | PLANNED |
| 75 | LADDER-075 | foobar2000 0.9.6 | old release | 2009 | msvcr90/ATL-lite | audio player | ★★★★★ | PLANNED |
| 76 | LADDER-076 | SumatraPDF 1.9 | old release | 2012 | msvcr100 | mupdf rendering → PNG evidence | ★★★★ | PLANNED |
| 77 | LADDER-077 | Notepad++ 5.x | old release | 2010 | msvcr90 + Scintilla | iconic | ★★★★★ | PLANNED |
| 78 | LADDER-078 | MPC-HC 1.3 | old release | 2011 | msvcr100 + DirectShow | media classic | ★★★★★ | PLANNED |
| 79 | LADDER-079 | Everything 1.3 | voidtools archive | 2012 | msvc-era | NTFS MFT index (API-heavy) | ★★★★★ | PLANNED |
| 80 | LADDER-080 | qBittorrent 2.x | old release | 2010 | Qt4 (mingw/msvc) | Qt runtime wave | ★★★★★ | PLANNED |

## T8 — Suites / heavy classics
| # | ID | Program | Obtain | Era | CRT | Notes | Difficulty | Status |
|---|----|---------|--------|-----|-----|-------|-----------|--------|
| 81 | LADDER-081 | Total Commander 7.x | ghisler.com (shareware, full-featured trial) | 2000s | msvc-era, plugin arch | file manager king | ★★★★★ | PLANNED |
| 82 | LADDER-082 | WinRAR 4.x | rarlab (trial) | 2011 | msvc-era | archive suite | ★★★★ | PLANNED |
| 83 | LADDER-083 | ACDSee 3.1 | archive builds | 1999 | msvc-era | legendary old viewer | ★★★★★ | PLANNED |
| 84 | LADDER-084 | Winamp 2.95 | archive builds | 2003 | msvc-era | audio classic, COM/directx | ★★★★★ | PLANNED |
| 85 | LADDER-085 | IrfanView 4.x latest | irfanview.com | modern | msvc-era + plugins | plugin loader = LoadLibrary wave | ★★★★★ | PLANNED |
| 86 | LADDER-086 | Everything 1.4 latest | voidtools.com | modern | msvc-era | service+NTFS | ★★★★★ | PLANNED |
| 87 | LADDER-087 | Notepad++ latest | notepad-plus-plus.org | modern | msvcr140+ | modern build of the classic | ★★★★★ | PLANNED |
| 88 | LADDER-088 | Process Monitor 3.x | sysinternals | modern | msvcr120? | needs driver — partial-run target | ★★★★★ | PLANNED |
| 89 | LADDER-089 | VLC 1.1 | old release | 2010 | msvcr90 + plugins | media suite | ★★★★★ | PLANNED |
| 90 | LADDER-090 | GIMP 2.6 (portable) | old release | 2009 | GTK+ win32 port | GTK runtime wave | ★★★★★ | PLANNED |

## T9 — Frontier class (explicitly out of reach today; document WHY each time)
| # | ID | Program | Obtain | Era | Blocker class | Status |
|---|----|---------|--------|-----|--------------|--------|
| 91 | LADDER-091 | Paint.NET 2.x (.NET) | archive builds | 2004 | needs CLR — decision pending (ADR-014: out of scope for now) | BLOCKED-BY-DESIGN |
| 92 | LADDER-092 | .NET Core self-contained console | build sample | modern | CoreCLR embedding study first | BLOCKED-BY-DESIGN |
| 93 | LADDER-093 | DirectX 9 game demo (old SDK sample exe) | legacy SDK | 2004 | D3D9 + GPU plan (DXVK-inspired study) | BLOCKED-BY-DESIGN |
| 94 | LADDER-094 | Win16 app (e.g., old notepad 3.1) | archive | 1992 | 16-bit: needs WineVDM-style VMM (otvdm inspiration; separate sub-project) | BLOCKED-BY-DESIGN |
| 95 | LADDER-095 | MSVC 6 MFC MDI app | rebuild sample | 1998 | MFC42.dll surface | BLOCKED-FRONTIER |
| 96 | LADDER-096 | UWP/MSIX packaged app | store | modern | out of scope by architecture | BLOCKED-BY-DESIGN |
| 97 | LADDER-097 | Office viewer 2003 | archive | 2003 | OLE2 compound suite | BLOCKED-FRONTIER |
| 98 | LADDER-098 | Installer (NSIS-built setup.exe) | nsis samples | modern | installer logic + COM | BLOCKED-FRONTIER |
| 99 | LADDER-099 | Unity 5 player demo | archive | 2015 | engine runtime | BLOCKED-BY-DESIGN |
| 100 | LADDER-100 | Visual Studio 6 IDE | archive | 1998 | the ultimate old-software stress test | BLOCKED-FRONTIER |

---

## CRT coverage matrix (drives MSVC_RUNTIME_PLAN.md priorities)
| CRT | Ladder rungs demanding it | Priority |
|---|---|---|
| msvcrt.dll (NT-native 6.x) | T1-T3 bulk, T5-T6 old tools | P0 — most old software |
| msvcr71 / msvcr80 (SxS) | scattered old utils, WinDirStat | P1 |
| msvcr90 | WinMerge/xmlstarlet/VLC-era | P1 |
| msvcr100 | md5deep/xz/MPC/Sumatra | P1 |
| ucrtbase + vcruntime140 + msvcp140 | ffmpeg/magick/openssl/curl | P0 — modern OSS builds |
| Delphi RTL / FPC | HxD/PeaZip/IrfanView-class | P2 (reuse kernels; no MS CRT) |
| MFC42/MFC80+ | T7-T9 | P2 |
| Qt4/Qt5, GTK2 | qBittorrent/GIMP | P3 |
