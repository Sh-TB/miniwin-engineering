#!/usr/bin/env python3
# SWEEP-031 SW-1305 — FindFirstFileA/FindNextFileA struct-contract fix.
#
# Root cause (putty FASTFAIL code=2 after drive-root sandbox exposed it):
#   FindFirstFileA callers pass WIN32_FIND_DATAA (318 bytes: CHAR arrays),
#   but fill_find_data always wrote the WIN32_FIND_DATAW layout (592 bytes,
#   wide names @+44..564) -> 274-byte stack overflow into the guest frame ->
#   stack cookie smash -> __report_gsfailure -> FASTFAIL(2).
#   Latent since BUG-049: every previous FindFirstFileA call hit a missing
#   directory (fill never ran). The sandbox seeds c/Windows, putty's
#   FindFirstFileA(C:\Windows\*) enumerated System32 -> first real fill ->
#   crash. Same hazard in FindNextFileA (delegates to W fill).
# Fix: W3Find gains a `wide` flag; fill_find_data_w (old layout) +
#   fill_find_data_a (318-byte A layout: header identical, CHAR names);
#   find_first_impl + find_next_match carry the flag.
import sys

SRC = "src/loader/loader.c"
txt = open(SRC, encoding="utf-8").read()
n = 0

def rep(old, new, count=1, tag=""):
    global txt, n
    c = txt.count(old)
    if c != count:
        print(f"FAIL[{tag}]: expected {count}, found {c}")
        sys.exit(1)
    txt = txt.replace(old, new, count)
    n += c
    print(f"ok[{tag}] x{c}")

rep("typedef struct { DIR* d; char pattern[4096]; char dirpart[4096]; int first; } W3Find;",
    "typedef struct { DIR* d; char pattern[4096]; char dirpart[4096]; int first;\n"
    "                 int wide; /* SWEEP-031: 1=W data (592B), 0=A data (318B) */ } W3Find;",
    1, "w3find-flag")

rep("""static void fill_find_data(void* data, const char* name, const struct stat* st) {
    if (!data) return;
    uint8_t* p = (uint8_t*)data;
    memset(p, 0, 592);""",
    """static void fill_find_data_w(void* data, const char* name, const struct stat* st) {
    if (!data) return;
    uint8_t* p = (uint8_t*)data;
    memset(p, 0, 592);""",
    1, "fill-w-rename")

rep("""    w3_utf8_to_utf16le(name, strlen(name), (uint16_t*)(p + 44), 259);
    ((uint16_t*)(p + 44))[259] = 0;
    /* cAlternateFileName left zeroed (no 8.3 names on this platform) */
}""",
    """    w3_utf8_to_utf16le(name, strlen(name), (uint16_t*)(p + 44), 259);
    ((uint16_t*)(p + 44))[259] = 0;
    /* cAlternateFileName left zeroed (no 8.3 names on this platform) */
}

/* SWEEP-031 SW-1305: ANSI WIN32_FIND_DATAA — 318 bytes total, same header
 * layout, but cFileName is CHAR[260] @+44 and cAlternateFileName CHAR[14]
 * @+304. FindFirstFileA/FindNextFileA callers pass THIS, not the W struct;
 * the old shared fill smashed 274 bytes past it (putty /GS FASTFAIL). */
static void fill_find_data_a(void* data, const char* name, const struct stat* st) {
    if (!data) return;
    uint8_t* p = (uint8_t*)data;
    memset(p, 0, 318);
    int is_dir = S_ISDIR(st->st_mode);
    uint32_t attr = is_dir ? 0x10 /*DIRECTORY*/ : 0x20 /*ARCHIVE*/;
    if (name[0] == '.') attr |= 0x2 /*HIDDEN*/;
    *(uint32_t*)(p + 0) = attr;
    *(uint64_t*)(p + 4)  = ft_from_time_t((uint64_t)st->st_ctime);
    *(uint64_t*)(p + 12) = ft_from_time_t((uint64_t)st->st_atime);
    *(uint64_t*)(p + 20) = ft_from_time_t((uint64_t)st->st_mtime);
    uint64_t sz = is_dir ? 0 : (uint64_t)st->st_size;
    *(uint32_t*)(p + 28) = (uint32_t)(sz >> 32);
    *(uint32_t*)(p + 32) = (uint32_t)sz;
    snprintf((char*)(p + 44), 260, "%s", name);
    /* cAlternateFileName[14] @+304 left zeroed (no 8.3 names here) */
}

static void fill_find_data(void* data, const char* name, const struct stat* st, int wide) {
    if (wide) fill_find_data_w(data, name, st);
    else fill_find_data_a(data, name, st);
}""",
    1, "fill-a+dispatch")

