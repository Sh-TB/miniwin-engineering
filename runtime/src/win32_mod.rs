use crate::error::Result;
use crate::mem::MemoryManager;
use crate::trace::{TraceCategory, TraceLevel};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Mutex;

static LAST_ERROR: AtomicU32 = AtomicU32::new(0);

// Console handle constants
const STD_INPUT_HANDLE: u32 = 0xFFFFFFF6;
const STD_OUTPUT_HANDLE: u32 = 0xFFFFFFF5;
const STD_ERROR_HANDLE: u32 = 0xFFFFFFF4;

/// Represents a dispatched Win32 API call
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ApiCall {
    pub module: String,
    pub function: String,
    pub arguments: serde_json::Value,
    pub return_value: u64,
    pub last_error: u32,
}

/// Win32 API dispatcher
pub struct Win32Dispatcher {
    /// Output buffer for console writes
    output_buffer: Mutex<String>,
    /// Captured API calls
    api_log: Mutex<Vec<ApiCall>>,
    /// Exit code (set by ExitProcess)
    exit_code: Mutex<Option<u32>>,
    /// Whether ExitProcess was called
    exited: Mutex<bool>,
    /// Resolved function addresses
    resolved: HashMap<String, u64>,
}

impl Win32Dispatcher {
    pub fn new() -> Self {
        Self {
            output_buffer: Mutex::new(String::new()),
            api_log: Mutex::new(Vec::new()),
            exit_code: Mutex::new(None),
            exited: Mutex::new(false),
            resolved: HashMap::new(),
        }
    }

    /// Resolve all imports and return function name -> resolved address mapping
    pub fn resolve_imports(
        &mut self,
        pe: &crate::pe::PeFile,
        mem: &mut MemoryManager,
    ) -> Result<HashMap<String, u64>> {
        let mut resolved = HashMap::new();
        // We use a simple trampoline approach:
        // Each imported function gets a unique guest address.
        // When execution reaches that address, we intercept it.

        // Allocate a page for our trampoline/code area
        let trampoline_size = 4096;
        let trampoline_guest = mem.heap_alloc(trampoline_size)?;

        let mut func_index = 0u32;
        for import in &pe.imports {
            let dll_lower = import.dll_name.to_lowercase();
            for func in &import.functions {
                let func_name = match func {
                    crate::pe::ImportFunction::ByName(name) => name.clone(),
                    crate::pe::ImportFunction::ByOrdinal(ord) => format!("Ordinal_{}", ord),
                };
                let full_name = format!("{}.{}", dll_lower, func_name);

                // Assign a trampoline slot
                let func_addr = trampoline_guest + (func_index * 16) as u64;
                resolved.insert(full_name.clone(), func_addr);
                func_index += 1;

                trace_event!(TraceCategory::Import, TraceLevel::Trace, "resolve",
                    format!("Resolved {} -> {:#018x}", full_name, func_addr));
            }
        }

        self.resolved = resolved.clone();
        Ok(resolved)
    }

