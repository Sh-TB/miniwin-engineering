#!/usr/bin/env python3
"""SWEEP-011: build a per-PE function map with Ghidra headless (the engine
behind `rea`) into a PERSISTENT project, then export a flat JSON consumed by
the loader's MINWIN_FUNC_MAP symbolizer.

Why: `rea function` pays a fresh 40-90 s Ghidra auto-analysis for EVERY query
(ephemeral project — evidence/sweep008_rea/README.md). This script analyzes
each PE ONCE; the map then answers "which function is RIP X?" in O(log n)
INSIDE the engine, on every [STUB]/[CRASH]/[WATCHDOG]/[PROF] line, forever.

Usage:
  python3 scripts/sweep011_funcmap_export.py exe1.exe exe2.exe ...
Env:
  GHIDRA_INSTALL_DIR (default /home/z/ghidra-dl/ghidra_12.1.4_PUBLIC)
  JAVA_HOME          (default /home/z/ghidra-dl/jdk-21)
  MW_GHIDRA_PROJECT  (default /home/z/ghidra-dl/mwproj)
Output: artifacts/sweep011_perf/funcmaps/<exe>.funcs.json
"""
import os, subprocess, sys

REPO = "/home/z/my-project/workspace/miniwin-engineering"
GDIR = os.environ.get("GHIDRA_INSTALL_DIR", "/home/z/ghidra-dl/ghidra_12.1.4_PUBLIC")
JAVA = os.environ.get("JAVA_HOME", "/home/z/ghidra-dl/jdk-21")
PROJ = os.environ.get("MW_GHIDRA_PROJECT", "/home/z/ghidra-dl/mwproj")
OUTDIR = os.path.join(REPO, "artifacts", "sweep011_perf", "funcmaps")
SCRIPTDIR = "/tmp/sweep011_ghidra_scripts"
HEADLESS = os.path.join(GDIR, "support", "analyzeHeadless")

# The postScript: Java (Ghidra 12 headless compiles .java scripts natively;
# .py needs PyGhidra which is not part of the stock distribution).
POSTSCRIPT = r'''
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Program;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FuncMapExport extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Program prog = currentProgram;
        FunctionManager fmAll = prog.getFunctionManager();
        long base = prog.getImageBase().getOffset();
        StringBuilder body = new StringBuilder();
        int n = 0;
        FunctionIterator it = fmAll.getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            long s = f.getEntryPoint().getOffset();
            long e = f.getBody().getMaxAddress().getOffset() + 1;
            if (e <= s) continue;
            if (n > 0) body.append(",\n");
            body.append("{\"name\":\"").append(esc(f.getName()))
                .append("\",\"start\":").append(s)
                .append(",\"end\":").append(e).append("}");
            n++;
        }
        String doc = "{\"exe\":\"" + esc(prog.getName()) + "\",\"image_base\":" + base
            + ",\"functions\":[\n" + body + "\n]}";
        PrintWriter w = new PrintWriter(new FileWriter(args[0]));
        w.print(doc);
        w.close();
        println("FUNCMAP_WRITTEN " + n + " -> " + args[0]);
    }
    static String esc(String s) {
        return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", " ");
    }
}
'''

def project_programs():
    """Names already inside the persistent project (from .rep file listing)."""
    rep = os.path.join(PROJ + ".rep", "projects", os.path.basename(PROJ) + ".rep")
    # cheap fallback: query via listPrograms is not exposed; use dir listing of
    # the project's data dir (program names appear as <name>.gpr-like files).
    names = set()
    data_dir = os.path.join(PROJ + ".rep", "data")
    if os.path.isdir(data_dir):
        for fn in os.listdir(data_dir):
            if fn.endswith(".idf") or fn.endswith("~"):
                continue
            names.add(fn)
    return names

def main():
    os.makedirs(OUTDIR, exist_ok=True)
    os.makedirs(SCRIPTDIR, exist_ok=True)
    os.makedirs(PROJ, exist_ok=True)
    with open(os.path.join(SCRIPTDIR, "FuncMapExport.java"), "w") as f:
        f.write(POSTSCRIPT)
    env = dict(os.environ)
    env["JAVA_HOME"] = JAVA
    env["PATH"] = JAVA + "/bin:" + env.get("PATH", "")
    # state dir marks programs already imported+analyzed into the project
    imported_dir = PROJ + ".imported"
    os.makedirs(imported_dir, exist_ok=True)
    for exe in sys.argv[1:]:
        exe = os.path.abspath(exe)
        name = os.path.basename(exe)
        out = os.path.join(OUTDIR, name + ".funcs.json")
        inproj = os.path.exists(os.path.join(imported_dir, name))
        if inproj:
            print("== %s: already analyzed -> -process -noanalysis" % name, flush=True)
            cmd = [HEADLESS, PROJ, os.path.basename(PROJ),
                   "-process", name, "-noanalysis",
                   "-scriptPath", SCRIPTDIR, "-postScript", "FuncMapExport.java", out]
        else:
            print("== %s: first import + auto-analysis (one-time 40-90 s)" % name, flush=True)
            cmd = [HEADLESS, PROJ, os.path.basename(PROJ),
                   "-import", exe,
                   "-scriptPath", SCRIPTDIR, "-postScript", "FuncMapExport.java", out]
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=900)
        log = r.stdout + "\n==== STDERR ====\n" + r.stderr
        with open("/tmp/sweep011_ghidra_%s.log" % name, "w") as f:
            f.write(log)
        ok = "FUNCMAP_WRITTEN" in r.stdout and os.path.exists(out)
        if ok and not inproj:
            open(os.path.join(imported_dir, name), "w").write(exe + "\n")
        print("   %s (rc=%d, log=/tmp/sweep011_ghidra_%s.log)" %
              ("OK" if ok else "FAILED", r.returncode, name), flush=True)

if __name__ == "__main__":
    main()
