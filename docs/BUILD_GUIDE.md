# Build Guide

## AI-Native Windows Compatibility Runtime

**Version:** 0.1.0-pre
**Last Updated:** 2026-07-30

---

## Prerequisites

| Requirement | Minimum | Tested With |
|-------------|---------|-------------|
| OS | Linux x86_64 | Debian 13 (trixie), kernel 5.10.134 |
| Disk Space | 500MB | 9.9GB available |
| Internet | Required (first setup) | GitHub downloads |
| sudo/root | NOT required | All tools are portable binaries |
| Python | 3.10+ | 3.12.13 |
| pip packages | pefile | pefile 2024.8.26 |

---

## Step 1: Download Tools

### Wine 9.0 Portable

```bash
cd /home/z/my-project/tools

curl -L \
  "https://github.com/Kron4ek/Wine-Builds/releases/download/9.0/wine-9.0-staging-tkg-amd64.tar.xz" \
  -o wine-portable.tar.xz

tar xf wine-portable.tar.xz
# Produces: wine-9.0-staging-tkg-amd64/
# Size: ~56MB compressed, ~200MB extracted
```

**Verify:**
```bash
./wine-9.0-staging-tkg-amd64/bin/wine64 --version
# Expected: wine-9.0.r0.gcab93f47 ( TkG Staging Esync Fsync )
```

### LLVM MinGW Cross-Compiler

```bash
cd /home/z/my-project/tools

curl -L \
  "https://github.com/mstorsjo/llvm-mingw/releases/download/20240917/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64.tar.xz" \
  -o llvm-mingw.tar.xz

tar xf llvm-mingw.tar.xz
# Produces: llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/
# Size: ~84MB compressed, ~400MB extracted
```

**Verify:**
```bash
./llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin/x86_64-w64-mingw32-gcc --version
# Expected: clang version 19.1.0
```

---

## Step 2: Initialize Wine Prefix

```bash
export WINE=/home/z/my-project/tools/wine-9.0-staging-tkg-amd64
export WINEPREFIX=/home/z/.wine-runtime

$WINE/bin/wine64 wineboot --init
```

**Expected output:** Several warnings about services failing to start (expected in headless environment).
**Expected exit code:** 0

The Wine prefix will be created at `/home/z/.wine-runtime/` containing a fake Windows filesystem.

---

## Step 3: Install Python Dependencies

```bash
pip3 install pefile
```

**Verify:**
```bash
python3 -c "import pefile; print(pefile.__version__)"
# Expected: 2024.8.26
```

---

## Step 4: Build Rust Runtime (Optional)

```bash
cd /home/z/my-project/runtime

cargo build --release
# Build time: ~30-60 seconds
# Output: target/release/winrt-ai (~50MB binary)

# Run tests
cargo test
```

**Dependencies** (auto-downloaded by Cargo):
- log 0.4, env_logger 0.11
- serde 1, serde_json 1
- chrono 0.4
- thiserror 2
- clap 4
- goblin 0.9
- hex 0.4
- uuid 1
- which 7
- tempfile 3

---

## Step 5: Compile a Windows EXE

### Option A: Simple HelloWorld (printf)

```bash
MINGW=/home/z/my-project/tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin

cat > /tmp/hello.c << 'EOF'
#include <stdio.h>
int main() {
    printf("Hello from real Windows x64 executable!\n");
    return 0;
}
EOF

$MINGW/x86_64-w64-mingw32-gcc \
  -o /home/z/my-project/tools/test-binaries-real/hello_simple.exe \
  /tmp/hello.c \
  -lucrt -mconsole -O2
```

### Option B: WinMain (WriteConsoleA)

```bash
MINGW=/home/z/my-project/tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin

cat > /home/z/my-project/tools/test-binaries-real/hello.c << 'EOF'
#include <windows.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    const char msg[] = "Hello from real Windows x64 executable!\r\n";
    WriteConsoleA(hStdOut, msg, sizeof(msg) - 1, &written, NULL);
    ExitProcess(0);
    return 0;
}
EOF

$MINGW/x86_64-w64-mingw32-gcc \
  -o /home/z/my-project/tools/test-binaries-real/hello_real.exe \
  /home/z/my-project/tools/test-binaries-real/hello.c \
  -lkernel32 -luser32 \
  -static -mconsole -O2
```

**Verify:**
```bash
file /home/z/my-project/tools/test-binaries-real/hello_simple.exe
# Expected: PE32+ executable for MS Windows 6.00 (console), x86-64
```

### Generate Minimal Synthetic PE (Python)

```bash
python3 /home/z/my-project/scripts/gen_test_pe.py
# Output: tests/fixtures/minimal_pe64.exe (2,048 bytes)
```

---

## Step 6: Run EXE Under Wine

```bash
export WINE=/home/z/my-project/tools/wine-9.0-staging-tkg-amd64
export WINEPREFIX=/home/z/.wine-runtime
export WINEDEBUG=-all

$WINE/bin/wine64 /home/z/my-project/tools/test-binaries-real/hello_simple.exe
```

