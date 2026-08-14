//! # Win32 Import Dispatch Layer
//!
//! Minimal Win32 API compatibility layer that intercepts API calls from
//! loaded Windows binaries and dispatches them to Linux equivalents.
//!
//! Supports:
//! - Console I/O (GetStdHandle, WriteConsoleA/W)
//! - Process management (ExitProcess, GetCurrentProcessId)
//! - Memory management (VirtualAlloc, VirtualFree, HeapAlloc, HeapFree)
//! - Module management (GetModuleHandleA/W, LoadLibraryA/W)
//! - Timing (QueryPerformanceCounter/Frequency, GetSystemTimeAsFileTime)
//!
//! Each API call produces trace events for deterministic replay.

use crate::trace::{ApiCallKind, TraceRecorder};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

/// Result from handling an API call.
#[derive(Debug, Clone)]
pub struct ApiHandlerResult {
    pub return_value: u64,
    pub last_error: u32,
    pub output: Option<String>,
}

/// A handler function signature.
type ApiHandler = fn(&ApiHandlerContext, &serde_json::Value) -> ApiHandlerResult;

/// Context passed to API handlers.
pub struct ApiHandlerContext {
    pub process_id: u32,
    pub exit_code: Arc<std::sync::atomic::AtomicI32>,
    pub heap: Arc<crate::dispatch::FakeHeap>,
}

/// Minimal fake heap allocator for tracking allocations.
pub struct FakeHeap {
    next_addr: AtomicU32,
    allocations: std::sync::Mutex<HashMap<u64, u32>>,
}

impl FakeHeap {
    pub fn new(base: u32) -> Self {
        Self {
            next_addr: AtomicU32::new(base),
            allocations: std::sync::Mutex::new(HashMap::new()),
        }
    }

    pub fn allocate(&self, size: u32) -> u64 {
        let addr = self.next_addr.fetch_add(size, Ordering::SeqCst) as u64;
        // Align to 16 bytes
        self.next_addr.store(
            (self.next_addr.load(Ordering::SeqCst) + 15) & !15,
            Ordering::SeqCst,
        );
        self.allocations.lock().unwrap().insert(addr, size);
        addr
    }

    pub fn free(&self, addr: u64) -> bool {
        self.allocations.lock().unwrap().remove(&addr).is_some()
    }

    pub fn allocated_count(&self) -> usize {
        self.allocations.lock().unwrap().len()
    }
}

/// The Win32 dispatch layer.
pub struct Win32Dispatcher {
    handlers: HashMap<(String, String), ApiHandler>,
    trace: TraceRecorder,
    process_id: u32,
    exit_code: Arc<std::sync::atomic::AtomicI32>,
    heap: Arc<FakeHeap>,
    call_count: AtomicU32,
}

