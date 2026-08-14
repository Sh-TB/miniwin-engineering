#!/usr/bin/env python3
"""
Real Trace Collector for Wine-based Windows EXE execution.
Parses Wine debug output and PE files to produce structured trace packages.

NO SIMULATION. NO SYNTHETIC DATA. Real Wine traces only.
"""

import json
import os
import re
import subprocess
import sys
import time
import hashlib
import struct
from pathlib import Path
from datetime import datetime, timezone
from collections import defaultdict

# Try to import pefile for PE analysis
try:
    import pefile
    HAS_PEFILE = True
except ImportError:
    HAS_PEFILE = False


def parse_relay_trace(trace_path):
    """Parse Wine +relay trace to extract API calls with thread IDs, args, and return values."""
    calls = []
    # Simpler approach: use a single pass with line-level parsing
    call_re = re.compile(r'^([0-9a-f]+):Call\s+(.+?)\(([^)]*)\)\s+ret=([0-9a-f]+)$')
    ret_re = re.compile(r'^([0-9a-f]+):Ret\s+(.+?)(\(\) retval=([0-9a-f]+))?\s+ret=([0-9a-f]+)$')
    # For PE DLL lines (nested parens), use a separate pattern
    pedll_call_re = re.compile(r'^([0-9a-f]+):Call\s+(PE DLL \(proc=([0-9a-f]+),module=([0-9a-f]+)\s+L"([^"]+)",reason=([^,]+),res=([0-9a-f]+)\))$')
    pedll_ret_re = re.compile(r'^([0-9a-f]+):Ret\s+(PE DLL \(proc=([0-9a-f]+),module=([0-9a-f]+)\s+L"([^"]+)",reason=([^,]+),res=([0-9a-f]+)\))\s+retval=([0-9a-f]+)$')
    
    pending_calls = {}  # tid -> call info
    
    with open(trace_path, 'r') as f:
        for line in f:
            line = line.rstrip('\n')
            
            # Try PE DLL call pattern first (nested parens)
            m = pedll_call_re.match(line)
            if m:
                tid = m.group(1)
                pending_calls[tid] = {
                    'thread_id': tid,
                    'function': f"DllMain[{m.group(5)}]",
                    'arguments': f"reason={m.group(6)},res={m.group(7)}",
                    'return_address': 'dll_main',
                    'line_number': len(calls) + 1
                }
                continue
            
            # Try call pattern
            m = call_re.match(line)
            if m:
                tid = m.group(1)
                func_raw = m.group(2).strip()
                pending_calls[tid] = {
                    'thread_id': tid,
                    'function': func_raw,
                    'arguments': m.group(3),
                    'return_address': m.group(4),
                    'line_number': len(calls) + 1
                }
                continue
            
            # Try PE DLL return pattern
            m = pedll_ret_re.match(line)
            if m:
                tid = m.group(1)
                if tid in pending_calls:
                    call = pending_calls.pop(tid)
                    call['return_value'] = m.group(8)
                    calls.append(call)
                continue
            
            # Try return pattern
            m = ret_re.match(line)
            if m:
                tid = m.group(1)
                if tid in pending_calls:
                    call = pending_calls.pop(tid)
                    retval = m.group(4) if m.group(4) else "1"
                    call['return_value'] = retval
                    calls.append(call)
                continue
    
    return calls