    /// Dispatch a Win32 API call by function name
    pub fn dispatch(
        &self,
        func_name: &str,
        _args: &[u64],
        mem: &MemoryManager,
    ) -> Result<u64> {
        trace_event!(TraceCategory::Import, TraceLevel::Info, "dispatch",
            format!("API call: {} (args: {})", func_name, _args.len()));

        let result = match func_name {
            // kernel32.dll
            "kernel32.getstdhandle" => self.get_std_handle(_args),
            "kernel32.writeconsolea" => self.write_console_a(_args, mem)?,
            "kernel32.writeconsolew" => self.write_console_a(_args, mem)?, // Simplified: treat as ASCII
            "kernel32.writefile" => self.write_file(_args, mem)?,
            "kernel32.exitprocess" => self.exit_process(_args),
            "kernel32.getlasterror" => self.get_last_error(),
            "kernel32.setlasterror" => self.set_last_error(_args),
            "kernel32.virtualalloc" => self.virtual_alloc(_args, mem)?,
            "kernel32.virtualfree" => self.virtual_free(_args, mem)?,
            "kernel32.heapalloc" => self.heap_alloc(_args, mem)?,
            "kernel32.heapfree" => self.heap_free(_args, mem)?,
            "kernel32.heapcreate" => self.heap_create(_args),
            "kernel32.getprocessheap" => self.get_process_heap(),
            "kernel32.getmodulehandlea" => self.get_module_handle_a(_args, mem),
            "kernel32.getmodulehandlew" => self.get_module_handle_a(_args, mem),
            "kernel32.getprocaddress" => self.get_proc_address(_args, mem),
            "kernel32.getsysteminfo" => self.get_system_info(_args, mem)?,
            "kernel32.getcurrentprocessid" => self.get_current_process_id(),
            "kernel32.queryperformancefrequency" => self.query_perf_frequency(_args, mem)?,
            "kernel32.queryperformancecounter" => self.query_perf_counter(_args, mem)?,
            "kernel32.initializecriticalsection" => self.init_critical_section(),
            "kernel32.entercriticalsection" => self.enter_critical_section(),
            "kernel32.leavecriticalsection" => self.leave_critical_section(),
            "kernel32.deletecriticalsection" => self.delete_critical_section(),
            "kernel32.tlsalloc" => self.tls_alloc(),
            "kernel32.tlsfree" => self.tls_free(_args),
            "kernel32.tlsgetvalue" => self.tls_get_value(_args),
            "kernel32.tlssetvalue" => self.tls_set_value(_args),
            "kernel32.flushconsoleinputbuffer" => self.flush_console_input_buffer(),
            "kernel32.setconsoletextattribute" => self.set_console_text_attribute(_args),
            "kernel32.getconsolemode" => self.get_console_mode(),
            "kernel32.setconsolectrlhandler" => self.set_console_ctrl_handler(),
            "kernel32.getstartupinfow" => self.get_startup_info_w(_args, mem)?,
            "kernel32.getcommandlinew" => self.get_command_line_w(_args, mem)?,
            "kernel32.getenvironmentstrings" => self.get_environment_strings(_args, mem)?,
            "kernel32.getenvironmentstringsw" => self.get_environment_strings(_args, mem)?,
            "kernel32.freeenvironmentstrings" => self.free_environment_strings(),
            "kernel32.freeenvironmentstringsw" => self.free_environment_strings(),
            "kernel32.raisexception" => self.raise_exception(),
            "kernel32.unhandledexceptionfilter" => self.unhandled_exception_filter(),
            "kernel32.setunhandledexceptionfilter" => self.set_unhandled_exception_filter(_args),
            "kernel32.isDebuggerPresent" => self.is_debugger_present(),
            "kernel32.outputdebugstringa" => self.output_debug_string_a(_args, mem)?,
            "kernel32.outputdebugstringw" => self.output_debug_string_w(_args, mem)?,
            "kernel32.sethandlecount" => self.set_handle_count(_args),
            "kernel32.get FileType" => self.get_file_type(_args),

            // msvcrt.dll
            "msvcrt.printf" => self.printf(_args, mem)?,
            "msvcrt.puts" => self.puts(_args, mem)?,
            "msvcrt.putchar" => self.putchar(_args),
            "msvcrt._initterm" => self._init_term(),
            "msvcrt._initterm_e" => self._init_term_e(),
            "msvcrt._cinit" => self._c_init(),
            "msvcrt.exit" => self.exit_process(_args),
            "msvcrt._exit" => self.exit_process(_args),
            "msvcrt._amsg_exit" => self._amsg_exit(_args),
            "msvcrt._cexit" => self._c_exit(),
            "msvcrt._XcptFilter" => self._xcpt_filter(),
            "msvcrt.__getmainargs" => self.__getmainargs(),
            "msvcrt._set_fmode" => self._set_fmode(_args),
            "msvcrt._set_new_mode" => self._set_new_mode(_args),
            "msvcrt.configthreadlocale" => self.config_thread_locale(_args),
            "msvcrt._setenvp" => self._setenvp(),
            "msvcrt.__set_app_type" => self.__set_app_type(_args),
            "msvcrt._cexit" => self._c_exit(),
            "msvcrt.terminate" => self.terminate(),
            "msvcrt._CxxThrowException" => self._cxx_throw_exception(),
            "msvcrt.__CxxFrameHandler3" => self._cxx_frame_handler3(),
            "msvcrt.memset" => self.memset_builtin(),
            "msvcrt.memcpy" => self.memcpy_builtin(),
            "msvcrt.memmove" => self.memmove_builtin(),
            "msvcrt._initterm_e" => self._init_term_e(),

            // ntdll.dll
            "ntdll.RtlAllocateHeap" => self.rtl_allocate_heap(_args, mem)?,
            "ntdll.RtlFreeHeap" => self.rtl_free_heap(_args, mem)?,
            "ntdll.RtlInitializeCriticalSection" => self.init_critical_section(),
            "ntdll.RtlEnterCriticalSection" => self.enter_critical_section(),
            "ntdll.RtlLeaveCriticalSection" => self.leave_critical_section(),
            "ntdll.RtlDeleteCriticalSection" => self.delete_critical_section(),
            "ntdll.RtlCaptureContext" => self.rtl_capture_context(),
            "ntdll.RtlLookupFunctionEntry" => self.rtl_lookup_function_entry(),
            "ntdll.RtlVirtualUnwind" => self.rtl_virtual_unwind(),
            "ntdll.RtlAddFunctionTable" => self.rtl_add_function_table(),
            "ntdll.NtCurrentTeb" => self.nt_current_teb(),
            "ntdll.NtQuerySystemTime" => self.nt_query_system_time(_args, mem)?,
            "ntdll.NtQueryPerformanceCounter" => self.nt_query_perf_counter(_args, mem)?,
            "ntdll.NtSetInformationThread" => self.nt_set_information_thread(),
            "ntdll.RtlGetNtVersionNumbers" => self.rtl_get_nt_version_numbers(_args, mem)?,
            "ntdll.RtlCaptureStackBackTrace" => self.rtl_capture_stack_back_trace(),
            "ntdll.LdrSystemDllInitBlock" => self.ldr_system_dll_init_block(),
            "ntdll.LdrGetDllHandle" => self.ldr_get_dll_handle(),
            "ntdll.LdrGetProcedureAddress" => self.ldr_get_procedure_address(),

            // ucrtbase.dll / ucrt.dll
            "ucrtbase.printf" => self.printf(_args, mem)?,
            "ucrtbase.puts" => self.puts(_args, mem)?,
            "ucrtbase._initterm" => self._init_term(),
            "ucrtbase._initterm_e" => self._init_term_e(),
            "ucrtbase._cinit" => self._c_init(),
            "ucrtbase.exit" => self.exit_process(_args),
            "ucrtbase._exit" => self.exit_process(_args),
            "ucrtbase._set_fmode" => self._set_fmode(_args),
            "ucrtbase._set_new_mode" => self._set_new_mode(_args),
            "ucrtbase.configurethreadlocale" => self.configure_thread_locale(_args),
            "ucrtbase.__set_app_type" => self.__set_app_type(_args),
            "ucrtbase._cexit" => self._c_exit(),
            "ucrtbase.terminate" => self.terminate(),
            "ucrtbase._CxxThrowException" => self._cxx_throw_exception(),
            "ucrtbase.__CxxFrameHandler3" => self._cxx_frame_handler3(),
            "ucrtbase.__getmainargs" => self.__getmainargs(),
            "ucrtbase._setenvp" => self._setenvp(),
            "ucrtbase._amsg_exit" => self._amsg_exit(_args),
            "ucrtbase._XcptFilter" => self._xcpt_filter(),
            "ucrtbase.outputdebugstringa" => self.output_debug_string_a(_args, mem)?,
            "ucrtbase.outputdebugstringw" => self.output_debug_string_w(_args, mem)?,
            "ucrtbase.heapalloc" => self.heap_alloc(_args, mem)?,
            "ucrtbase.heapfree" => self.heap_free(_args, mem)?,
            "ucrtbase.heapcreate" => self.heap_create(_args),
            "ucrtbase.getprocessheap" => self.get_process_heap(),

            // VCRUNTIME*.dll
            "vcruntime140.__current_exception" => self.current_exception(),
            "vcruntime140.__current_exception_context" => self.current_exception_context(),
            "vcruntime140.__(ImageBase)" => self.image_base(mem),
            "vcruntime140._init_term" => self._init_term(),
            "vcruntime140._init_term_e" => self._init_term_e(),
            "vcruntime140._CxxThrowException" => self._cxx_throw_exception(),
            "vcruntime140.__CxxFrameHandler3" => self._cxx_frame_handler3(),
            "vcruntime140.memset" => self.memset_builtin(),
            "vcruntime140.memcpy" => self.memcpy_builtin(),

            _ => {
                trace_event!(TraceCategory::Import, TraceLevel::Warn, "dispatch",
                    format!("UNIMPLEMENTED API: {}", func_name));
                // Log and return 0 (success for most APIs)
                LAST_ERROR.store(0, Ordering::SeqCst);
                0
            }
        };

        // Log the API call
        let call = ApiCall {
            module: func_name.split('.').next().unwrap_or("unknown").to_string(),
            function: func_name.split('.').nth(1).unwrap_or(func_name).to_string(),
            arguments: serde_json::json!(_args.to_vec()),
            return_value: result,
            last_error: LAST_ERROR.load(Ordering::SeqCst),
        };
        if let Ok(mut log) = self.api_log.lock() {
            log.push(call);
        }

        trace_event!(TraceCategory::Import, TraceLevel::Trace, "dispatch",
            format!("API {} returned {:#018x}", func_name, result));

        Ok(result)
    }

