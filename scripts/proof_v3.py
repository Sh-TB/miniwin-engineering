#!/usr/bin/env python3
"""Execution-proof sheet v3 (2026-10-17): BUG-054b + zstd LADDER-008 evidence."""
import matplotlib.font_manager as fm
import os
for _p in ('/usr/share/fonts/truetype/chinese/NotoSansSC-Regular.ttf',
           '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
    if os.path.exists(_p):
        fm.fontManager.addfont(_p)
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Noto Sans SC']
plt.rcParams['axes.unicode_minus'] = False

BG, PANEL, GREEN, RED, GREY, ACCENT = '#0d1117', '#161b22', '#3fb950', '#f85149', '#8b949e', '#58a6ff'
MONO = {'family': 'DejaVu Sans Mono', 'size': 8.2}

fig = plt.figure(figsize=(16, 10), facecolor=BG)
fig.suptitle('MiniWin — Execution Proof v3 (2026-10-17)   7-Zip 26.04 x64 + zstd 1.5.6 x64   all EXIT 0',
             color='white', fontsize=13, fontweight='bold', y=0.97)

def panel(rect, title, lines, badge):
    ax = fig.add_axes(rect); ax.set_facecolor(PANEL)
    for s in ax.spines.values(): s.set_color('#30363d')
    ax.set_xticks([]); ax.set_yticks([])
    ax.text(0.02, 0.96, title, color=ACCENT, fontsize=10, fontweight='bold', va='top', transform=ax.transAxes)
    ax.text(0.98, 0.96, badge, color=GREEN if 'PASS' in badge or '0' in badge else RED,
            fontsize=10, fontweight='bold', va='top', ha='right', transform=ax.transAxes)
    ax.text(0.02, 0.88, '\n'.join(lines), color='#e6edf3', va='top', transform=ax.transAxes, **MONO)

panel([0.03, 0.52, 0.45, 0.40],
      '1) 7-Zip 26.04 — archive create + extract with ABSOLUTE backslash -o path (BUG-054b)',
      ['$ ./minwin_loader 7za.exe a final.7z final.txt',
       '  ...',
       '  Scanning the drive:',
       '  1 folder, 1 file, 68 bytes (1 KiB)',
       '  Creating archive: final.7z',
       '  Everything is Ok              exit=0  3/3 runs',
       '',
       '$ ./minwin_loader 7za.exe e final.7z "-o\\tmp\\7ztest54b\\out"',
       '  Everything is Ok              exit=0  3/3 runs',
       '',
       'input  sha256: 3ca45a54ccdf8f34...d851927c9bd237f99b4ed77122d5f',
       'output sha256: 3ca45a54ccdf8f34...d851927c9bd237f99b4ed77122d5f',
       '',
       'FIX: CreateDirectoryW/RemoveDirectoryW/DeleteFileW/',
       '     GetDiskFreeSpaceExW now normalize Windows paths.',
       '     Before: literal "\\tmp\\..." dir created in CWD.'],
      '3/3 SHA OK')

panel([0.52, 0.52, 0.45, 0.40],
      '2) zstd.exe 1.5.6 (LADDER-008) — --version banner, mingw/msvcrt guest',
      ['$ sha256sum zstd.exe',
       '6b5c50dde7062909b69b618fae228c72090596dc254efe498fb426f5f430a1f9',
       '$ ./minwin_loader zstd.exe --version',
       '*** Zstandard CLI (64-bit) v1.5.6, by Yann Collet ***',
       '                               exit=0  3/3 runs',
       '',
       'census: PE32+ x64 console, ImageBase 0x140000000,',
       '        KERNEL32 38 + msvcrt 77 = 115 imports',
       'zstd-wave APIs added: FindFirstFileA/FindNextFileA,',
       'SetPriorityClass, _filelengthi64, fgetpos, fsetpos,',
       'SleepConditionVariableCS'],
      'exit 0  3/3')

panel([0.03, 0.06, 0.45, 0.40],
      '3) zstd — compress + decompress round trip (BUG-061 fix)',
      ['$ ./minwin_loader zstd.exe -T1 -f payload.txt',
       'payload.txt : 119.12%  (    68 B  =>      81 B, payload.txt.zst)',
       '                               exit=0  3/3 runs',
       '',
       '$ ./minwin_loader zstd.exe -T1 -d -f payload.txt.zst -o payload_rt.txt',
       '                               exit=0  3/3 runs',
       '',
       'sha256(payload.txt) == sha256(payload_rt.txt)  byte-perfect',
       '',
       'BUG-061: _stat64 wrote POSIX offsets (mode@0/size@8) into the',
       'MS x64 struct (mode@6/size@24) AND memset 128B into a 56B',
       'struct (72B overflow) -> guest corruption -> RIP=0x0.',
       'Now exact 56-byte MS layout; regression 12/12 + G1 pin OK.'],
      '3/3 SHA OK')

panel([0.52, 0.06, 0.45, 0.40],
      '4) Gate status after this wave',
      ['G1 pin (UPX --version exit 0)        OK   3 evidence dirs today',
       'regression suite                     12/12',
       'BUG-054b  absolute backslash -o      CLOSED (TESTED) 3/3',
       'BUG-061   _stat64 layout + overflow  CLOSED (TESTED) 3/3',
       'LADDER-008 zstd.exe                  VERIFIED (v+round trip)',
       '',
       'New planning base (user directive):',
       '  .agent/PROGRAM_LADDER.md      100 programs, 10 tiers',
       '  docs/master/MSVC_RUNTIME_PLAN.md   ADR-012',
       '  docs/master/ANDROID_FEASIBILITY.md ADR-013',
       '  docs/master/GITHUB_RESOURCES_MAP.md ADR-014'],
      'ALL GREEN')

out = '/home/z/my-project/download/miniwin_execution_proof_v3_20261017.png'
import os; os.makedirs('/home/z/my-project/download', exist_ok=True)
fig.savefig(out, dpi=150, facecolor=BG)
print('saved', out)
