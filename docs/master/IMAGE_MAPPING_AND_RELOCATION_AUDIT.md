# IMAGE MAPPING AND RELOCATION AUDIT — Master Record

**Produced:** TASK-005 (static audit portion), 2026-10-14, at HEAD `39181aa`
**Scope:** `load_pe()` mapping path (loader.c L3075–3093), section permission pass (L3101–3131), relocation handling (absent). Dynamic tests are queued as MAPPING-01..07 (see §4) and are prerequisites for closing this audit.

---

## 1. Current Implementation (verified from source)

| Aspect | Behavior | Line refs |
|---|---|---|
| Allocation | `mmap((void*)image_base, size_of_image, PROT_READ|WRITE|EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0)` | L3082-3084 |
| Failure policy | mmap failure → error return (no fallback path anymore) | L3086-3091 |
| Header copy | `SizeOfHeaders` bytes to image start | L3099 |
| Sections | memcpy by `SizeOfRawData` from `PointerToRawData`; zero BSS tail | L3105-3116 |
| Protections | per-section `mprotect` AFTER data copy (R default; +X/+W per characteristics) | L3124-3130 |
| Relocations | **NOT IMPLEMENTED — no .reloc parsing anywhere in loader.c** | (absence verified by search) |

## 2. Safety Findings

| ID | Question (from mission §6) | Finding | Severity |
|---|---|---|---|
| MAP-1 | Can mappings overwrite the loader or existing mappings? | **YES in principle.** `MAP_FIXED` unmaps anything intersecting `[image_base, image_base+size)`. Loader code is parked at 0x2000000 (`-Ttext-segment`), static data/heap/TEB at 0x2018000+…, so the current UPX (0x400000+0x213000) is safe. A PE with ImageBase near 0x2000000 or SizeOfImage reaching it would silently destroy loader memory | HIGH (design) |
| MAP-2 | Are preferred-ImageBase collisions detected? | **NO.** MAP_FIXED resolves collisions by destruction, not detection | HIGH |
| MAP-3 | Is the PE always mapped at the required base? | **YES** (that is the MAP_FIXED rationale; trace confirms 0x400000) | OK |
| MAP-4 | Are relocations applied when actual base differs? | **MOOT but UNSAFE:** base never differs (forced), and relocations don't exist. Any future fallback/ASLR strategy without relocation = the exact corruption scenario the fdb939c commit message describes | HIGH (latent) |
| MAP-5 | Relocation types supported? | None processed (DIR64/HIGHLOW/etc. all absent) | — |
| MAP-6 | Unsupported relocation types fail explicitly? | N/A (no parser) — must fail-loud when implemented | — |
| MAP-7 | Do page protections match section characteristics? | **Mostly.** Per-section mprotect from Characteristics. Gaps: (a) whole image starts RWX before per-section pass (window during load only); (b) mprotect length = VirtualSize rounded to SectionAlignment can spill into the next section's header page; (c) header page stays R|W|X-ish (headers copied, never re-protected) | MEDIUM |
| MAP-8 | Bounds checks on image/metadata ranges? | **WEAK:** no validation of `e_lfanew`, `SizeOfHeaders`, section `PointerToRawData+SizeOfRawData` vs file size, `VirtualAddress+VirtualSize` vs SizeOfImage, import/TLS/pdata RVAs vs image bounds | HIGH (adversarial) |
| MAP-9 | Can malformed PE metadata cause OOB? | **YES.** A crafted PointerToRawData/SizeOfRawData can read OOB from `filedata` (heap) or write OOB into the image (memcpy target bounded only by VirtualAddress trust). Import walk reads `imp->Name` etc. without bounds checks | HIGH (adversarial) |
| MAP-10 | Dangerous strategy kept just because one test passes? | The MAP_FIXED decision is *documented* and defensible for preferred-base targets, but must be paired with (a) a collision *detection* pre-check, (b) relocation support, or (c) an explicit refusal for non-preferred-base-capable images | DECISION OWED |

## 3. Required Decisions (owner/architecture)

1. **D-MAP-A:** Keep forced preferred-base mapping but ADD a pre-flight `msync`/`/proc/self/maps` collision check that FAILS LOUDLY (instead of MAP_FIXED destroying) — or probe with `MAP_FIXED_NOREPLACE` first and only then decide.
2. **D-MAP-B:** Implement `.reloc` processing (DIR64 + HIGHLOW minimum, explicit reject for others) BEFORE any second target that doesn't load at preferred base. Packed UPX (`.reloc` empty) and decompressed UPX (loads at preferred base) both avoid it today.
3. **D-MAP-C:** Add metadata bounds validation (all RVAs/Raws) with fail-closed error paths (Tier-D tests depend on this).

## 4. Required Tests (queued — Tier B/D per TESTING_STRATEGY.md)

| Test ID | Purpose | Depends on |
|---|---|---|
| MAPPING-01 | Preferred-base success (UPX-decompressed already covers; make explicit synthetic) | synthetic PE builder |
| MAPPING-02 | Preferred-base collision → must fail LOUDLY, not destroy the loader | D-MAP-A |
| MAPPING-03 | Relocation to alternate base (image with .reloc, mapped at non-preferred base) | D-MAP-B |
| MAPPING-04 | Unsupported relocation type → explicit failure | D-MAP-B |
| MAPPING-05 | Out-of-range relocation entry → explicit failure | D-MAP-B, D-MAP-C |
| MAPPING-06 | Section protection verification (read /proc/self/maps after load; compare per section) | none — can be added now |
| MAPPING-07 | Preservation of unrelated process mappings (map a sentinel region at a candidate collision address; assert sentinel intact after refused load) | D-MAP-A |

## 5. Current Verdict

**Mapping is functionally correct for the single validated target class (PE loads at preferred base, no relocations needed) and UNSAFE as a general strategy.** No regression to existing validated behavior is acceptable while redesigning: keep the 0x400000 path green (regression suite) while adding detection/relocation/bounds work as separate bounded tasks.