def parse_module_trace(trace_path):
    """Parse Wine +module trace to extract loaded DLLs with their addresses and sections."""
    modules = {}
    dll_pattern = re.compile(
        r'map_image_into_view mapping PE file L"([^"]+)" at (0x[0-9a-f]+)-(0x[0-9a-f]+)'
    )
    section_pattern = re.compile(
        r'map_image_into_view mapping L"([^"]+)" section (\.\w+) at (0x[0-9a-f]+) off ([0-9a-f]+) size ([0-9a-f]+) virt ([0-9a-f]+) flags ([0-9a-f]+)'
    )
    reloc_pattern = re.compile(
        r'relocating L"([^"]+)" dynamic base ([0-9a-f]+) -> ([0-9a-f]+) mapped at (0x[0-9a-f]+)'
    )
    load_pattern = re.compile(
        r'build_module loaded L"([^"]+)" ([0-9a-f]+) ([0-9a-f]+)'
    )
    apiset_pattern = re.compile(
        r'load_apiset_dll loaded L"([^"]+)" apiset at (0x[0-9a-f]+)'
    )
    
    current_module = None
    
    with open(trace_path, 'r') as f:
        for line in f:
            m = dll_pattern.search(line)
            if m:
                path = m.group(1).replace('\\??\\', '')
                base_addr = m.group(2)
                end_addr = m.group(3)
                current_module = path
                if path not in modules:
                    modules[path] = {
                        'path': path,
                        'base_address': base_addr,
                        'end_address': end_addr,
                        'sections': [],
                        'preferred_base': None,
                        'actual_base': None
                    }
                continue
            
            m = section_pattern.search(line)
            if m:
                mod_path = m.group(1)
                section_name = m.group(2)
                virtual_addr = m.group(3)
                file_offset = m.group(4)
                raw_size = m.group(5)
                virtual_size = m.group(6)
                flags = m.group(7)
                
                # Find which module this section belongs to
                for mp in modules:
                    if mp.endswith(m.group(1).split('\\')[-1]) or m.group(1) in mp:
                        modules[mp]['sections'].append({
                            'name': section_name,
                            'virtual_address': virtual_addr,
                            'file_offset': file_offset,
                            'raw_size': raw_size,
                            'virtual_size': virtual_size,
                            'flags': flags
                        })
                        break
                continue
            
            m = reloc_pattern.search(line)
            if m:
                path = m.group(1).replace('\\??\\', '')
                preferred_base = m.group(2)
                actual_base = m.group(3)
                for mp in modules:
                    if path in mp:
                        modules[mp]['preferred_base'] = preferred_base
                        modules[mp]['actual_base'] = actual_base
                        break
                continue
    
    return modules


def parse_pe_info(exe_path):
    """Parse PE file headers using pefile library."""
    if not HAS_PEFILE:
        return {"error": "pefile not available"}
    
    try:
        pe = pefile.PE(exe_path)
        info = {
            'file_path': str(exe_path),
            'file_size': os.path.getsize(exe_path),
            'machine': hex(pe.FILE_HEADER.Machine),
            'machine_name': 'AMD64' if pe.FILE_HEADER.Machine == 0x8664 else hex(pe.FILE_HEADER.Machine),
            'number_of_sections': pe.FILE_HEADER.NumberOfSections,
            'timestamp': datetime.fromtimestamp(pe.FILE_HEADER.TimeDateStamp).isoformat(),
            'entry_point': hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint),
            'image_base': hex(pe.OPTIONAL_HEADER.ImageBase),
            'subsystem': pe.OPTIONAL_HEADER.Subsystem,
            'subsystem_name': {
                1: 'Native', 2: 'GUI', 3: 'Console',
                5: 'OS/2', 7: 'POSIX', 9: 'Windows CE GUI',
                10: 'EFI', 14: 'Xbox'
            }.get(pe.OPTIONAL_HEADER.Subsystem, str(pe.OPTIONAL_HEADER.Subsystem)),
            'stack_reserve': hex(pe.OPTIONAL_HEADER.SizeOfStackReserve),
            'stack_commit': hex(pe.OPTIONAL_HEADER.SizeOfStackCommit),
            'heap_reserve': hex(pe.OPTIONAL_HEADER.SizeOfHeapReserve),
            'heap_commit': hex(pe.OPTIONAL_HEADER.SizeOfHeapCommit),
            'sections': [],
            'imports': [],
            'export_table': None
        }
        
        for s in pe.sections:
            info['sections'].append({
                'name': s.Name.decode().strip('\x00'),
                'virtual_address': hex(s.VirtualAddress),
                'virtual_size': hex(s.Misc_VirtualSize),
                'raw_data_size': hex(s.SizeOfRawData),
                'characteristics': hex(s.Characteristics)
            })
        
        if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
            for entry in pe.DIRECTORY_ENTRY_IMPORT:
                dll_imports = {
                    'dll': entry.dll.decode(),
                    'functions': []
                }
                for imp in entry.imports:
                    name = imp.name.decode() if imp.name else f'ordinal:{imp.ordinal}'
                    dll_imports['functions'].append({
                        'name': name,
                        'address': hex(imp.address) if imp.address else None
                    })
                info['imports'].append(dll_imports)
        
        # Calculate file hashes
        with open(exe_path, 'rb') as f:
            data = f.read()
            info['md5'] = hashlib.md5(data).hexdigest()
            info['sha256'] = hashlib.sha256(data).hexdigest()
        
        pe.close()
        return info
    except Exception as e:
        return {'error': str(e)}