impl Win32Dispatcher {
    /// Create a new dispatcher with built-in handlers.
    pub fn new(trace: TraceRecorder) -> Self {
        let process_id = std::process::id();
        let exit_code = Arc::new(std::sync::atomic::AtomicI32::new(0));
        let heap = Arc::new(FakeHeap::new(0x00800000)); // 8MB base for heap

        let mut dispatcher = Self {
            handlers: HashMap::new(),
            trace,
            process_id,
            exit_code,
            heap,
            call_count: AtomicU32::new(0),
        };

        // Register handlers — KERNEL32.DLL
        dispatcher.register("KERNEL32.DLL", "GetStdHandle", api_get_std_handle);
        dispatcher.register("KERNEL32.DLL", "WriteConsoleA", api_write_console_a);
        dispatcher.register("KERNEL32.DLL", "WriteConsoleW", api_write_console_w);
        dispatcher.register("KERNEL32.DLL", "ExitProcess", api_exit_process);
        dispatcher.register("KERNEL32.DLL", "GetLastError", api_get_last_error);
        dispatcher.register("KERNEL32.DLL", "VirtualAlloc", api_virtual_alloc);
        dispatcher.register("KERNEL32.DLL", "VirtualFree", api_virtual_free);
        dispatcher.register("KERNEL32.DLL", "HeapAlloc", api_heap_alloc);
        dispatcher.register("KERNEL32.DLL", "HeapFree", api_heap_free);
        dispatcher.register("KERNEL32.DLL", "GetModuleHandleA", api_get_module_handle);
        dispatcher.register("KERNEL32.DLL", "GetModuleHandleW", api_get_module_handle);
        dispatcher.register("KERNEL32.DLL", "LoadLibraryA", api_load_library);
        dispatcher.register("KERNEL32.DLL", "LoadLibraryW", api_load_library);
        dispatcher.register("KERNEL32.DLL", "GetCurrentProcessId", api_get_current_process_id);
        dispatcher.register("KERNEL32.DLL", "GetCurrentThreadId", api_get_current_thread_id);
        dispatcher.register("KERNEL32.DLL", "QueryPerformanceCounter", api_query_perf_counter);
        dispatcher.register("KERNEL32.DLL", "QueryPerformanceFrequency", api_query_perf_frequency);
        dispatcher.register("KERNEL32.DLL", "GetSystemTimeAsFileTime", api_get_system_time);

        // MSVCRT.DLL
        dispatcher.register("MSVCRT.DLL", "printf", api_printf);
        dispatcher.register("MSVCRT.DLL", "puts", api_puts);
        dispatcher.register("MSVCRT.DLL", "_cexit", api_cexit);
        dispatcher.register("MSVCRT.DLL", "exit", api_exit_process);
        dispatcher.register("MSVCRT.DLL", "_exit", api_exit_process);
        dispatcher.register("MSVCRT.DLL", "_initterm", api_stub_success);
        dispatcher.register("MSVCRT.DLL", "_initterm_e", api_stub_success);
        dispatcher.register("MSVCRT.DLL", "_configthreadlocale", api_stub_success);
        dispatcher.register("MSVCRT.DLL", "__set_app_type", api_stub_success);
        dispatcher.register("MSVCRT.DLL", "_set_fmode", api_stub_success);
        dispatcher.register("MSVCRT.DLL", "_set_new_mode", api_stub_success);

        // NTDLL.DLL
        dispatcher.register("NTDLL.DLL", "RtlGetLastError", api_get_last_error);
        dispatcher.register("NTDLL.DLL", "RtlAllocateHeap", api_heap_alloc);
        dispatcher.register("NTDLL.DLL", "RtlFreeHeap", api_heap_free);
        dispatcher.register("NTDLL.DLL", "NtQueryInformationProcess", api_stub_success);

        log::info!(
            "Win32 Dispatcher initialized with {} handlers for {} API functions",
            dispatcher.handlers.len(),
            dispatcher.handlers.keys().map(|(_, f)| f.as_str()).collect::<Vec<_>>().len()
        );

        dispatcher
    }

    fn register(&mut self, dll: &str, func: &str, handler: ApiHandler) {
        let key = (dll.to_uppercase(), func.to_string());
        if self.handlers.insert(key, handler).is_some() {
            log::warn!("Overriding handler for {}.{}", dll, func);
        }
    }

    /// Resolve an import — returns a fake function pointer (trace ID + index).
    pub fn resolve(&self, dll: &str, func: &str) -> Option<u64> {
        let key = (dll.to_uppercase(), func.to_string());
        if self.handlers.contains_key(&key) {
            // Return a unique function ID encoded as an address
            let call_id = self.call_count.fetch_add(1, Ordering::SeqCst);
            Some(0xDEAD0000u64 | call_id as u64)
        } else {
            None
        }
    }

    /// Dispatch an API call by function ID.
    pub fn dispatch(&mut self, _func_id: u64, dll: &str, func_name: &str,
                    args: serde_json::Value) -> ApiHandlerResult {
        let api_kind = ApiCallKind::from_name(dll, func_name);

        self.trace.record_api_call(dll, api_kind.clone(), args.clone());

        let key = (dll.to_uppercase(), func_name.to_string());
        let context = ApiHandlerContext {
            process_id: self.process_id,
            exit_code: Arc::clone(&self.exit_code),
            heap: Arc::clone(&self.heap),
        };

        let result = if let Some(&handler) = self.handlers.get(&key) {
            handler(&context, &args)
        } else {
            log::warn!("No handler for {}.{}", dll, func_name);
            ApiHandlerResult {
                return_value: 0,
                last_error: 0x00000005, // ERROR_CALL_NOT_IMPLEMENTED
                output: None,
            }
        };

        self.trace.record_api_return(dll, api_kind, result.return_value, result.last_error);

        result
    }

    /// Get the exit code set by ExitProcess.
    pub fn exit_code(&self) -> i32 {
        self.exit_code.load(Ordering::SeqCst)
    }

    /// Get the trace recorder.
    pub fn trace(&self) -> &TraceRecorder {
        &self.trace
    }
}

