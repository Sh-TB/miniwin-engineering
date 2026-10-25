# SW-1298 — spider `.rsrc` audit: can the engine surface all card-back art variants?

Agent 2-b, recon only (no source edits). Tools: python3.13 + pefile 2024.8.26.
Guest: `artifacts/sweep061/guests/spider/spider.exe` (+ `cards.dll`, same dir).
Engine: `src/loader/loader.c` (single 40,816-line file; ALL resource code lives here).

Spider identity (pefile): **PE32+ x64** (Machine=0x8664, Magic=0x20b, Subsystem=2 GUI),
version `5.2.3790.1830 (srv03_sp1_rtm)` — the Server 2003 SP1 64-bit build of Spider,
NOT an XP-era 32-bit exe. This matters for Task 2: the PE32+ native resource path is
what serves this guest.

---

## TASK 1 — .rsrc tree (pefile, recursive type -> name/id -> language)

### Headline: the worklist figure "87,992 resource entries" is WRONG for this binary

| quantity (measured) | spider.exe | cards.dll |
|---|---|---|
| **leaf entries (real)** | **90** | 75 |
| total nodes (dirs + leaves) | 190 | ~157 |
| .rsrc data-dir size (bytes) | 470,392 (0x72D78) | 353,872 |
| .rsrc section raw size | 470,528 | 354,304 |
| sum of leaf data sizes | 464,927 | 349,978 |

87,992 matches **no** structural quantity of either file (not leaf count, not node
count, not section/dir byte size, not any per-type byte subtotal — see breakdown
below). It is almost certainly either (a) a byte size misread as an entry count from
a *different* build (e.g. the 32-bit XP Spider), or (b) a raw-walker artifact that
treated `IMAGE_RESOURCE_DATA_ENTRY`s as subdirectories and counted garbage. The
pefile walk of the real tree is unambiguous: **90 leaves**.

### Per-type leaf counts (corrected RT names), spider.exe — every leaf at lang 0x409

| Type dir | Name | Leaves | Bytes |
|---|---|---|---|
| ID:2 | RT_BITMAP | **56** | 199,472 |
| ID:3 | RT_ICON | 10 | 25,360 |
| NAME:"WAVE" | custom named type | 6 | 232,840 |
| ID:5 | RT_DIALOG | 5 (IDs 107,117,118,119,130) | 2,518 |
| ID:14 | RT_GROUP_ICON | 5 (IDs 103,120,121,122,123) | 170 |
| ID:6 | RT_STRING | 4 (IDs 1–4) | 2,308 |
| ID:4 | RT_MENU | 1 (ID 101) | 586 |
| ID:9 | RT_ACCELERATOR | 1 (ID 102) | 96 |
| ID:16 | RT_VERSION | 1 (ID 1) | 872 |
| ID:24 | RT_MANIFEST | 1 (ID 1) | 705 |
| | **total** | **90** | 464,927 |

Tree shape: depth exactly 3 everywhere (type → name/id → language); all 90 leaves
language ID 1033; single language — no MUI complications.

### Full RT_BITMAP leaf-ID list (56 leaves)

