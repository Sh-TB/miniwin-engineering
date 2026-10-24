#!/usr/bin/env python3
# SWEEP-065 — build the 20-program Ghidra batch spec.
# Batch audit mandate («۲۰ تا برنامه رو با این ابزارها بررسی بکن و اجرا ببین»):
# WinRAR + Notepad++ (GitHub #1 Windows program) + 7-Zip 7zr + 13 NirSoft tools
# + 4 XP classics. Output: scripts/sweep065_spec.json
import json
import os

R = "/home/z/my-project/workspace/miniwin-engineering"
S62 = os.path.join(R, "artifacts/sweep062/x")
G61 = os.path.join(R, "artifacts/sweep061/guests")
DL65 = os.path.join(R, "artifacts/sweep065/dl")
DL62 = os.path.join(R, "artifacts/sweep062/dl")

nirsoft = {
    "devmanview": "DevManView.exe",
    "dnsquerysniffer": "DNSQuerySniffer.exe",
    "driverview": "DriverView.exe",
    "faview": "faview.exe",
    "heapmemview": "HeapMemView.exe",
    "htmlastext": "HtmlAsText.exe",
    "iconsext": "iconsext.exe",
    "installedappview": "InstalledAppView.exe",
    "multimonitortool": "MultiMonitorTool.exe",
    "recentfilesview": "RecentFilesView.exe",
    "resourcesextract": "ResourcesExtract.exe",
    "simpleprogramdebugger": "SimpleProgramDebugger.exe",
    "urlprotocolview": "URLProtocolView.exe",
}
xpc = ["freecell", "spider", "calc", "mspaint"]

jobs = []
jobs.append({
    "binary": os.path.join(DL62, "winrar712.exe"),
    "out": os.path.join(R, "evidence/sweep065/ghidra/winrar712"),
    "targets": [],
})
jobs.append({
    "binary": os.path.join(DL65, "npp/notepad++.exe"),
    "out": os.path.join(R, "evidence/sweep065/ghidra/notepadpp"),
    "targets": [],
})
jobs.append({
    "binary": os.path.join(DL65, "7zr.exe"),
    "out": os.path.join(R, "evidence/sweep065/ghidra/7zr"),
    "targets": [],
})
for name, exe in nirsoft.items():
    jobs.append({
        "binary": os.path.join(S62, name, exe),
        "out": os.path.join(R, "evidence/sweep065/ghidra", name),
        "targets": [],
    })
for name in xpc:
    jobs.append({
        "binary": os.path.join(G61, name, "%s.exe" % name),
        "out": os.path.join(R, "evidence/sweep065/ghidra", name),
        "targets": [],
    })

missing = [j["binary"] for j in jobs if not os.path.isfile(j["binary"])]
if missing:
    raise SystemExit("MISSING: %s" % missing)

out = os.path.join(R, "scripts/sweep065_spec.json")
with open(out, "w") as f:
    json.dump(jobs, f, indent=1)
print("SPEC %s : %d jobs" % (out, len(jobs)))
for j in jobs:
    print("  %8d  %s" % (os.path.getsize(j["binary"]), j["binary"]))