rep("""static int find_next_match(W3Find* f, void* data) {
    struct dirent* de;
    while ((de = readdir(f->d)) != NULL) {
        if (strcmp(f->pattern, "*") != 0 &&
            fnmatch(f->pattern, de->d_name, 0) != 0) continue;
        struct stat st;
        int dfd = dirfd(f->d);
        if (fstatat(dfd >= 0 ? dfd : AT_FDCWD, de->d_name, &st,
                    (dfd >= 0 ? 0 : AT_SYMLINK_NOFOLLOW)) != 0) continue;
        char full[8192];
        snprintf(full, sizeof(full), "%s/%s", f->dirpart, de->d_name);
        (void)full;
        fill_find_data(data, de->d_name, &st);
        return 1;
    }
    return 0;
}""",
    """static int find_next_match(W3Find* f, void* data) {
    struct dirent* de;
    while ((de = readdir(f->d)) != NULL) {
        if (strcmp(f->pattern, "*") != 0 &&
            fnmatch(f->pattern, de->d_name, 0) != 0) continue;
        struct stat st;
        int dfd = dirfd(f->d);
        if (fstatat(dfd >= 0 ? dfd : AT_FDCWD, de->d_name, &st,
                    (dfd >= 0 ? 0 : AT_SYMLINK_NOFOLLOW)) != 0) continue;
        char full[8192];
        snprintf(full, sizeof(full), "%s/%s", f->dirpart, de->d_name);
        (void)full;
        fill_find_data(data, de->d_name, &st, f->wide); /* SWEEP-031 */
        return 1;
    }
    return 0;
}""",
    1, "next-match-flag")

# find_first_impl: add wide param, set f->wide, fix both fill sites
rep("static void* find_first_impl(const char* p8, void* data) {",
    "static void* find_first_impl(const char* p8, void* data, int wide) {",
    1, "ffi-signature")

rep("""        W3Find* f = &g_finds[g_find_count];
        f->d = d; f->first = 1;
        snprintf(f->pattern, sizeof(f->pattern), "%s", fname);
        snprintf(f->dirpart, sizeof(f->dirpart), "%s", dirpart[0] ? dirpart : ".");
        void* h = (void*)(uintptr_t)(4 + g_find_count++);
        fill_find_data(data, fname, &st);
        return h;""",
    """        W3Find* f = &g_finds[g_find_count];
        f->d = d; f->first = 1; f->wide = wide; /* SWEEP-031 */
        snprintf(f->pattern, sizeof(f->pattern), "%s", fname);
        snprintf(f->dirpart, sizeof(f->dirpart), "%s", dirpart[0] ? dirpart : ".");
        void* h = (void*)(uintptr_t)(4 + g_find_count++);
        fill_find_data(data, fname, &st, wide);
        return h;""",
    1, "ffi-nonwild-fill")

rep("""    W3Find* f = &g_finds[g_find_count];
    f->d = d;
    f->first = 1;
    snprintf(f->pattern, sizeof(f->pattern), "%s", fname);
    snprintf(f->dirpart, sizeof(f->dirpart), "%s", dirpart[0] ? dirpart : ".");
    void* h = (void*)(uintptr_t)(4 + g_find_count++);
    if (!find_next_match(f, data)) {""",
    """    W3Find* f = &g_finds[g_find_count];
    f->d = d;
    f->first = 1;
    f->wide = wide; /* SWEEP-031 */
    snprintf(f->pattern, sizeof(f->pattern), "%s", fname);
    snprintf(f->dirpart, sizeof(f->dirpart), "%s", dirpart[0] ? dirpart : ".");
    void* h = (void*)(uintptr_t)(4 + g_find_count++);
    if (!find_next_match(f, data)) {""",
    1, "ffi-wild-flag")

# callers
rep("""    p8[sizeof(p8)-1] = '\\0';
    return find_first_impl(p8, data);
}

__attribute__((ms_abi)) int mw_FindNextFileW(void* h, void* data) {""",
    """    p8[sizeof(p8)-1] = '\\0';
    return find_first_impl(p8, data, 1 /* wide */); /* SWEEP-031 */
}

__attribute__((ms_abi)) int mw_FindNextFileW(void* h, void* data) {""",
    1, "exw-caller")

rep("""__attribute__((ms_abi)) void* mw_FindFirstFileA(const char* pattern, void* data) {
    if (!pattern) { mw_SetLastError(87); return (void*)-1; }
    MW_TRACE("FindFirstFileA(%s)", pattern);
    return find_first_impl((char*)pattern, data);
}
__attribute__((ms_abi)) int mw_FindNextFileA(void* h, void* data) {
    return mw_FindNextFileW(h, data);
}""",
    """__attribute__((ms_abi)) void* mw_FindFirstFileA(const char* pattern, void* data) {
    if (!pattern) { mw_SetLastError(87); return (void*)-1; }
    MW_TRACE("FindFirstFileA(%s)", pattern);
    return find_first_impl((char*)pattern, data, 0 /* ANSI: 318B struct */); /* SWEEP-031 */
}
__attribute__((ms_abi)) int mw_FindNextFileA(void* h, void* data) {
    /* SWEEP-031: ANSI data struct — must fill A layout, not W. */
    int idx = (int)(uintptr_t)h - 4;
    if (idx < 0 || idx >= g_find_count || !g_finds[idx].d) { mw_SetLastError(6); return 0; }
    W3Find* f = &g_finds[idx];
    if (!find_next_match(f, data)) {
        mw_SetLastError(18); /* ERROR_NO_MORE_FILES */
        return 0;
    }
    return 1;
}""",
    1, "A-callers")

# any other find_first_impl callers?
if "find_first_impl(p8, data)\n" in txt.replace("find_first_impl(p8, data, 1", ""):
    print("WARN: possible unpatched find_first_impl caller remains")

open(SRC, "w", encoding="utf-8").write(txt)
print(f"APPLIED: {n} edits")