Named (UTF-16) resources, 54: `CARD1 … CARD52` (i.e. CARD1, CARD2, … CARD52 — every
decimal name 1–52), `CARDBACK`, `FELT`.
Int-ID resources, 2: `106` (0x6A), `108` (0x6C).
No ID > 0xFFFF anywhere in the tree; all names are ASCII uppercase (survive the
engine's case-insensitive compare).

### Bitmap geometry (from BITMAPINFOHEADER / BITMAPCOREHEADER in the data)

| resource | hdr | dim | bpp | bytes |
|---|---|---|---|---|
| ID 106 (0x6A) | INFOHEADER | 359x242 | 8 | 88,186 (splash/about art) |
| **CARDBACK** | INFOHEADER | **71x96** | **8** | 7,978 |
| **ID 108 (0x6C)** | INFOHEADER | **71x96** | **8** | 7,978 (same size/dims as CARDBACK — duplicate back art at RVA 0x55d98 vs 0x3d1b8) |
| FELT | INFOHEADER | 64x64 | 8 | 5,162 |
| CARD1, CARD10, CARD14–CARD23, CARD27–CARD36 (22) | INFOHEADER | 71x96 | 1 | 1,200 each |
| CARD2–CARD9, CARD40–CARD49 (18) | COREHEADER | 71x96 | 1 | 1,184 each |
| CARD11 (1) | INFOHEADER | 71x96 | 4 | 3,560 |
| CARD12, CARD13, CARD24–CARD26, CARD37–CARD39, CARD50–CARD52 (11) | COREHEADER | 71x96 | 4 | 3,536 each |

All INFOHEADER samples verified `biCompression=0` (BI_RGB), `biSizeImage=0` — the
engine's BI_RGB-only gate passes every one.

### Card-back candidates

* In **spider.exe**: back-sized (71x96) 8bpp art = named `CARDBACK` + int `108`
  (identical geometry/size → same art duplicated). Note this build ships **one**
  named back, not 12; the "12 selectable backs" of the XP game map to **cards.dll**:
  RT_BITMAP IDs **54,55,56,57,58,59,60,61,62,63,64,65** — a contiguous run of 12,
  all 71x96 24bpp, 20,778 bytes each (classic cards.dll back block).
* spider.exe does **not** import cards.dll (imports: ADVAPI32/KERNEL32/GDI32/
  USER32/SHELL32/WINMM/COMCTL32; the only resource-ish import is
  `USER32.dll!LoadBitmapW`), so its art comes entirely from its own named bitmaps.

### ID anomalies

* No resource ID > 0xFFFF anywhere (0 hits).
* No duplicate IDs within any directory (cross-level overlaps like ID 1/2/3 at
  type- vs name-level are normal; lang 1033 repeats once per leaf — expected).
* Gaps in the sorted distinct-ID set only at natural block boundaries
  (24 → 101, 130 → 1033); bitmap name-level IDs have no gaps (54 named + 106, 108).
* Icons: 10 RT_ICON (IDs 1–10) ↔ 5 RT_GROUP_ICON (103,120–123) — consistent.

---

## TASK 2 — engine resource loader (src/loader/loader.c)

There is exactly **one** `.rsrc` walker; both the native (x64) guests and the
SW-1281 PE32/x86 interpreter route through it (x86 IAT thunks bind the same
name-only dispatch table — comment block at loader.c:37900–37910).

### (a) How .rsrc is parsed — live walk, no prebuilt table

`w3_res_find()` — loader.c:13128–13208 — walks the guest PE header at runtime:
* base selection: `mw_res_pe_base()` loader.c:13049–13058 (accepts hmod only if
  page-aligned + MZ + PE sig, else falls back to main exe; SW-1277 per-module fix),
* data-dir read: loader.c:13141–13143 `num_dd = *(u16*)(pe+pe_off+24+108)`,
  `rsrc_rva = *(u32*)(pe+pe_off+24+112+2*8)`, gate `if (num_dd < 3) return NULL;`
* fixed 3-level loop loader.c:13149–13206; counts read from
  `+12/+14` (NamedEntries/IdEntries — SW-801c fix); entries scanned
  `for (i = 0; i < count; i++)` over `entries + i*8` — **no cap on entry count**;
* consumers: `mw_FindResourceA` 13209, `mw_FindResourceW` 13215,
  `mw_FindResourceExW` 13220 (lang arg **ignored**, any-language accepted —
  matches depth-2 `match = 1` at 13171), `mw_LoadResource` 13226 (returns
  data pointer, SW-1259), `mw_SizeofResource` 13252, `mw_LockResource` 13242
  (identity no-op).

### (b) Hardcoded caps / filters found (with lines)

| line | cap / filter | impact |
|---|---|---|
| 13142 | `num_dd < 3 → NULL` | benign (all real exes have ≥3 data dirs) |
| 13149 | fixed `depth < 3` | correct — rsrc tree is 3 levels |
| **13191** | `i < f_named + f_ided && i < 64` | **TRACE-ONLY** diag dump cap; the real lookup loop (13162) is uncapped — no functional effect |
| 22034–22045 | `MW_IMAGE_BASE`, `g_img_w/h/live[8]` | 8-slot pool for *pseudo* image handles (icons/cursors, non-DIB loads); NOT used by the resource-bitmap path |
| **22055–22065** | `mw_bitmap_slot` over `MW_MEMDC_MAX` = **256** (line 18050) | HBITMAP slot pool shared by all DIB loads; a *lifecycle* cap (leaked slots exhaust), not a per-resource or per-ID cap |
| 22087 | `hdr_size < 12 → reject` | benign |
| 22095–22106 | BITMAPCOREHEADER (12-byte) support — SW-1275 fix | required by spider (29 of 56 bitmaps are COREHEADER) — already landed |
| 22107 | `comp != 0 (BI_RGB only) → reject` | all spider bitmaps are BI_RGB — pass |
| 22109 | `w > 4096 || h > 4096 → reject` | spider max 359x242 — pass |
| 22110 | bpp whitelist {1,4,8,24,32} | spider uses 1/4/8 — pass |
| 22140–22143 | `LR_LOADFROMFILE → NULL` (honest) | spider loads from resources only |
| **13257–13260, 29082–29089** | `mw_EnumResourceNamesW/A`, `mw_EnumResourceTypesW` = stubs returning 0 | enumeration unsupported (honest). spider.exe imports **no** Enum* API — no impact; would matter for a guest that *enumerates* backs |
| 22068–22073 | IS_INTRESOURCE boundary `< 0x10000` | correct Win32 semantics |
| 1230/1296 | LoadLibrary requires PE32+ (0x20b) | x86 DLLs rejected — irrelevant to spider |

Type handling: **no type whitelist** — depth-0 compares whatever type the guest
passes, including named custom types like `"WAVE"` (string-type compare path,
13059–13095). RT_BITMAP is reached via LoadBitmapW→LoadImageW (28683–28684) and
the explicit `2 /*RT_BITMAP*/` at 22069–22073.

### (c) ID width

Directory entry IDs are read as **full uint32** (`nm = *(u32*)(entries + i*8)`,
13163) with no truncation; only bit 0x80000000 is special (name offset, 13064).
Guest-side lookup values are ints bounded to 0xFFFF via IS_INTRESOURCE
(13062–13063) — identical to real Windows (MAKEINTRESOURCE cannot express >0xFFFF).
Decimal-string aliases are honored: A-path `#123` (13117–13124), W-path plain
decimal (13101–13112, saturates at 0x7FFFFFFF). Spider needs none of these
(max ID 108, all names literal strings).

### (d) Named (string) resources on the x86 path

The x86 (cpu32) path has **no separate resource implementation**: PE32 guests bind
`FindResourceA/W`, `FindResourceExW`, `LoadResource`, `LockResource`,
`SizeofResource`, `EnumResourceNamesA/W`, `EnumResourceTypesW` from the same
dispatch table (annotation list at loader.c ~39200 area: `{ "FindResourceA", 12 }`,
`{ "FindResourceExW", 16 }`, …) and call the very same `mw_*` bodies; the PE32
image is mapped in-process at its ImageBase, so `w3_res_cmp_name` reads guest
UTF-16 strings directly. Named resources therefore work identically for x86.
**BUT one real PE32 gap exists**: `w3_res_find` reads the data directory with
**PE32+ offsets only** (13141–13143: num_dd @ oh+108, rsrc RVA @ oh+128). For a
PE32 (0x10b) module those addresses land on DataDirectory[1] (import) and
DataDirectory[4] (certificate): unsigned PE32 exes read rsrc_rva=0 → honest miss
(every FindResource fails), signed ones walk the cert table as a resource dir.
`load_pe32()`'s own loader plane reads the correct PE32 offsets (comment at
37903–37905) but installs **no resource fixup**. Not exercised by spider.exe
(PE32+), but it is the standing bug for the 57/108 x86 battery guests.

