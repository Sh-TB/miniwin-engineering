//! # Deterministic Replay System
//!
//! Loads a previously recorded trace and replays it deterministically.
//! Enables:
//! - Exact reproduction of execution sequences
//! - Regression testing: verify that a fix doesn't break existing behavior
//! - AI analysis on identical execution contexts
//! - Crash reproduction with full state

use crate::trace::{TraceEvent, TraceRecorder, TraceCategory, TracePayload, ApiCallKind};
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};

/// Result of a replay session.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReplayResult {
    /// The trace ID being replayed.
    pub trace_id: String,
    /// Total events replayed.
    pub events_replayed: usize,
    /// API calls found.
    pub api_calls: Vec<ReplayApiCall>,
    /// Console output captured.
    pub console_output: Vec<String>,
    /// Exit code (if ExitProcess was called).
    pub exit_code: Option<i32>,
    /// Crash detected.
    pub crash_detected: bool,
    /// Crash details (if any).
    pub crash_details: Option<ReplayCrash>,
    /// Duration of original trace.
    pub trace_duration_ms: Option<f64>,
    /// Analysis notes.
    pub notes: Vec<String>,
}

/// An API call extracted from replay.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReplayApiCall {
    pub seq: u64,
    pub timestamp: DateTime<Utc>,
    pub dll: String,
    pub function: ApiCallKind,
    pub arguments: serde_json::Value,
    pub return_value: u64,
    pub last_error: u32,
}

/// Crash details from replay.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReplayCrash {
    pub seq: u64,
    pub timestamp: DateTime<Utc>,
    pub exception_code: u32,
    pub exception_address: u64,
    pub signal: Option<String>,
    pub registers: Option<serde_json::Value>,
    /// Events leading up to the crash (last N events).
    pub preceding_events: Vec<String>,
}

/// The replay engine.
pub struct ReplaySession {
    trace_dir: PathBuf,
    events: Vec<TraceEvent>,
    notes: Vec<String>,
}

impl ReplaySession {
    /// Load a trace directory for replay.
    pub fn load(trace_dir: &Path) -> std::io::Result<Self> {
        let events = TraceRecorder::load_from_dir(trace_dir)?;
        log::info!("ReplaySession: loaded {} events from {}", events.len(), trace_dir.display());

        // Check for crash report
        let crash_report_path = trace_dir.join("crash_report.json");
        if crash_report_path.exists() {
            log::warn!("Crash report found in trace directory");
        }

        Ok(Self {
            trace_dir: trace_dir.to_path_buf(),
            events,
            notes: Vec::new(),
        })
    }