    /// Check if ExitProcess was called
    pub fn has_exited(&self) -> bool {
        *self.exited.lock().unwrap()
    }

    /// Get exit code
    pub fn get_exit_code(&self) -> Option<u32> {
        *self.exit_code.lock().unwrap()
    }

    /// Get captured output
    pub fn get_output(&self) -> String {
        self.output_buffer.lock().map(|g| g.clone()).unwrap_or_default()
    }

    /// Get API call log
    pub fn get_api_log(&self) -> Vec<ApiCall> {
        self.api_log.lock().map(|g| g.clone()).unwrap_or_default()
    }

    // === Individual API implementations ===

    fn get_std_handle(&self, args: &[u64]) -> u64 {
        let handle = if args.is_empty() { STD_OUTPUT_HANDLE } else { args[0] as u32 };
        // Return pseudo-handles
        match handle {
            STD_INPUT_HANDLE => 0xFFFFFFFC, // -3 in signed, but we keep as unsigned
            STD_OUTPUT_HANDLE => 0xFFFFFFFF, // -1 in signed
            STD_ERROR_HANDLE => 0xFFFFFFFE, // -2 in signed
            _ => 0,
        }
    }

    fn write_console_a(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        // WriteConsoleA(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, lpReserved)
        if args.len() < 2 {
            LAST_ERROR.store(87, Ordering::SeqCst); // ERROR_INVALID_PARAMETER
            return Ok(0);
        }
        let _handle = args[0];
        let buffer_ptr = args[1];
        let num_chars = if args.len() > 2 { args[2] as usize } else { 256 };

        // Read the string from guest memory
        let data = mem.read_guest_memory(buffer_ptr, num_chars)?;
        let s = String::from_utf8_lossy(&data);
        let trimmed = s.trim_end_matches('\0');

        // Output to our buffer and stdout
        print!("{}", trimmed);
        if let Ok(mut buf) = self.output_buffer.lock() {
            buf.push_str(trimmed);
        }

        // Write number of chars written
        if args.len() > 3 && args[3] != 0 {
            mem.write_guest_u32(args[3], num_chars as u32)?;
        }

        LAST_ERROR.store(0, Ordering::SeqCst);
        Ok(1) // TRUE
    }