---

## TASK 3 — GAP VERDICT

**For spider.exe (PE32+): NO GAP.** The engine's FindResource/LoadImage path can
surface **all 56 RT_BITMAPs**, including every card-back candidate:

1. The tree walk is uncapped and unfiltered (13162 loop; no type whitelist);
   named resources — `CARDBACK`, `CARD1..52`, `FELT` — are fully supported on
   the W path used by `LoadBitmapW` (22072→13074–13094).
2. No ID-width or ID-range limit bites: spider IDs are 106/108 + names, all
   < 0xFFFF, read as full u32.
3. The DIB decoder accepts every spider bitmap: INFOHEADER **and** COREHEADER
   (SW-1275, 22095), bpp 1/4/8 (22110), BI_RGB (verified comp=0 on all sampled
   leaves), dims ≤ 359x242 < 4096 (22109).
4. Enumeration stubs (13257/29082) are the only unimplemented surface — spider
   does not import Enum*.

**Residual engine risks (do not block spider):**
* PE32-only: optional-header offsets at 13141–13143 are PE32+ hardcoded → x86
  guests lose FindResource entirely. Minimal fix: branch on
  `*(u16*)(pe+pe_off+24)` — Magic 0x10b → `num_dd @ pe+pe_off+24+92`,
  `rsrc_rva @ pe+pe_off+24+96+2*8`; keep 108/128 for 0x20b. Two lines.
* `mw_bitmap_slot` 256-slot pool (18050/22055) is shared lifecycle state; a
  guest that leaks HBITMAPs eventually gets NULL — lifecycle, not resource-access.
* If a future spider build picks backs via `EnumResourceNames`, the stubs at
  13257–13260 / 29082–29089 would need a real walk (mechanical: reuse the depth-1
  loop of w3_res_find).
* The "87,992 entries" figure should be corrected in the worklist to the measured
  **90 leaves / 56 RT_BITMAPs** (this build), or re-measured against whatever
  binary it was taken from.

### Runtime confirmation (no code changed)

```
cd /home/z/my-project/workspace/miniwin-engineering
rm -rf miniwin-results/spider.exe
MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=800 timeout 75 ./minwin_loader \
    artifacts/sweep061/guests/spider/spider.exe  >/tmp/sp67_out.txt 2>/tmp/sp67_err.txt
rc=0 ; frames = 523 (miniwin-results/spider.exe/screenshots/frame_001..523.bmp)
```
Guest healthy (hundreds of frames, clean rc; SWEEP-066 baseline recorded 618
frames under the same budget — same order, no regression signal; frame count is
scheduler-timing dependent).
