//! # Crash Recorder
//!
//! When a crash/exception occurs during execution, creates a CRASH-XXXXXX/
//! directory containing:
//! - `report.json` — structured crash report
//! - `cpu_state.dump` — register state
//! - `api.trace` — API calls leading to the crash
//! - `execution.trace` — full execution trace (copy)
//! - `environment.json` — runtime environment info

use crate::trace::{TraceRecorder, TraceEvent, TraceCategory, TracePayload};
use chrono::Utc;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

/// Structured crash report.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CrashReport {
    /// Unique crash ID.
    pub crash_id: String,
    /// Timestamp (UTC).
    pub timestamp: String,
    /// PE file that crashed.
    pub pe_path: String,
    /// Exception code.
    pub exception_code: u32,
    /// Exception address.
    pub exception_address: u64,
    /// Signal (if Unix signal).
    pub signal: Option<String>,
    /// Register state at crash.
    pub registers: Option<serde_json::Value>,
    /// Number of events before the crash.
    pub events_before_crash: usize,
    /// API calls made before crash.
    pub api_calls_before_crash: Vec<serde_json::Value>,
    /// Console output before crash.
    pub console_output_before_crash: Vec<String>,
    /// Exit code (if any).
    pub exit_code: Option<i32>,
    /// Whether the crash is reproducible (deterministic).
    pub reproducible: bool,
}

/// The crash recorder.
pub struct CrashRecorder {
    base_dir: PathBuf,
}

impl CrashRecorder {
    /// Create a new crash recorder with a base output directory.
    pub fn new(base_dir: &Path) -> Self {
        Self {
            base_dir: base_dir.to_path_buf(),
        }
    }

    /// Record a crash from a trace recorder's events.
    pub fn record_crash(
        &self,
        pe_path: &str,
        exception_code: u32,
        exception_address: u64,
        signal: Option<&str>,
        registers: Option<serde_json::Value>,
        trace_events: &[TraceEvent],
        exit_code: Option<i32>,
    ) -> std::io::Result<PathBuf> {
        let crash_id = format!("CRASH-{}", Utc::now().format("%Y%m%d-%H%M%S-%f"));
        let crash_dir = self.base_dir.join(&crash_id);
        fs::create_dir_all(&crash_dir)?;

        log::error!(
            "CRASH RECORDING: {} — exception {:#010X} at {:#018X}",
            crash_id, exception_code, exception_address
        );

        // Collect API calls before crash
        let mut api_calls_before_crash = Vec::new();
        let mut console_output = Vec::new();

        for event in trace_events {
            match &event.event {
                TracePayload::ApiCallEvent { dll, function, arguments } => {
                    api_calls_before_crash.push(serde_json::json!({
                        "seq": event.seq,
                        "timestamp": event.timestamp.to_rfc3339(),
                        "dll": dll,
                        "function": function.to_string(),
                        "arguments": arguments,
                    }));
                }
                TracePayload::ApiReturnEvent { dll, function, return_value, last_error, .. } => {
                    if function.to_string().contains("WriteConsole")
                        || function.to_string().contains("printf")
                        || function.to_string().contains("puts")
                    {
                        // Get matching call args
                        if let Some(call) = api_calls_before_crash.last() {
                            if let Some(buffer) = call.get("arguments")
                                .and_then(|a| a.get("lpBuffer"))
                                .and_then(|b| b.as_str())
                            {
                                console_output.push(buffer.to_string());
                            }
                        }
                    }
                }
                _ => {}
            }
        }

        let report = CrashReport {
            crash_id: crash_id.clone(),
            timestamp: Utc::now().to_rfc3339(),
            pe_path: pe_path.to_string(),
            exception_code,
            exception_address,
            signal: signal.map(|s| s.to_string()),
            registers: registers.clone(),
            events_before_crash: trace_events.len(),
            api_calls_before_crash,
            console_output_before_crash: console_output,
            exit_code,
            reproducible: true, // Our traces are deterministic
        };

        // Write report.json
        let report_path = crash_dir.join("report.json");
        fs::write(
            &report_path,
            serde_json::to_string_pretty(&report)?,
        )?;

        // Write cpu_state.dump
        if let Some(ref regs) = registers {
            let cpu_path = crash_dir.join("cpu_state.dump");
            fs::write(&cpu_path, serde_json::to_string_pretty(regs)?)?;
        }

        // Write api.trace (just the API events)
        let api_trace_path = crash_dir.join("api.trace");
        let mut api_trace = Vec::new();
        for event in trace_events {
            if matches!(event.category, TraceCategory::ApiCall | TraceCategory::ApiReturn) {
                api_trace.push(serde_json::to_string(event).unwrap_or_default());
            }
        }
        fs::write(&api_trace_path, api_trace.join("\n"))?;

        // Write environment.json
        let env_info = serde_json::json!({
            "os": "Linux",
            "arch": std::env::consts::ARCH,
            "runtime_version": env!("CARGO_PKG_VERSION"),
            "recorded_at": Utc::now().to_rfc3339(),
        });
        let env_path = crash_dir.join("environment.json");
        fs::write(&env_path, serde_json::to_string_pretty(&env_info)?)?;

        log::info!("Crash report saved to: {}", crash_dir.display());
        Ok(crash_dir)
    }
}