    fn write_file(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        // Simplified: treat as console output
        self.write_console_a(args, mem)
    }

    fn exit_process(&self, args: &[u64]) -> u64 {
        let code = if args.is_empty() { 0 } else { args[0] as u32 };
        trace_event!(TraceCategory::Execution, TraceLevel::Info, "exit_process",
            format!("ExitProcess called with code {}", code));
        if let Ok(mut ec) = self.exit_code.lock() {
            *ec = Some(code);
        }
        if let Ok(mut ex) = self.exited.lock() {
            *ex = true;
        }
        0
    }

    fn get_last_error(&self) -> u64 {
        LAST_ERROR.load(Ordering::SeqCst) as u64
    }

    fn set_last_error(&self, args: &[u64]) -> u64 {
        if !args.is_empty() {
            LAST_ERROR.store(args[0] as u32, Ordering::SeqCst);
        }
        0
    }

    fn virtual_alloc(&self, args: &[u64], _mem: &MemoryManager) -> Result<u64> {
        // VirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect)
        // Return a non-zero address
        if args.len() < 4 {
            return Ok(0x60000000); // fallback
        }
        let size = args[1] as usize;
        // For simplicity, use the memory manager's heap
        let addr = if size > 0 { 0x60000000 } else { 0 };
        trace_event!(TraceCategory::Import, TraceLevel::Trace, "virtual_alloc",
            format!("VirtualAlloc: addr={:#018x} size={:#010x}", args[0], args[1]));
        LAST_ERROR.store(0, Ordering::SeqCst);
        Ok(addr)
    }