impl ApiCallKind {
    /// Parse an API call kind from DLL name and function name.
    pub fn from_name(dll: &str, func: &str) -> Self {
        match func {
            "GetStdHandle" => ApiCallKind::GetStdHandle,
            "WriteConsoleA" => ApiCallKind::WriteConsoleA,
            "WriteConsoleW" => ApiCallKind::WriteConsoleW,
            "ExitProcess" | "exit" | "_exit" => ApiCallKind::ExitProcess,
            "GetLastError" | "RtlGetLastError" => ApiCallKind::GetLastError,
            "VirtualAlloc" => ApiCallKind::VirtualAlloc,
            "VirtualFree" => ApiCallKind::VirtualFree,
            "HeapAlloc" | "RtlAllocateHeap" => ApiCallKind::HeapAlloc,
            "HeapFree" | "RtlFreeHeap" => ApiCallKind::HeapFree,
            "GetModuleHandleA" | "GetModuleHandleW" => ApiCallKind::GetModuleHandleA,
            "LoadLibraryA" | "LoadLibraryW" => ApiCallKind::LoadLibraryA,
            "GetCurrentProcessId" => ApiCallKind::GetCurrentProcessId,
            "GetCurrentThreadId" => ApiCallKind::GetCurrentThreadId,
            "QueryPerformanceCounter" => ApiCallKind::QueryPerformanceCounter,
            "QueryPerformanceFrequency" => ApiCallKind::QueryPerformanceFrequency,
            "GetSystemTimeAsFileTime" => ApiCallKind::GetSystemTimeAsFileTime,
            _ => ApiCallKind::Unknown(func.to_string()),
        }
    }
}

// === API Handler Implementations ===

/// STD_OUTPUT_HANDLE = -11 (0xFFFFFFF5 unsigned)
const STD_OUTPUT_HANDLE: u64 = 0xFFFFFFF5;
/// STD_ERROR_HANDLE = -12 (0xFFFFFFF4 unsigned)
const STD_ERROR_HANDLE: u64 = 0xFFFFFFF4;

fn api_get_std_handle(_ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let handle = args.get("nStdHandle").and_then(|v| v.as_u64()).unwrap_or(STD_OUTPUT_HANDLE);
    match handle {
        STD_OUTPUT_HANDLE => ApiHandlerResult {
            return_value: 1, // stdout fd
            last_error: 0,
            output: None,
        },
        STD_ERROR_HANDLE => ApiHandlerResult {
            return_value: 2, // stderr fd
            last_error: 0,
            output: None,
        },
        STD_INPUT_HANDLE => ApiHandlerResult {
            return_value: 0, // stdin fd
            last_error: 0,
            output: None,
        },
        _ => ApiHandlerResult {
            return_value: 0xFFFFFFFFFFFFFFFF, // INVALID_HANDLE_VALUE
            last_error: 0x00000057, // ERROR_INVALID_PARAMETER
            output: None,
        },
    }
}

const STD_INPUT_HANDLE: u64 = 0xFFFFFFF6;

fn api_write_console_a(_ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let _handle = args.get("hConsoleOutput").and_then(|v| v.as_u64()).unwrap_or(1);
    let buffer = args.get("lpBuffer").and_then(|v| v.as_str()).unwrap_or("");
    let _written = args.get("lpNumberOfCharsWritten").and_then(|v| v.as_u64());

    println!("[CONSOLE] {}", buffer);

    ApiHandlerResult {
        return_value: 1, // TRUE
        last_error: 0,
        output: Some(buffer.to_string()),
    }
}

fn api_write_console_w(_ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let _handle = args.get("hConsoleOutput").and_then(|v| v.as_u64()).unwrap_or(1);
    let buffer = args.get("lpBuffer").and_then(|v| v.as_str()).unwrap_or("");
    let _written = args.get("lpNumberOfCharsWritten").and_then(|v| v.as_u64());

    println!("[CONSOLE] {}", buffer);

    ApiHandlerResult {
        return_value: 1,
        last_error: 0,
        output: Some(buffer.to_string()),
    }
}

fn api_exit_process(ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let code = args.get("uExitCode").and_then(|v| v.as_i64()).unwrap_or(0) as i32;
    ctx.exit_code.store(code, Ordering::SeqCst);
    log::info!("ExitProcess called with code: {}", code);
    ApiHandlerResult {
        return_value: 0,
        last_error: 0,
        output: None,
    }
}

fn api_get_last_error(_ctx: &ApiHandlerContext, _args: &serde_json::Value) -> ApiHandlerResult {
    ApiHandlerResult {
        return_value: 0,
        last_error: 0,
        output: None,
    }
}

fn api_virtual_alloc(ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let size = args.get("dwSize").and_then(|v| v.as_u64()).unwrap_or(4096) as u32;
    let addr = ctx.heap.allocate(size);
    log::debug!("VirtualAlloc: size={}, addr={:#018X}", size, addr);
    ApiHandlerResult {
        return_value: addr,
        last_error: 0,
        output: None,
    }
}

