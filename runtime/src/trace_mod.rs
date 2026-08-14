//! # Trace System
//!
//! Observes and records all runtime activity:
//! - PE parsing events
//! - Memory loading / mapping events
//! - API call interception (Win32 → dispatch)
//! - Execution flow (entry point, TLS callbacks)
//! - Crash events
//!
//! All traces are structured JSON for deterministic replay and AI analysis.

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::fs;
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};

/// Unique trace session identifier.
pub type TraceId = String;

/// A single trace event — the atomic unit of observation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TraceEvent {
    /// Monotonically increasing event sequence number.
    pub seq: u64,
    /// Timestamp (UTC, microsecond precision).
    pub timestamp: DateTime<Utc>,
    /// Event category.
    pub category: TraceCategory,
    /// Event payload.
    pub event: TracePayload,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum TraceCategory {
    /// PE file parsing
    PeParse,
    /// Memory loading / mapping
    MemoryLoad,
    /// Win32 API call (before dispatch)
    ApiCall,
    /// Win32 API return (after dispatch)
    ApiReturn,
    /// Execution entry / exit
    Execution,
    /// Crash / exception
    Crash,
    /// System / infrastructure
    System,
}

/// API call kinds that the dispatch layer recognizes.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum ApiCallKind {
    GetStdHandle,
    WriteConsoleA,
    WriteConsoleW,
    ExitProcess,
    GetLastError,
    VirtualAlloc,
    VirtualFree,
    HeapAlloc,
    HeapFree,
    GetModuleHandleA,
    GetModuleHandleW,
    LoadLibraryA,
    LoadLibraryW,
    GetCurrentProcessId,
    GetCurrentThreadId,
    QueryPerformanceCounter,
    QueryPerformanceFrequency,
    GetSystemTimeAsFileTime,
    RtlGetLastError,
    NtQueryInformationProcess,
    Unknown(String),
}

impl std::fmt::Display for ApiCallKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ApiCallKind::Unknown(s) => write!(f, "{}", s),
            _ => write!(f, "{:?}", self),
        }
    }
}

/// Trace event payload — varies by category.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum TracePayload {
    PeParseEvent {
        path: String,
        machine: String,
        subsystem: String,
        entry_point: u32,
        image_base: u64,
        sections: usize,
        imports: usize,
        relocations: usize,
    },
    MemoryLoadEvent {
        section_name: String,
        rva: u32,
        file_offset: u32,
        size: u32,
        permissions: String,
    },
    RelocationEvent {
        page_rva: u32,
        relocation_type: String,
        offset: u32,
        old_value: u64,
        new_value: u64,
    },
    ApiCallEvent {
        dll: String,
        function: ApiCallKind,
        arguments: serde_json::Value,
    },
    ApiReturnEvent {
        dll: String,
        function: ApiCallKind,
        return_value: u64,
        last_error: u32,
    },
    ExecutionEvent {
        phase: String,
        address: u64,
        detail: String,
    },
    CrashEvent {
        exception_code: u32,
        exception_address: u64,
        signal: Option<String>,
        registers: Option<serde_json::Value>,
    },
    SystemEvent {
        message: String,
        detail: Option<serde_json::Value>,
    },
}

/// The trace recorder — accumulates events and writes to disk.
pub struct TraceRecorder {
    trace_id: TraceId,
    events: Vec<TraceEvent>,
    output_dir: PathBuf,
    seq_counter: u64,
    writer: Option<BufWriter<fs::File>>,
    /// Whether to also keep events in memory.
    in_memory: bool,
}

impl TraceRecorder {
    /// Create a new trace recorder with a unique ID.
    pub fn new(output_dir: &Path) -> std::io::Result<Self> {
        let trace_id = format!(
            "{}",
            chrono::Utc::now().format("%Y%m%d-%H%M%S-%f")
        );
        let trace_dir = output_dir.join(&trace_id);
        fs::create_dir_all(&trace_dir)?;

        let trace_file = trace_dir.join("execution.trace");
        let file = fs::File::create(&trace_file)?;
        let writer = BufWriter::new(file);

        log::info!("Trace session started: {} (dir: {})", trace_id, trace_dir.display());

        Ok(Self {
            trace_id,
            events: Vec::new(),
            output_dir: trace_dir,
            seq_counter: 0,
            writer: Some(writer),
            in_memory: true,
        })
    }

    /// Get the trace session ID.
    pub fn trace_id(&self) -> &str {
        &self.trace_id
    }

    /// Get the trace directory path.
    pub fn trace_dir(&self) -> &Path {
        &self.output_dir
    }

    /// Record a trace event.
    pub fn record(&mut self, category: TraceCategory, payload: TracePayload) {
        let event = TraceEvent {
            seq: self.seq_counter,
            timestamp: Utc::now(),
            category,
            event: payload,
        };
        self.seq_counter += 1;

        // Write to file immediately for crash resilience
        if let Some(ref mut writer) = self.writer {
            if let Ok(json_line) = serde_json::to_string(&event) {
                let _ = writeln!(writer, "{}", json_line);
                let _ = writer.flush();
            }
        }

        if self.in_memory {
            self.events.push(event);
        }
    }

