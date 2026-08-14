# Trace Format Specification

## AI-Native Windows Compatibility Runtime

**Version:** 0.1.0-pre
**Last Updated:** 2026-07-30

---

## Overview

There are currently **two trace formats** in the system:

1. **Wine Trace Format** — produced by `scripts/trace_collector.py` from Wine's debug output
2. **Rust Trace Format** — produced by `runtime/src/trace/mod.rs` from the Rust runtime

These formats are **not yet unified**. This document describes both.

---

## Wine Trace Format

### Source

Produced by running Windows EXEs under Wine with debug channels:
```bash
WINEDEBUG=+relay wine64 hello.exe 2>&1   # API calls
WINEDEBUG=+module wine64 hello.exe 2>&1 # DLL loading
```

### Raw Wine Relay Format

**File:** `wine_relay.raw`

Plain text, one event per line. Two line types:

**API Call:**
```
TID:Call MODULE.Function(args) ret=RETADDR
```

**API Return:**
```
TID:Ret  MODULE.Function() retval=RETVAL ret=RETADDR
```

**PE DLL Notification (Call):**
```
TID:Call PE DLL (proc=PROCPTR,module=MODULEPTR L"module.dll",reason=REASON,res=RESPTR)
```

**PE DLL Notification (Return):**
```
TID:Ret  PE DLL (proc=PROCPTR,module=MODULEPTR L"module.dll",reason=REASON,res=RESPTR) retval=RETVAL
```

**Field Types:**

| Field | Type | Example |
|-------|------|---------|
| TID | Hex thread ID | `002c`, `0034` |
| MODULE | DLL name (uppercase) | `KERNEL32`, `ntdll`, `ucrtbase` |
| Function | API function name | `HeapFree`, `RtlAllocateHeap` |
| args | Comma-separated hex values | `001b0000,00000000,00000000` |
| RETADDR | Hex return address | `6fffffc297a3` |
| RETVAL | Hex return value | `00000001` |
| PROCPTR, MODULEPTR | Hex pointers | `00006FFFFFEE2260` |
| REASON | Attach/detach reason | `PROCESS_ATTACH`, `PROCESS_DETACH` |

### Raw Wine Module Format

**File:** `wine_modules.raw`

Plain text, one event per line.

**DLL Mapping:**
```
trace:module:map_image_into_view mapping PE file L"\\??\\C:\windows\system32\ntdll.dll" at 0xADDR-0xADDR
```

**Section Mapping:**
```
trace:module:map_image_into_view mapping L"ntdll.dll" section .text at 0xADDR off 0xOFF SIZE 0xVSIZE VIRT 0xVSIZE FLAGS 0xFLAGS
```

**Relocation:**
```
trace:module:relocating L"ntdll.dll" dynamic base 0xPREF -> 0xACTUAL mapped at 0xMAPPED
```

**Module Loaded:**
```
trace:module:build_module loaded L"\\??\\C:\windows\system32\ntdll.dll" ENTRYPOINT BASE
```

### Parsed Wine Trace Files

#### execution.json

```json
{
  "trace_id": "TRACE-20260729-211123",
  "executable": "/path/to/hello_simple.exe",
  "timestamp": "2026-07-29T21:11:23.456789+00:00",
  "execution_backend": "Wine",
  "wine_version": "wine-9.0.r0.gcab93f47 ( TkG Staging Esync Fsync )",
  "exit_code": 0,
  "crashed": false,
  "execution_time_ms": 4504.89,
  "stdout": "Hello from real Windows x64 executable!\n",
  "stderr": "wineserver: using server-side synchronization.\n",
  "pe_analysis": { ... },
  "statistics": {
    "total_api_calls": 593358,
    "total_loaded_modules": 20,
    "unique_api_functions": 313,
    "unique_dlls_called": 14,
    "threads_observed": 48
  }
}
```

**pe_analysis** sub-object contains full PE file analysis:
```json
{
  "file_path": "/path/to/hello_simple.exe",
  "file_size": 87552,
  "machine": "0x8664",
  "machine_name": "AMD64",
  "number_of_sections": 13,
  "timestamp": "2026-07-29T21:06:17",
  "entry_point": "0x1350",
  "image_base": "0x140000000",
  "subsystem": 3,
  "subsystem_name": "Console",
  "sections": [
    {
      "name": ".text",
      "virtual_address": "0x1000",
      "virtual_size": "0x1826",
      "raw_data_size": "0x1a00",
      "characteristics": "0x60000020"
    }
  ],
  "imports": [
    {
      "dll": "KERNEL32.dll",
      "functions": [
        {
          "name": "HeapFree",
          "address": "0x1400039a0"
        }
      ]
    }
  ],
  "md5": "925cbb666ccbc323688f7758080be2c5",
  "sha256": "ba36d9231ea683b64f70db23fc21f297d184a36d354cee0e7b8a9a46fdf26e74"
}
```

#### api_trace.json

```json
{
  "trace_id": "TRACE-20260729-211123",
  "total_calls": 593358,
  "unique_functions": 313,
  "by_function": {
    "ntdll.RtlFreeHeap": { "count": 332784 },
    "ucrtbase.isspace": { "count": 56358 },
    "KERNEL32.HeapFree": { "count": 16513 }
  },
  "calls_by_dll": {
    "KERNEL32": ["EnterCriticalSection", "GetLastError", "HeapFree", ...],
    "ntdll": ["LdrFindResource_U", "memcmp", "memmove", ...],
    "ucrtbase": ["isspace", "wcslen", "memcpy", ...]
  },
  "sample_calls": [
    {
      "thread_id": "002c",
      "function": "ntdll.LdrDisableThreadCalloutsForDll",
      "arguments": "6fffffc00000",
      "return_address": "6fffffc297a3",
      "line_number": 1,
      "return_value": "00000000"
    }
  ]
}
```