    fn virtual_free(&self, _args: &[u64], _mem: &MemoryManager) -> Result<u64> {
        LAST_ERROR.store(0, Ordering::SeqCst);
        Ok(1) // TRUE
    }

    fn heap_alloc(&self, args: &[u64], _mem: &MemoryManager) -> Result<u64> {
        let size = if args.len() > 1 { args[1] as usize } else { 256 };
        // Allocate from heap - we return a guest address
        // Use a simple allocation scheme
        let addr = 0x70000000 + (self.api_log.lock().unwrap().len() * 0x1000) as u64;
        trace_event!(TraceCategory::Import, TraceLevel::Trace, "heap_alloc",
            format!("HeapAlloc: size={} -> {:#018x}", size, addr));
        LAST_ERROR.store(0, Ordering::SeqCst);
        Ok(addr)
    }

    fn heap_free(&self, _args: &[u64], _mem: &MemoryManager) -> Result<u64> {
        LAST_ERROR.store(0, Ordering::SeqCst);
        Ok(1)
    }

    fn heap_create(&self, _args: &[u64]) -> u64 {
        0x50000000 // Return a heap handle
    }

    fn get_process_heap(&self) -> u64 {
        0x50000000
    }

    fn get_module_handle_a(&self, args: &[u64], _mem: &MemoryManager) -> u64 {
        let _module = if !args.is_empty() && args[0] != 0 {
            // Would read string from memory
            "kernel32"
        } else {
            ""
        };
        // Return the image base of the main module
        // This should be the actual image base
        0x140000000
    }

    fn get_proc_address(&self, args: &[u64], _mem: &MemoryManager) -> u64 {
        // GetProcAddress(hModule, lpProcName)
        if args.len() < 2 {
            return 0;
        }
        let _module = args[0];
        let _name_ptr = args[1];
        trace_event!(TraceCategory::Import, TraceLevel::Trace, "get_proc_address",
            format!("GetProcAddress called"));
        // Return a dummy address
        0x80000000
    }

    fn get_system_info(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        // SYSTEM_INFO structure (48 bytes on x64)
        if args.is_empty() || args[0] == 0 {
            return Ok(0);
        }
        let info_ptr = args[0];
        // Write a minimal SYSTEM_INFO structure
        let mut info = vec![0u8; 48];
        // dwOemId / wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64 (9)
        info[0] = 9;
        info[1] = 0;
        // dwPageSize = 4096
        info[4..8].copy_from_slice(&4096u32.to_le_bytes());
        // lpMinimumApplicationAddress
        info[8..16].copy_from_slice(&0x10000u64.to_le_bytes());
        // lpMaximumApplicationAddress
        info[16..24].copy_from_slice(&0x7FFFFFFFFFFFu64.to_le_bytes());
        // dwNumberOfProcessors = 1
        info[24..28].copy_from_slice(&1u32.to_le_bytes());
        // dwProcessorType
        info[28..32].copy_from_slice(&0x8664u32.to_le_bytes()); // AMD64

        mem.write_guest_memory(info_ptr, &info)?;
        Ok(0)
    }

    fn get_current_process_id(&self) -> u64 {
        1234 // Dummy PID
    }

    fn query_perf_frequency(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        if !args.is_empty() && args[0] != 0 {
            mem.write_guest_u64(args[0], 10_000_000)?; // 10MHz
        }
        Ok(1)
    }

    fn query_perf_counter(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        if !args.is_empty() && args[0] != 0 {
            // Use a simple counter
            mem.write_guest_u64(args[0], 0)?;
        }
        Ok(1)
    }

    fn init_critical_section(&self) -> u64 { 0 }
    fn enter_critical_section(&self) -> u64 { 0 }
    fn leave_critical_section(&self) -> u64 { 0 }
    fn delete_critical_section(&self) -> u64 { 0 }

    fn tls_alloc(&self) -> u64 { 1 } // TLS slot 1
    fn tls_free(&self, _args: &[u64]) -> u64 { 1 }
    fn tls_get_value(&self, _args: &[u64]) -> u64 { 0 }
    fn tls_set_value(&self, _args: &[u64]) -> u64 { 1 }