fn api_virtual_free(ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let addr = args.get("lpAddress").and_then(|v| v.as_u64()).unwrap_or(0);
    let freed = ctx.heap.free(addr);
    log::debug!("VirtualFree: addr={:#018X}, freed={}", addr, freed);
    ApiHandlerResult {
        return_value: if freed { 1 } else { 0 },
        last_error: if freed { 0 } else { 0x000001E7 }, // ERROR_INVALID_ADDRESS
        output: None,
    }
}

fn api_heap_alloc(ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let size = args.get("dwBytes").and_then(|v| v.as_u64()).unwrap_or(256) as u32;
    let addr = ctx.heap.allocate(size);
    log::debug!("HeapAlloc: size={}, addr={:#018X}", size, addr);
    ApiHandlerResult {
        return_value: addr,
        last_error: 0,
        output: None,
    }
}

fn api_heap_free(ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let addr = args.get("lpMem").and_then(|v| v.as_u64()).unwrap_or(0);
    let freed = ctx.heap.free(addr);
    ApiHandlerResult {
        return_value: if freed { 1 } else { 0 },
        last_error: if freed { 0 } else { 0x000001E7 },
        output: None,
    }
}

fn api_get_module_handle(_ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let _module = args.get("lpModuleName").and_then(|v| v.as_str()).unwrap_or("");
    // Return a fake module handle
    ApiHandlerResult {
        return_value: 0x00400000,
        last_error: 0,
        output: None,
    }
}

fn api_load_library(_ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let _module = args.get("lpLibFileName").and_then(|v| v.as_str()).unwrap_or("");
    // Return a fake module handle
    ApiHandlerResult {
        return_value: 0x10000000,
        last_error: 0,
        output: None,
    }
}

fn api_get_current_process_id(ctx: &ApiHandlerContext, _args: &serde_json::Value) -> ApiHandlerResult {
    ApiHandlerResult {
        return_value: ctx.process_id as u64,
        last_error: 0,
        output: None,
    }
}

fn api_get_current_thread_id(_ctx: &ApiHandlerContext, _args: &serde_json::Value) -> ApiHandlerResult {
    ApiHandlerResult {
        return_value: 1,
        last_error: 0,
        output: None,
    }
}

fn api_query_perf_counter(_ctx: &ApiHandlerContext, _args: &serde_json::Value) -> ApiHandlerResult {
    use std::time::{SystemTime, UNIX_EPOCH};
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos() as u64;
    ApiHandlerResult {
        return_value: nanos / 100, // Convert to ~100ns units (QPC frequency)
        last_error: 0,
        output: None,
    }
}

fn api_query_perf_frequency(_ctx: &ApiHandlerContext, _args: &serde_json::Value) -> ApiHandlerResult {
    // 10MHz = 10,000,000 counts per second (typical QPC frequency)
    ApiHandlerResult {
        return_value: 10_000_000,
        last_error: 0,
        output: None,
    }
}

fn api_get_system_time(_ctx: &ApiHandlerContext, _args: &serde_json::Value) -> ApiHandlerResult {
    use std::time::{SystemTime, UNIX_EPOCH};
    let duration = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default();
    // FILETIME: 100-nanosecond intervals since January 1, 1601
    // Unix epoch → Windows epoch offset: 11644473600 seconds
    let filetime = ((duration.as_secs() + 11644473600) * 10_000_000) as u64
        + duration.subsec_nanos() as u64 / 100;
    ApiHandlerResult {
        return_value: filetime,
        last_error: 0,
        output: None,
    }
}

fn api_printf(_ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let fmt = args.get("format").and_then(|v| v.as_str()).unwrap_or("");
    println!("[MSVCRT] {}", fmt);
    ApiHandlerResult {
        return_value: fmt.len() as u64,
        last_error: 0,
        output: Some(fmt.to_string()),
    }
}

fn api_puts(_ctx: &ApiHandlerContext, args: &serde_json::Value) -> ApiHandlerResult {
    let s = args.get("str").and_then(|v| v.as_str()).unwrap_or("");
    println!("{}", s);
    ApiHandlerResult {
        return_value: 0, // puts returns non-negative on success
        last_error: 0,
        output: Some(s.to_string()),
    }
}

fn api_cexit(_ctx: &ApiHandlerContext, _args: &serde_json::Value) -> ApiHandlerResult {
    // _cexit calls atexit functions and flushes buffers
    ApiHandlerResult {
        return_value: 0,
        last_error: 0,
        output: None,
    }
}

fn api_stub_success(_ctx: &ApiHandlerContext, _args: &serde_json::Value) -> ApiHandlerResult {
    ApiHandlerResult {
        return_value: 1, // TRUE / success
        last_error: 0,
        output: None,
    }
}