**Notes:**
- `by_function` is sorted by call count (descending)
- `calls_by_dll` values are sorted alphabetically
- `sample_calls` contains the first 200 parsed API calls

#### environment.json

```json
{
  "trace_id": "TRACE-20260729-211123",
  "host": {
    "os": "Linux",
    "os_release": "5.10.134-013.8.3.kangaroo.al8.x86_64",
    "os_version": "#1 SMP ...",
    "architecture": "x86_64",
    "processor": "",
    "python_version": "3.12.13",
    "hostname": "..."
  },
  "wine": {
    "version": "wine-9.0.r0.gcab93f47 ( TkG Staging Esync Fsync )",
    "path": "/home/z/my-project/tools/wine-9.0-staging-tkg-amd64",
    "prefix": "/home/z/.wine-runtime"
  },
  "executable": {
    "path": "/path/to/hello_simple.exe",
    "file_size": 87552
  },
  "timestamp": "2026-07-29T21:11:23.456789+00:00",
  "libraries": {
    "pefile": true
  }
}
```

#### replay_metadata.json

```json
{
  "trace_id": "TRACE-20260729-211123",
  "replay_instructions": {
    "backend": "Wine",
    "command": "/path/to/wine64 /path/to/hello_simple.exe",
    "wine_version": "wine-9.0.r0.gcab93f47 ( TkG Staging Esync Fsync )",
    "wine_prefix": "/home/z/.wine-runtime"
  },
  "expected_results": {
    "exit_code": 0,
    "stdout_contains": "Hello from real Windows x64 executable!",
    "execution_time_ms_approx": 4504.89
  },
  "trace_files": {
    "execution": "execution.json",
    "api_trace": "api_trace.json",
    "environment": "environment.json",
    "replay": "replay_metadata.json",
    "raw_relay": "wine_relay.raw",
    "raw_modules": "wine_modules.raw"
  },
  "validation": {
    "all_data_from_real_execution": true,
    "no_simulated_data": true,
    "no_synthetic_data": true
  }
}
```

---

## Rust Trace Format

### Source

Produced by `runtime/src/trace/mod.rs` during simulated or Wine-mode execution.

### File: execution.trace

JSON Lines format (one JSON object per line):

```json
{
  "timestamp": "2026-07-29T21:06:17.123456Z",
  "category": "api_call",
  "event": {
    "thread_id": 0,
    "module": "KERNEL32.DLL",
    "function": "GetStdHandle",
    "arguments": {"nStdHandle": -11}
  }
}
```

### Event Categories

| Category | Description | Event Fields |
|----------|-------------|---------------|
| `pe_parse` | PE file parsing events | file_path, field, value |
| `memory_load` | Section mapping, memory ops | section, address, size, protection |
| `api_call` | Win32 API call dispatched | thread_id, module, function, arguments |
| `api_return` | API return value | thread_id, module, function, return_value |
| `execution` | CPU execution events | address, instruction, description |
| `crash` | Crash/exception events | signal, address, message |
| `system` | System-level events | message, metadata |

### Crash Report Format

**Directory:** `CRASH-XXXXXX/`

#### report.json

```json
{
  "crash_id": "CRASH-000001",
  "timestamp": "2026-07-29T21:06:17.123456Z",
  "pe_path": "/path/to/exe.exe",
  "exception_code": 3221225477,
  "exception_address": "0x65470000",
  "signal": "SIGSEGV",
  "events_before_crash": 15,
  "exit_code": -11
}
```

#### cpu_state.dump

Hex dump of CPU registers at crash time.

#### memory.dump

Hex dump of process memory around crash address.

#### api.trace

List of API calls made before the crash.

#### environment.json

System environment at crash time.

---

## Trace Package Directory Structure

### Wine Trace Package

```
TRACE-YYYYMMDD-HHMMSS/
├── execution.json        # Execution summary + PE analysis + statistics
├── api_trace.json        # Parsed API calls grouped by function and DLL
├── environment.json      # Host OS + Wine environment info
├── replay_metadata.json  # Replay instructions + expected results
├── wine_relay.raw        # Raw Wine +relay output (~90MB for HelloWorld)
└── wine_modules.raw      # Raw Wine +module output (~115KB)
```

### Rust Crash Package

```
CRASH-XXXXXX/
├── report.json           # Crash metadata
├── cpu_state.dump        # CPU register dump
├── memory.dump           # Memory snapshot
├── api.trace             # API call trace
├── execution.trace        # Execution trace events
└── environment.json      # System environment
```

### Rust Replay Package

```
replays/traces/YYYYMMDD-HHMMSS/
├── execution.trace        # Full execution trace
└── summary.json          # Replay result summary

replays/loop/iter-N/
├── execution.trace        # Iteration N trace
├── summary.json          # Iteration N summary
└── regression_spec.json  # Expected results for regression testing
```

---

## Future: Unified Trace Format

The two formats need to be unified. The target unified format should:

1. **Use typed JSON** for all arguments (not raw hex strings)
2. **Separate Wine infrastructure calls** from target application calls
3. **Include memory allocation events** (VirtualAlloc, HeapAlloc, etc.)
4. **Include thread lifecycle events** (create, exit)
5. **Include module events** (load, unload, getprocaddress)
6. **Be streamable** — can be written incrementally during execution
7. **Support diffing** — two traces can be compared field-by-field
8. **Include timing** — relative timestamps for ordering and duration analysis
9. **Support filtering** — by module, thread, time range, event type
10. **Be compact** — avoid 90MB for HelloWorld (current limitation)