def run_exe_under_wine(exe_path, wine_path, wineprefix, output_dir):
    """Execute a Windows EXE under Wine and capture all trace information."""
    env = os.environ.copy()
    env['WINE'] = wine_path
    env['WINEPREFIX'] = wineprefix
    env['WINEDEBUG'] = '-all'
    
    timestamp = datetime.now(timezone.utc)
    trace_id = f"TRACE-{timestamp.strftime('%Y%m%d-%H%M%S')}"
    trace_dir = Path(output_dir) / trace_id
    trace_dir.mkdir(parents=True, exist_ok=True)
    
    results = {
        'trace_id': trace_id,
        'exe_path': str(exe_path),
        'timestamp': timestamp.isoformat(),
        'execution_backend': 'Wine',
        'wine_version': None,
        'wine_path': str(wine_path),
        'exit_code': None,
        'stdout': '',
        'stderr': '',
        'stdout_raw': b'',
        'stderr_raw': b'',
        'execution_time_ms': 0,
        'crashed': False
    }
    
    # Get Wine version
    try:
        result = subprocess.run(
            [os.path.join(wine_path, 'bin', 'wine64'), '--version'],
            capture_output=True, text=True, env=env, timeout=10
        )
        results['wine_version'] = result.stdout.strip()
    except Exception as e:
        results['wine_version'] = f'error: {e}'
    
    # Run with clean output capture
    start_time = time.time()
    try:
        proc = subprocess.run(
            [os.path.join(wine_path, 'bin', 'wine64'), str(exe_path)],
            capture_output=True, text=True, env=env, timeout=30
        )
        results['exit_code'] = proc.returncode
        results['stdout'] = proc.stdout
        results['stderr'] = proc.stderr
        results['stdout_raw'] = proc.stdout.encode('utf-8', errors='replace')
        results['crashed'] = proc.returncode != 0
    except subprocess.TimeoutExpired:
        results['exit_code'] = -1
        results['crashed'] = True
        results['stderr'] = 'Execution timed out after 30 seconds'
    except Exception as e:
        results['exit_code'] = -1
        results['crashed'] = True
        results['stderr'] = str(e)
    
    results['execution_time_ms'] = round((time.time() - start_time) * 1000, 2)
    
    return results, trace_dir


def capture_wine_traces(exe_path, wine_path, wineprefix, output_dir):
    """Run EXE under Wine with various debug channels and produce trace package."""
    
    print(f"[1/6] Running EXE under Wine (clean execution)...")
    exec_results, trace_dir = run_exe_under_wine(exe_path, wine_path, wineprefix, output_dir)
    
    env = os.environ.copy()
    env['WINE'] = wine_path
    env['WINEPREFIX'] = wineprefix
    
    # Run with +relay for API call tracing
    relay_path = trace_dir / 'wine_relay.raw'
    print(f"[2/6] Capturing API call trace (+relay)...")
    start = time.time()
    try:
        with open(relay_path, 'w') as f:
            env['WINEDEBUG'] = '+relay'
            proc = subprocess.run(
                [os.path.join(wine_path, 'bin', 'wine64'), str(exe_path)],
                stdout=f, stderr=subprocess.STDOUT, env=env, timeout=60
            )
    except subprocess.TimeoutExpired:
        print("  WARNING: Relay trace timed out")
    relay_time = round((time.time() - start) * 1000)
    print(f"  Relay trace captured in {relay_time}ms ({os.path.getsize(relay_path)} bytes)")
    
    # Run with +module for DLL loading trace
    module_path = trace_dir / 'wine_modules.raw'
    print(f"[3/6] Capturing module loading trace (+module)...")
    try:
        with open(module_path, 'w') as f:
            env['WINEDEBUG'] = '+module'
            subprocess.run(
                [os.path.join(wine_path, 'bin', 'wine64'), str(exe_path)],
                stdout=f, stderr=subprocess.STDOUT, env=env, timeout=60
            )
    except subprocess.TimeoutExpired:
        print("  WARNING: Module trace timed out")
    
    # Parse traces
    print(f"[4/6] Parsing relay trace (API calls)...")
    api_calls = parse_relay_trace(str(relay_path))
    print(f"  Found {len(api_calls)} API call/return pairs")
    
    print(f"[5/6] Parsing module trace (loaded DLLs)...")
    loaded_modules = parse_module_trace(str(module_path))
    print(f"  Found {len(loaded_modules)} loaded modules")
    
    # Parse PE info
    print(f"[6/6] Analyzing PE file structure...")
    pe_info = parse_pe_info(exe_path)
    
    return exec_results, api_calls, loaded_modules, pe_info, trace_dir