    fn flush_console_input_buffer(&self) -> u64 { 1 }
    fn set_console_text_attribute(&self, _args: &[u64]) -> u64 { 1 }
    fn get_console_mode(&self) -> u64 { 1 }
    fn set_console_ctrl_handler(&self) -> u64 { 1 }
    fn set_handle_count(&self, _args: &[u64]) -> u64 { 1 }
    fn get_file_type(&self, _args: &[u64]) -> u64 { 2 } // FILE_TYPE_CHAR

    fn get_startup_info_w(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        // Write a minimal STARTUPINFOW (104 bytes)
        if args.is_empty() || args[0] == 0 {
            return Ok(0);
        }
        let info = vec![0u8; 104];
        mem.write_guest_memory(args[0], &info)?;
        Ok(0)
    }

    fn get_command_line_w(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        if args.is_empty() || args[0] == 0 {
            return Ok(0);
        }
        // Return pointer to a wide string "program.exe\0"
        let cmd = "program.exe\0";
        let mut wide_bytes: Vec<u8> = Vec::new();
        for c in cmd.encode_utf16() {
            wide_bytes.extend_from_slice(&c.to_le_bytes());
        }
        let cmd_addr = 0x80000000u64;
        mem.write_guest_memory(cmd_addr, &wide_bytes)?;
        // Write the pointer to the pointer
        mem.write_guest_u64(args[0], cmd_addr)?;
        Ok(0)
    }

    fn get_environment_strings(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        // Return a minimal environment block
        let env = "SystemRoot=C:\\Windows\0\0";
        let env_addr = 0x81000000u64;
        let mut env_bytes = env.as_bytes().to_vec();
        env_bytes.push(0);
        env_bytes.push(0); // double null terminator
        mem.write_guest_memory(env_addr, &env_bytes)?;
        if args.len() > 0 && args[0] != 0 {
            mem.write_guest_u64(args[0], env_addr)?;
        }
        Ok(env_addr)
    }

    fn free_environment_strings(&self) -> u64 { 1 }
    fn raise_exception(&self) -> u64 { 0 }
    fn unhandled_exception_filter(&self) -> u64 { 0 }
    fn set_unhandled_exception_filter(&self, _args: &[u64]) -> u64 { 0 }
    fn is_debugger_present(&self) -> u64 { 0 }

    fn output_debug_string_a(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        if args.is_empty() || args[0] == 0 {
            return Ok(0);
        }
        let data = mem.read_guest_memory(args[0], 256)?;
        let s = String::from_utf8_lossy(&data);
        let trimmed = s.trim_end_matches('\0');
        trace_event!(TraceCategory::Execution, TraceLevel::Debug, "debug_string",
            format!("OutputDebugString: {}", trimmed));
        Ok(0)
    }

    fn output_debug_string_w(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        if args.is_empty() || args[0] == 0 {
            return Ok(0);
        }
        let data = mem.read_guest_memory(args[0], 512)?;
        let s = String::from_utf16_lossy(
            &data.chunks_exact(2)
                .map(|c| u16::from_le_bytes([c[0], c[1]]))
                .collect::<Vec<u16>>()
        );
        trace_event!(TraceCategory::Execution, TraceLevel::Debug, "debug_string",
            format!("OutputDebugStringW: {}", s.trim_end_matches('\0')));
        Ok(0)
    }

    // MSVCRT / UCRT functions
    fn printf(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        if args.is_empty() || args[0] == 0 {
            return Ok(0);
        }
        let data = mem.read_guest_memory(args[0], 256)?;
        let s = String::from_utf8_lossy(&data);
        let trimmed = s.trim_end_matches('\0');
        print!("{}", trimmed);
        if let Ok(mut buf) = self.output_buffer.lock() {
            buf.push_str(trimmed);
        }
        Ok(trimmed.len() as u64)
    }

    fn puts(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        if args.is_empty() || args[0] == 0 {
            return Ok(0);
        }
        let data = mem.read_guest_memory(args[0], 256)?;
        let s = String::from_utf8_lossy(&data);
        let trimmed = s.trim_end_matches('\0');
        println!("{}", trimmed);
        if let Ok(mut buf) = self.output_buffer.lock() {
            buf.push_str(trimmed);
            buf.push('\n');
        }
        Ok(0)
    }

