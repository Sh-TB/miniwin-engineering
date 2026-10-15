# NEXT ACTION (single, most-restrictive next step)

**Verified state (2026-10-15, HEAD f75aa97):** regression 12/12; t08 255/255 x3;
probe 3/3 x3; UPX G1 exit 0; sweep 11/12 (t09 TIMEOUT = TASK-API-009 frontier).

**TASK-OPENSRC-1 (P1):** two real famous open-source targets deep into CRT init.
- ffmpeg.exe (BtbN win64 GPL): ~425 imports unresolved; executes CRT init, WSAStartup,
  vulkan probe, per-library constructors. BLOCKED root: heap metadata corruption
  ("malloc(): invalid size (unsorted)") during late CRT init, before main.
  Next command: `timeout 150 ./minwin_loader artifacts/opensource/ffmpeg/ffmpeg-master-latest-win64-gpl/bin/ffmpeg.exe -hide_banner -loglevel error -f lavfi -i "testsrc2=size=64x48:rate=1" -frames:v 1 -y /tmp/out.png`
  Next evidence: trace tail + the MINWIN_HEAP_GUARD tooling already in the tree.
- magick.exe (ImageMagick 7.1.2-34 portable): BLOCKED root: UCRT locale ctype-init
  delivers a corrupted frame to GetStringTypeW (R9="ab.." string data as out-ptr).
  Next: decode the caller at RVA 0x3d87b2 frame and the locale struct layout.

**Acceptance for unblocking:** ffmpeg reaches main() and produces /tmp/out.png
(valid PNG), magick produces rose.png; regression 12/12 + sweep 11/12 stay green.