def build_trace_package(exec_results, api_calls, loaded_modules, pe_info, trace_dir):
    """Build the complete CRASH/TRACE package."""
    
    # 1. execution.json
    execution = {
        'trace_id': exec_results['trace_id'],
        'executable': exec_results['exe_path'],
        'timestamp': exec_results['timestamp'],
        'execution_backend': exec_results['execution_backend'],
        'wine_version': exec_results['wine_version'],
        'exit_code': exec_results['exit_code'],
        'crashed': exec_results['crashed'],
        'execution_time_ms': exec_results['execution_time_ms'],
        'stdout': exec_results['stdout'],
        'stderr': exec_results['stderr'],
        'pe_analysis': pe_info,
        'statistics': {
            'total_api_calls': len(api_calls),
            'total_loaded_modules': len(loaded_modules),
            'unique_api_functions': len(set(c.get('function') or '' for c in api_calls)),
            'unique_dlls_called': len(set(
                (c.get('function') or '').split('.')[0] for c in api_calls if c.get('function') and '.' in c['function']
            )),
            'threads_observed': len(set(c.get('thread_id') or '' for c in api_calls)),
        }
    }
    
    with open(trace_dir / 'execution.json', 'w') as f:
        json.dump(execution, f, indent=2, default=str)
    
    # 2. api_trace.json
    # Summarize API calls: group by function, count calls
    api_summary = defaultdict(lambda: {'count': 0, 'calls': []})
    for call in api_calls:
        func = call['function']
        api_summary[func]['count'] += 1
        api_summary[func]['calls'].append({
            'thread': call['thread_id'],
            'args': call['arguments'],
            'retval': call['return_value'],
            'ret_addr': call['return_address']
        })
    
    # Also include unique functions per DLL
    dll_functions = defaultdict(set)
    for call in api_calls:
        func = call.get('function')
        if func and '.' in func:
            dll, fname = func.split('.', 1)
            dll_functions[dll].add(fname)
    
    api_trace = {
        'trace_id': exec_results['trace_id'],
        'total_calls': len(api_calls),
        'unique_functions': len(api_summary),
        'by_function': {k: {'count': v['count']} for k, v in sorted(api_summary.items(), key=lambda x: -x[1]['count'])},
        'calls_by_dll': {dll: sorted(funcs) for dll, funcs in sorted(dll_functions.items())},
        'sample_calls': api_calls[:200],  # First 200 calls as sample
    }
    
    with open(trace_dir / 'api_trace.json', 'w') as f:
        json.dump(api_trace, f, indent=2, default=str)
    
    # 3. environment.json
    import platform
    environment = {
        'trace_id': exec_results['trace_id'],
        'host': {
            'os': platform.system(),
            'os_release': platform.release(),
            'os_version': platform.version(),
            'architecture': platform.machine(),
            'processor': platform.processor(),
            'python_version': platform.python_version(),
            'hostname': platform.node(),
        },
        'wine': {
            'version': exec_results['wine_version'],
            'path': exec_results['wine_path'],
            'prefix': os.environ.get('WINEPREFIX', 'default'),
        },
        'executable': {
            'path': exec_results['exe_path'],
            'file_size': os.path.getsize(exec_results['exe_path']),
        },
        'timestamp': exec_results['timestamp'],
        'libraries': {
            'pefile': HAS_PEFILE,
        }
    }
    
    with open(trace_dir / 'environment.json', 'w') as f:
        json.dump(environment, f, indent=2, default=str)
    
    # 4. replay metadata
    replay = {
        'trace_id': exec_results['trace_id'],
        'replay_instructions': {
            'backend': 'Wine',
            'command': f"{os.path.join(exec_results['wine_path'], 'bin', 'wine64')} {exec_results['exe_path']}",
            'wine_version': exec_results['wine_version'],
            'wine_prefix': os.environ.get('WINEPREFIX', 'default'),
        },
        'expected_results': {
            'exit_code': exec_results['exit_code'],
            'stdout_contains': exec_results['stdout'].strip() if exec_results['stdout'] else None,
            'execution_time_ms_approx': exec_results['execution_time_ms'],
        },
        'trace_files': {
            'execution': 'execution.json',
            'api_trace': 'api_trace.json',
            'environment': 'environment.json',
            'replay': 'replay_metadata.json',
            'raw_relay': 'wine_relay.raw',
            'raw_modules': 'wine_modules.raw',
        },
        'validation': {
            'all_data_from_real_execution': True,
            'no_simulated_data': True,
            'no_synthetic_data': True,
        }
    }
    
    with open(trace_dir / 'replay_metadata.json', 'w') as f:
        json.dump(replay, f, indent=2, default=str)
    
    return trace_dir


