#!/usr/bin/env python3
# SW-1297b (SWEEP-066): cross-validate the engine's PE32 stdcall arity table
# (g_api32_arity in src/loader/loader.c) against wine's .spec files.
# spec arg classes: ptr/str/strb/wstr/long/word/int16/handle = 4 bytes,
# double/int64 = 8 bytes, vararg = caller-side (skip stdcall check),
# cdecl = caller-cleans (arity irrelevant -> report separately).
import re
import subprocess
import sys
import urllib.request

LOADER = "/home/z/my-project/workspace/miniwin-engineering/src/loader/loader.c"
SPEC_BASE = "https://raw.githubusercontent.com/wine-mirror/wine/master/dlls"
DLLS = {
    "ADVAPI32.DLL": "advapi32/advapi32.spec",
    "COMCTL32.DLL": "comctl32/comctl32.spec",
    "COMDLG32.DLL": "comdlg32/comdlg32.spec",
    "CRYPT32.DLL": "crypt32/crypt32.spec",
    "GDI32.DLL": "gdi32/gdi32.spec",
    "GDIPLUS.DLL": "gdiplus/gdiplus.spec",
    "IMM32.DLL": "imm32/imm32.spec",
    "IPHLPAPI.DLL": "iphlpapi/iphlpapi.spec",
    "KERNEL32.DLL": "kernel32/kernel32.spec",
    "MPR.DLL": "mpr/mpr.spec",
    "MSIMG32.DLL": "msimg32/msimg32.spec",
    "NETAPI32.DLL": "netapi32/netapi32.spec",
    "ODBC32.DLL": "odbc32/odbc32.spec",
    "OLE32.DLL": "ole32/ole32.spec",
    "OLEAUT32.DLL": "oleaut32/oleaut32.spec",
    "SECUR32.DLL": "secur32/secur32.spec",
    "SHELL32.DLL": "shell32/shell32.spec",
    "SHLWAPI.DLL": "shlwapi/shlwapi.spec",
    "USER32.DLL": "user32/user32.spec",
    "WEVTAPI.DLL": "wevtapi/wevtapi.spec",
    "WINMM.DLL": "winmm/winmm.spec",
    "VERSION.DLL": "version/version.spec",
    "WINHTTP.DLL": "winhttp/winhttp.spec",
    "WTSAPI32.DLL": "wtsapi32/wtsapi32.spec",
    "PDH.DLL": "pdh/pdh.spec",
    "UXTHEME.DLL": "uxtheme/uxtheme.spec",
    "NTDLL.DLL": "ntdll/ntdll.spec",
    "UXTHEME.DLL": "uxtheme/uxtheme.spec",
}
BYTES4 = {"ptr", "str", "strb", "strali", "wstr", "wstrb", "long", "word",
          "handle", "segptr", "segstr", "s_word", "float"}
BYTES8 = {"double", "int64", "s_int64"}


def fetch_spec(url):
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            return r.read().decode(errors="replace")
    except Exception as e:
        print("FETCH FAIL %s: %s" % (url, e), file=sys.stderr)
        return None


def parse_specs():
    """name -> (argspec_string, callconv, dll) — Wine canonical name."""
    table = {}
    for dll, path in DLLS.items():
        txt = fetch_spec("%s/%s" % (SPEC_BASE, path))
        if not txt:
            continue
        for ln in txt.splitlines():
            ln = ln.strip()
            if not (ln.startswith("@") or (ln and ln[0].isdigit())):
                continue
            m = re.match(r"(?:@|\d+)\s*(stdcall|cdecl|pascal|varargs|thiscall|stub)\s+(?:-\S+\s+)*([A-Za-z0-9_]+)\s*\(([^)]*)\)", ln)
            if not m:
                continue
            cc, name, args = m.group(1), m.group(2), m.group(3).strip()
            if name in table:
                continue  # first DLL wins (matches loader ENTRY order roughly)
            table[name] = (args, cc, dll)
    return table