    /// Run the replay analysis.
    pub fn analyze(&mut self) -> ReplayResult {
        let trace_id = self.trace_dir
            .file_name()
            .map(|n| n.to_string_lossy().to_string())
            .unwrap_or_else(|| "unknown".to_string());

        let mut api_calls = Vec::new();
        let mut console_output = Vec::new();
        let mut exit_code = None;
        let mut crash_detected = false;
        let mut crash_details = None;

        // Pair up API calls with their returns
        let mut pending_calls: HashMap<u64, (String, ApiCallKind, serde_json::Value)> = HashMap::new();

        for event in &self.events {
            match &event.event {
                TracePayload::ApiCallEvent { dll, function, arguments } => {
                    pending_calls.insert(event.seq, (dll.clone(), function.clone(), arguments.clone()));
                }
                TracePayload::ApiReturnEvent { dll, function, return_value, last_error } => {
                    // Find matching call (best effort: use most recent pending)
                    if let Some((_, _call_func, call_args)) = pending_calls.remove(&event.seq.saturating_sub(1)) {
                        let call = ReplayApiCall {
                            seq: event.seq,
                            timestamp: event.timestamp,
                            dll: dll.clone(),
                            function: function.clone(),
                            arguments: call_args.clone(),
                            return_value: *return_value,
                            last_error: *last_error,
                        };
                        api_calls.push(call);

                        // Capture console output
                        if *function == ApiCallKind::WriteConsoleA
                            || *function == ApiCallKind::WriteConsoleW
                            || *function == ApiCallKind::Unknown("printf".to_string())
                            || *function == ApiCallKind::Unknown("puts".to_string())
                        {
                            if let Some(output) = call_args.get("lpBuffer").and_then(|v| v.as_str()) {
                                console_output.push(output.to_string());
                            } else if let Some(output) = call_args.get("format").and_then(|v| v.as_str()) {
                                console_output.push(output.to_string());
                            }
                        }

                        // Capture exit code
                        if *function == ApiCallKind::ExitProcess {
                            if let Some(code) = call_args.get("uExitCode").and_then(|v| v.as_i64()) {
                                exit_code = Some(code as i32);
                            }
                        }
                    }
                }
                TracePayload::CrashEvent { exception_code, exception_address, signal, registers } => {
                    crash_detected = true;
                    crash_details = Some(ReplayCrash {
                        seq: event.seq,
                        timestamp: event.timestamp,
                        exception_code: *exception_code,
                        exception_address: *exception_address,
                        signal: signal.clone(),
                        registers: registers.clone(),
                        preceding_events: self.get_preceding_events(event.seq, 10),
                    });
                }
                _ => {}
            }
        }

        let trace_duration_ms = if self.events.len() >= 2 {
            let start = self.events.first().unwrap().timestamp;
            let end = self.events.last().unwrap().timestamp;
            Some((end - start).num_milliseconds() as f64)
        } else {
            None
        };

        self.notes.push(format!(
            "Replay complete: {} events, {} API calls, crash={}",
            self.events.len(),
            api_calls.len(),
            crash_detected,
        ));

        ReplayResult {
            trace_id,
            events_replayed: self.events.len(),
            api_calls,
            console_output,
            exit_code,
            crash_detected,
            crash_details,
            trace_duration_ms,
            notes: std::mem::take(&mut self.notes),
        }
    }

    /// Get a string summary of events preceding a given sequence number.
    fn get_preceding_events(&self, seq: u64, count: usize) -> Vec<String> {
        let start = seq.saturating_sub(count as u64) as usize;
        let end = seq as usize;
        self.events[start..end.min(self.events.len())]
            .iter()
            .map(|e| format!("[{}] {:?}", e.seq, e.event))
            .collect()
    }

    /// Generate a regression test from this replay.
    /// Returns a JSON object describing expected behavior.
    pub fn generate_regression_spec(&mut self) -> serde_json::Value {
        let result = self.analyze();
        serde_json::json!({
            "trace_id": result.trace_id,
            "expected_api_calls": result.api_calls.len(),
            "expected_exit_code": result.exit_code,
            "expected_console_output": result.console_output,
            "expected_crash": result.crash_detected,
            "version": 1,
        })
    }

    /// Compare this replay against a regression spec.
    pub fn check_regression(&mut self, spec: &serde_json::Value) -> RegressionCheckResult {
        let result = self.analyze();

        let mut mismatches = Vec::new();

        // Check API call count
        if let Some(expected) = spec.get("expected_api_calls").and_then(|v| v.as_i64()) {
            if result.api_calls.len() != expected as usize {
                mismatches.push(format!(
                    "API call count: expected {}, got {}",
                    expected,
                    result.api_calls.len()
                ));
            }
        }

        // Check exit code
        if let Some(expected) = spec.get("expected_exit_code").and_then(|v| v.as_i64()) {
            if result.exit_code != Some(expected as i32) {
                mismatches.push(format!(
                    "Exit code: expected {:?}, got {:?}",
                    expected,
                    result.exit_code
                ));
            }
        }

        // Check crash expectation
        if let Some(expected) = spec.get("expected_crash").and_then(|v| v.as_bool()) {
            if result.crash_detected != expected {
                mismatches.push(format!(
                    "Crash: expected {}, got {}",
                    expected,
                    result.crash_detected
                ));
            }
        }

        RegressionCheckResult {
            passed: mismatches.is_empty(),
            mismatches,
        }
    }
}

/// Result of a regression check.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RegressionCheckResult {
    pub passed: bool,
    pub mismatches: Vec<String>,
}