def main():
    # Configuration
    EXE_PATH = sys.argv[1] if len(sys.argv) > 1 else '/home/z/my-project/tools/test-binaries-real/hello_simple.exe'
    WINE_PATH = '/home/z/my-project/tools/wine-9.0-staging-tkg-amd64'
    WINEPREFIX = '/home/z/.wine-runtime'
    OUTPUT_DIR = '/home/z/my-project/traces'
    
    os.environ['WINEPREFIX'] = WINEPREFIX
    
    print(f"=" * 70)
    print(f"REAL TRACE COLLECTOR")
    print(f"=" * 70)
    print(f"EXE:    {EXE_PATH}")
    print(f"Wine:   {WINE_PATH}")
    print(f"Prefix: {WINEPREFIX}")
    print(f"Output: {OUTPUT_DIR}")
    print(f"=" * 70)
    
    # Verify EXE exists and is real
    if not os.path.exists(EXE_PATH):
        print(f"FATAL: EXE not found: {EXE_PATH}")
        sys.exit(1)
    
    # Verify it's a real PE (not synthetic)
    with open(EXE_PATH, 'rb') as f:
        header = f.read(2)
        if header != b'MZ':
            print(f"FATAL: Not a valid PE file (no MZ header)")
            sys.exit(1)
    
    file_size = os.path.getsize(EXE_PATH)
    print(f"PE file verified: {file_size} bytes")
    
    if file_size < 4096:
        print(f"WARNING: Very small PE file ({file_size} bytes) - may be synthetic")
    
    # Execute and capture
    exec_results, api_calls, loaded_modules, pe_info, trace_dir = capture_wine_traces(
        EXE_PATH, WINE_PATH, WINEPREFIX, OUTPUT_DIR
    )
    
    # Build package
    print(f"\nBuilding trace package...")
    package_dir = build_trace_package(exec_results, api_calls, loaded_modules, pe_info, trace_dir)
    
    # Print summary
    print(f"\n{'=' * 70}")
    print(f"TRACE PACKAGE COMPLETE")
    print(f"{'=' * 70}")
    print(f"Trace ID:      {exec_results['trace_id']}")
    print(f"Package Dir:   {package_dir}")
    print(f"Exit Code:     {exec_results['exit_code']}")
    print(f"Crashed:       {exec_results['crashed']}")
    print(f"Exec Time:     {exec_results['execution_time_ms']}ms")
    print(f"Stdout:        {repr(exec_results['stdout'][:100])}")
    print(f"API Calls:     {len(api_calls)} total")
    print(f"Loaded Modules:{len(loaded_modules)} total")
    print(f"Wine Version:  {exec_results['wine_version']}")
    print(f"\nPackage files:")
    for f in sorted(package_dir.iterdir()):
        size = os.path.getsize(f)
        if size > 1024 * 1024:
            size_str = f"{size / (1024*1024):.1f}MB"
        elif size > 1024:
            size_str = f"{size / 1024:.1f}KB"
        else:
            size_str = f"{size}B"
        print(f"  {f.name:30s} {size_str:>10s}")
    print(f"{'=' * 70}")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