**Expected stdout:**
```
Hello from real Windows x64 executable!
```

**Expected exit code:** 0

**Important notes:**
- Always use `wine64` directly, NOT the `wine` wrapper script
- Always set `WINEPREFIX` before running
- First run after `wineboot --init` may take 5+ seconds

---

## Step 7: Generate Full Trace Package

```bash
python3 /home/z/my-project/scripts/trace_collector.py \
  /home/z/my-project/tools/test-binaries-real/hello_simple.exe
```

**Expected output:**
```
======================================================================
REAL TRACE COLLECTOR
======================================================================
EXE:    /home/z/my-project/tools/test-binaries-real/hello_simple.exe
Wine:   /home/z/my-project/tools/wine-9.0-staging-tkg-amd64
Prefix: /home/z/.wine-runtime
Output: /home/z/my-project/traces
======================================================================
PE file verified: 87552 bytes
[1/6] Running EXE under Wine (clean execution)...
[2/6] Capturing API call trace (+relay)...
  Relay trace captured in ~2000ms (~90MB)
[3/6] Capturing module loading trace (+module)...
[4/6] Parsing relay trace (API calls)...
  Found ~593000 API call/return pairs
[5/6] Parsing module trace (loaded DLLs)...
  Found 20 loaded modules
[6/6] Analyzing PE file structure...

======================================================================
TRACE PACKAGE COMPLETE
======================================================================
```

**Output files** (in `/home/z/my-project/traces/TRACE-YYYYMMDD-HHMMSS/`):

| File | Content | Typical Size |
|------|---------|-------------|
| execution.json | Execution summary, PE analysis, statistics | ~9KB |
| api_trace.json | Parsed API calls grouped by function/DLL | ~74KB |
| environment.json | Host OS, Wine version, paths | ~731B |
| replay_metadata.json | Replay command, expected results | ~879B |
| wine_relay.raw | Raw Wine +relay output | ~90MB |
| wine_modules.raw | Raw Wine +module output | ~115KB |

---

## Step 8: Run Rust Runtime Commands

```bash
RUNTIME=/home/z/my-project/runtime
BIN=$RUNTIME/target/release/winrt-ai

# Analyze PE file structure
$BIN analyze /home/z/my-project/tools/test-binaries-real/hello_simple.exe

# Execute with simulated backend
$BIN run /home/z/my-project/tests/fixtures/minimal_pe64.exe --backend simulated

# Execute with Wine backend
$BIN run /home/z/my-project/tools/test-binaries-real/hello_simple.exe --backend wine

# Replay a trace
$BIN replay /home/z/my-project/replays/traces/<trace-dir>

# Analyze a crash report
$BIN crash-analyze /home/z/my-project/runtime/replays/CRASH-000001

# Dump full PE structure as JSON
$BIN dump /home/z/my-project/tools/test-binaries-real/hello_simple.exe

# Run AI debug loop (3 iterations with regression checking)
$BIN loop /home/z/my-project/tests/fixtures/minimal_pe64.exe --iterations 3
```

---

## Troubleshooting

### "wine: not found" or "command not found"

Make sure you're calling `wine64` directly, not `wine`:
```bash
# WRONG
wine hello.exe

# CORRECT
/home/z/my-project/tools/wine-9.0-staging-tkg-amd64/bin/wine64 hello.exe
```

### Wine initialization errors

If `wineboot --init` fails with RPC errors, try deleting and reinitializing:
```bash
rm -rf /home/z/.wine-runtime
export WINE=/home/z/my-project/tools/wine-9.0-staging-tkg-amd64
export WINEPREFIX=/home/z/.wine-runtime
$WINE/bin/wine64 wineboot --init
```

### Trace collector produces 0 API calls

If the relay parser finds 0 calls, the regex patterns may not match. Check the raw file:
```bash
head -20 /home/z/my-project/traces/TRACE-*/wine_relay.raw
```
Lines should start with `TID:Call` or `TID:Ret`.

### Compiler can't find headers

The LLVM MinGW portable includes headers. Make sure you use the correct path:
```bash
MINGW=/home/z/my-project/tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin
$MINGW/x86_64-w64-mingw32-gcc --version
```

### No stdout from Wine execution

Wine console emulation may not capture `WriteConsoleA` output properly.
Use `printf` or `puts` instead of `WriteConsoleA` for reliable output capture.

---

## Clean Rebuild

```bash
# Delete everything except source code and tools
rm -rf /home/z/my-project/runtime/target
rm -rf /home/z/my-project/traces/*
rm -rf /home/z/.wine-runtime

# Rebuild
cd /home/z/my-project/runtime
cargo build --release

# Reinitialize Wine
export WINE=/home/z/my-project/tools/wine-9.0-staging-tkg-amd64
export WINEPREFIX=/home/z/.wine-runtime
$WINE/bin/wine64 wineboot --init
```