    fn putchar(&self, args: &[u64]) -> u64 {
        if !args.is_empty() {
            print!("{}", args[0] as u8 as char);
        }
        args.get(0).copied().unwrap_or(0)
    }

    fn _init_term(&self) -> u64 { 0 }
    fn _init_term_e(&self) -> u64 { 0 }
    fn _c_init(&self) -> u64 { 0 }
    fn _amsg_exit(&self, _args: &[u64]) -> u64 { 0 }
    fn _c_exit(&self) -> u64 { 0 }
    fn _xcpt_filter(&self) -> u64 { 0 }
    fn __getmainargs(&self) -> u64 { 0 }
    fn _set_fmode(&self, _args: &[u64]) -> u64 { 0 }
    fn _set_new_mode(&self, _args: &[u64]) -> u64 { 0 }
    fn config_thread_locale(&self, _args: &[u64]) -> u64 { 0 }
    fn __set_app_type(&self, _args: &[u64]) -> u64 { 0 }
    fn terminate(&self) -> u64 {
        self.exit_process(&[1]);
        0
    }
    fn _cxx_throw_exception(&self) -> u64 { 0 }
    fn _cxx_frame_handler3(&self) -> u64 { 0 }
    fn memset_builtin(&self) -> u64 { 0 }
    fn memcpy_builtin(&self) -> u64 { 0 }
    fn memmove_builtin(&self) -> u64 { 0 }

    fn rtl_allocate_heap(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        let size = if args.len() > 2 { args[2] as usize } else { 256 };
        let addr = 0x70000000 + (self.api_log.lock().unwrap().len() * 0x1000) as u64;
        if size > 0 && addr > 0x70000000 {
            // Zero the memory
            let zeroed = vec![0u8; size.min(4096)];
            mem.write_guest_memory(addr, &zeroed)?;
        }
        trace_event!(TraceCategory::Import, TraceLevel::Trace, "rtl_alloc_heap",
            format!("RtlAllocateHeap: size={} -> {:#018x}", size, addr));
        LAST_ERROR.store(0, Ordering::SeqCst);
        Ok(addr)
    }

    fn rtl_free_heap(&self, _args: &[u64], _mem: &MemoryManager) -> Result<u64> {
        LAST_ERROR.store(0, Ordering::SeqCst);
        Ok(1)
    }

    fn rtl_capture_context(&self) -> u64 { 0 }
    fn rtl_lookup_function_entry(&self) -> u64 { 0 }
    fn rtl_virtual_unwind(&self) -> u64 { 0 }
    fn rtl_add_function_table(&self) -> u64 { 1 }
    fn nt_current_teb(&self) -> u64 { 0 }
    fn nt_query_system_time(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        if !args.is_empty() && args[0] != 0 {
            // Write FILETIME (100ns intervals since 1601)
            mem.write_guest_u64(args[0], 133_000_000_000_000_000)?;
        }
        Ok(0)
    }
    fn nt_query_perf_counter(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        if !args.is_empty() && args[0] != 0 {
            mem.write_guest_u64(args[0], 0)?;
        }
        Ok(0)
    }
    fn nt_set_information_thread(&self) -> u64 { 0 }
    fn rtl_get_nt_version_numbers(&self, args: &[u64], mem: &MemoryManager) -> Result<u64> {
        // Write version: 10.0.19041
        if args.len() >= 3 {
            if args[0] != 0 { mem.write_guest_u32(args[0], 10)?; }
            if args[1] != 0 { mem.write_guest_u32(args[1], 0)?; }
            if args[2] != 0 { mem.write_guest_u32(args[2], 19041)?; }
        }
        Ok(0)
    }
    fn rtl_capture_stack_back_trace(&self) -> u64 { 0 }
    fn ldr_system_dll_init_block(&self) -> u64 { 0 }
    fn ldr_get_dll_handle(&self) -> u64 { 0 }
    fn ldr_get_procedure_address(&self) -> u64 { 0 }
    fn current_exception(&self) -> u64 { 0 }
    fn current_exception_context(&self) -> u64 { 0 }
    fn image_base(&self, _mem: &MemoryManager) -> u64 { 0x140000000 }
    fn configure_thread_locale(&self, _args: &[u64]) -> u64 { 0 }
    fn _setenvp(&self) -> u64 { 0 }
}