    /// Record a PE parse completion event.
    pub fn record_pe_parse(&mut self, path: &str, machine: &str, subsystem: &str,
                           entry_point: u32, image_base: u64, sections: usize,
                           imports: usize, relocations: usize) {
        self.record(TraceCategory::PeParse, TracePayload::PeParseEvent {
            path: path.to_string(),
            machine: machine.to_string(),
            subsystem: subsystem.to_string(),
            entry_point,
            image_base,
            sections,
            imports,
            relocations,
        });
    }

    /// Record a memory load event.
    pub fn record_memory_load(&mut self, section: &str, rva: u32, file_off: u32,
                             size: u32, perms: &str) {
        self.record(TraceCategory::MemoryLoad, TracePayload::MemoryLoadEvent {
            section_name: section.to_string(),
            rva,
            file_offset: file_off,
            size,
            permissions: perms.to_string(),
        });
    }

    /// Record a relocation event.
    pub fn record_relocation(&mut self, page_rva: u32, reloc_type: &str,
                             offset: u32, old_val: u64, new_val: u64) {
        self.record(TraceCategory::MemoryLoad, TracePayload::RelocationEvent {
            page_rva,
            relocation_type: reloc_type.to_string(),
            offset,
            old_value: old_val,
            new_value: new_val,
        });
    }

    /// Record an API call.
    pub fn record_api_call(&mut self, dll: &str, func: ApiCallKind, args: serde_json::Value) {
        self.record(TraceCategory::ApiCall, TracePayload::ApiCallEvent {
            dll: dll.to_string(),
            function: func,
            arguments: args,
        });
    }

    /// Record an API return.
    pub fn record_api_return(&mut self, dll: &str, func: ApiCallKind, ret_val: u64, last_error: u32) {
        self.record(TraceCategory::ApiReturn, TracePayload::ApiReturnEvent {
            dll: dll.to_string(),
            function: func,
            return_value: ret_val,
            last_error,
        });
    }

    /// Record an execution event.
    pub fn record_execution(&mut self, phase: &str, address: u64, detail: &str) {
        self.record(TraceCategory::Execution, TracePayload::ExecutionEvent {
            phase: phase.to_string(),
            address,
            detail: detail.to_string(),
        });
    }

    /// Record a crash event.
    pub fn record_crash(&mut self, exception_code: u32, exception_addr: u64,
                        signal: Option<&str>, registers: Option<serde_json::Value>) {
        self.record(TraceCategory::Crash, TracePayload::CrashEvent {
            exception_code,
            exception_address: exception_addr,
            signal: signal.map(|s| s.to_string()),
            registers,
        });
    }

    /// Record a system event.
    pub fn record_system(&mut self, message: &str, detail: Option<serde_json::Value>) {
        self.record(TraceCategory::System, TracePayload::SystemEvent {
            message: message.to_string(),
            detail,
        });
    }

    /// Flush the writer to disk.
    pub fn flush(&mut self) -> std::io::Result<()> {
        if let Some(ref mut writer) = self.writer {
            writer.flush()?;
        }
        Ok(())
    }

    /// Finalize the trace — write summary and close files.
    pub fn finalize(mut self) -> std::io::Result<PathBuf> {
        self.flush()?;

        // Write summary
        let summary = serde_json::json!({
            "trace_id": self.trace_id,
            "total_events": self.seq_counter,
            "categories": {
                "pe_parse": self.events.iter().filter(|e| e.category == TraceCategory::PeParse).count(),
                "memory_load": self.events.iter().filter(|e| e.category == TraceCategory::MemoryLoad).count(),
                "api_call": self.events.iter().filter(|e| e.category == TraceCategory::ApiCall).count(),
                "api_return": self.events.iter().filter(|e| e.category == TraceCategory::ApiReturn).count(),
                "execution": self.events.iter().filter(|e| e.category == TraceCategory::Execution).count(),
                "crash": self.events.iter().filter(|e| e.category == TraceCategory::Crash).count(),
                "system": self.events.iter().filter(|e| e.category == TraceCategory::System).count(),
            },
            "start_time": self.events.first().map(|e| e.timestamp.to_rfc3339()),
            "end_time": self.events.last().map(|e| e.timestamp.to_rfc3339()),
        });

        let summary_path = self.output_dir.join("summary.json");
        let mut f = fs::File::create(&summary_path)?;
        writeln!(f, "{}", serde_json::to_string_pretty(&summary)?)?;

        // Drop the writer to close the file
        drop(self.writer);

        Ok(self.output_dir.clone())
    }

    /// Get all recorded events (only if in_memory is true).
    pub fn events(&self) -> &[TraceEvent] {
        &self.events
    }

    /// Load a trace from a directory for replay.
    pub fn load_from_dir(trace_dir: &Path) -> std::io::Result<Vec<TraceEvent>> {
        let trace_file = trace_dir.join("execution.trace");
        let contents = fs::read_to_string(&trace_file)?;
        let mut events = Vec::new();
        for line in contents.lines() {
            if let Ok(event) = serde_json::from_str::<TraceEvent>(line) {
                events.push(event);
            }
        }
        Ok(events)
    }
}