def spec_bytes(args, cc):
    if cc in ("cdecl", "varargs"):
        return None  # caller cleans / unknown
    if args in ("", "-"):
        return 0
    total = 0
    for a in args.split():
        a = a.strip()
        if a in BYTES4:
            total += 4
        elif a in BYTES8:
            total += 8
        elif a in ("str", "strb", "wstr"):
            total += 4
        elif a.startswith("wstr") or a.startswith("str"):
            total += 4
        else:
            return None  # unknown class — skip
    return total


def parse_engine_table():
    txt = open(LOADER).read()
    m = re.search(r"g_api32_arity\[\] = \{(.*?)\n\};", txt, re.S)
    out = []
    for ln in m.group(1).splitlines():
        e = re.match(r'\s*\{\s*"([^"]+)",\s*(\d+)\s*\}', ln)
        if e:
            out.append((e.group(1), int(e.group(2))))
    return out


def main():
    specs = parse_specs()
    eng = parse_engine_table()
    print("engine entries: %d, wine spec names: %d" % (len(eng), len(specs)))
    bad, missing, cdecl = [], [], []
    # SW-1297b manual-review exclusions: x86 ECX-fastcall SList family
    # (MSDN: first arg in ECX on x86) + RtlUnwind (SW-1151 SEH heritage).
    EXCLUDE = {"InterlockedPushEntrySList", "InterlockedPopEntrySList",
               "InterlockedFlushSList", "InitializeSListHead", "RtlUnwind"}
    for name, bytes_ in eng:
        if name not in specs:
            missing.append(name)
            continue
        args, cc, dll = specs[name]
        want = spec_bytes(args, cc)
        if want is None:
            cdecl.append((name, cc))
            continue
        if want != bytes_ and name not in EXCLUDE:
            bad.append((name, bytes_, want, args, dll))
    print("\n=== MISMATCH (%d) ===" % len(bad))
    for name, got, want, args, dll in sorted(bad):
        print("%-40s engine=%-3d wine=%-3d args=(%s) [%s]" % (name, got, want, args, dll))
    print("\n=== NOT IN WINE SPECS (%d) — manual review ===" % len(missing))
    for name in sorted(missing):
        print(" ", name)
    print("\n=== cdecl/varargs (arity unused) %d ===" % len(cdecl))
    # emit corrected table block for the mismatches
    if "--emit" in sys.argv:
        print("\n=== CORRECTIONS (python dict) ===")
        for name, got, want, args, dll in sorted(bad):
            print('    ("%s", %d),  # was %d' % (name, want, got))
    if "--apply" in sys.argv:
        fixes = {n: (g, w) for n, g, w, a, d in bad}
        txt = open(LOADER).read()
        m = re.search(r"g_api32_arity\[\] = \{(.*?)\n\};", txt, re.S)
        block = m.group(0)
        nfix = 0
        for name, (got, want) in sorted(fixes.items()):
            pat = re.compile(r'(\{\s*"%s",\s*)(\d+)(\s*\})' % re.escape(name))
            new_block, n = pat.subn(lambda mm: mm.group(1) + str(want) + mm.group(3), block)
            if n != 1:
                print("APPLY FAIL %s (n=%d)" % (name, n))
                continue
            block = new_block
            nfix += 1
        txt = txt[:m.start()] + block + txt[m.end():]
        open(LOADER, "w").write(txt)
        print("APPLIED %d/%d fixes to %s" % (nfix, len(fixes), LOADER))
        # evidence TSV
        with open("/home/z/my-project/workspace/miniwin-engineering/evidence/sweep066/arity_fixes.tsv", "w") as f:
            f.write("name\tengine_old\twine_new\targs\tdll\n")
            for name, got, want, args, dll in sorted(bad):
                f.write("%s\t%d\t%d\t%s\t%s\n" % (name, got, want, args, dll))
        print("evidence: evidence/sweep066/arity_fixes.tsv")


if __name__ == "__main__":
    main()
