//! # AI Analysis Engine
//!
//! Analyzes execution traces and crash reports to:
//! - Identify root causes of failures
//! - Suggest compatibility fixes
//! - Generate plugin specifications
//! - Detect patterns across multiple executions

use crate::replay::{ReplayResult, ReplaySession, ReplayCrash};
use crate::trace::{ApiCallKind, TraceCategory};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::Path;

/// An analysis report produced by the AI engine.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AnalysisReport {
    /// The trace being analyzed.
    pub trace_id: String,
    /// Analysis timestamp.
    pub timestamp: String,
    /// Overall verdict.
    pub verdict: AnalysisVerdict,
    /// Root cause analysis (if crash or failure).
    pub root_cause: Option<RootCauseAnalysis>,
    /// Suggestions for compatibility improvements.
    pub suggestions: Vec<CompatibilitySuggestion>,
    /// Plugin generation requests.
    pub plugin_requests: Vec<PluginRequest>,
    /// Summary statistics.
    pub statistics: TraceStatistics,
    /// Raw notes from the analysis.
    pub notes: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum AnalysisVerdict {
    /// Execution completed successfully.
    Success,
    /// Crash detected — analysis identifies likely cause.
    Crash,
    /// Unresolved imports — some APIs not handled.
    IncompleteCoverage,
    /// Unexpected behavior but no crash.
    Suspicious,
    /// Not enough data to analyze.
    InsufficientData,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RootCauseAnalysis {
    /// Likely cause category.
    pub cause_type: RootCauseType,
    /// Human-readable explanation.
    pub explanation: String,
    /// The specific API or operation that failed.
    pub failing_component: Option<String>,
    /// Suggested fix priority (1=highest).
    pub priority: u32,
    /// Confidence level (0.0 to 1.0).
    pub confidence: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum RootCauseType {
    /// An unimplemented Win32 API was called.
    UnimplementedApi,
    /// An API was called with unexpected arguments.
    InvalidArguments,
    /// Memory access violation.
    AccessViolation,
    /// Stack overflow.
    StackOverflow,
    /// Division by zero.
    DivisionByZero,
    /// DLL not found or failed to load.
    MissingDll,
    /// Import resolution failure.
    ImportResolutionFailure,
    /// Unknown / unclassified.
    Unknown,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CompatibilitySuggestion {
    /// Short title.
    pub title: String,
    /// Detailed description.
    pub description: String,
    /// Which component this affects.
    pub affected_dll: String,
    /// Which API function.
    pub affected_function: Option<String>,
    /// Implementation hint for the developer.
    pub implementation_hint: String,
    /// Priority (1=highest).
    pub priority: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginRequest {
    /// Plugin name.
    pub name: String,
    /// What the plugin should do.
    pub purpose: String,
    /// DLL(s) it covers.
    pub target_dlls: Vec<String>,
    /// API functions it needs to handle.
    pub target_functions: Vec<String>,
    /// Priority.
    pub priority: u32,
    /// Estimated complexity (lines of code).
    pub estimated_complexity: String,
}

/// Statistics extracted from a trace.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TraceStatistics {
    pub total_events: usize,
    pub api_calls_total: usize,
    pub api_calls_by_dll: HashMap<String, usize>,
    pub api_calls_by_function: HashMap<String, usize>,
    pub unique_dlls: usize,
    pub unique_functions: usize,
    pub console_output_lines: usize,
    pub duration_ms: Option<f64>,
}

/// The AI analysis engine.
pub struct AiAnalyzer;

impl AiAnalyzer {
    /// Analyze a replay result and produce a report.
    pub fn analyze(replay: &ReplayResult) -> AnalysisReport {
        let timestamp = chrono::Utc::now().to_rfc3339();
        let mut notes = Vec::new();
        let mut suggestions = Vec::new();
        let mut plugin_requests = Vec::new();

        // Build statistics
        let statistics = Self::build_statistics(replay);

        // Determine verdict
        let verdict = if replay.crash_detected {
            notes.push("CRASH detected during execution".to_string());
            AnalysisVerdict::Crash
        } else if replay.exit_code.is_some() && replay.exit_code != Some(0) {
            notes.push(format!("Non-zero exit code: {:?}", replay.exit_code));
            AnalysisVerdict::Suspicious
        } else if replay.exit_code == Some(0) {
            notes.push("Execution completed successfully (exit code 0)".to_string());
            AnalysisVerdict::Success
        } else {
            notes.push("No explicit exit — execution may have been interrupted".to_string());
            AnalysisVerdict::InsufficientData
        };

        // Root cause analysis (if crash)
        let root_cause = if let Some(ref crash) = replay.crash_details {
            Some(Self::analyze_crash(crash, &statistics, &mut notes))
        } else {
            None
        };

        // Generate suggestions based on uncovered APIs
        Self::generate_suggestions(&statistics, &mut suggestions, &mut plugin_requests, &mut notes);

        // Console output analysis
        if !replay.console_output.is_empty() {
            notes.push(format!(
                "Console output captured: {} lines",
                replay.console_output.len()
            ));
        }

        AnalysisReport {
            trace_id: replay.trace_id.clone(),
            timestamp,
            verdict,
            root_cause,
            suggestions,
            plugin_requests,
            statistics,
            notes,
        }
    }

    /// Analyze a crash and determine root cause.
    fn analyze_crash(
        crash: &ReplayCrash,
        stats: &TraceStatistics,
        notes: &mut Vec<String>,
    ) -> RootCauseAnalysis {
        let mut explanation = String::new();
        let mut cause_type = RootCauseType::Unknown;
        let mut failing_component = None;
        let mut confidence = 0.5;

        match crash.exception_code {
            0xC0000005 => {
                cause_type = RootCauseType::AccessViolation;
                explanation = format!(
                    "Access violation at address {:#018X}. This typically indicates \
                     an attempt to read or write to an invalid memory location. \
                     Common causes include: null pointer dereference, accessing \
                     unmapped memory, or incorrect pointer arithmetic.",
                    crash.exception_address
                );
                confidence = 0.8;

                // Check if it happened during an API call
                if !crash.preceding_events.is_empty() {
                    explanation.push_str("\n\nLast events before crash:");
                    for event in crash.preceding_events.iter().take(5) {
                        explanation.push_str(&format!("\n  {}", event));
                    }
                }
            }
            0xC0000094 => {
                cause_type = RootCauseType::DivisionByZero;
                explanation = "Integer division by zero exception.".to_string();
                confidence = 0.95;
            }
            0xC00000FD => {
                cause_type = RootCauseType::StackOverflow;
                explanation = "Stack overflow detected. The program's stack has been exhausted.".to_string();
                confidence = 0.9;
            }
            0xC0000135 => {
                cause_type = RootCauseType::MissingDll;
                explanation = "DLL not found. A required dynamic link library could not be located.".to_string();
                confidence = 0.95;
            }
            code => {
                explanation = format!(
                    "Unhandled exception code {:#010X} at address {:#018X}.",
                    code, crash.exception_address
                );
                confidence = 0.4;
            }
        }

        if let Some(ref signal) = crash.signal {
            explanation.push_str(&format!("\nSignal: {}", signal));
        }

        notes.push(format!(
            "Root cause analysis: {:?} (confidence: {:.0}%)",
            cause_type,
            confidence * 100.0
        ));

        RootCauseAnalysis {
            cause_type,
            explanation,
            failing_component,
            priority: 1,
            confidence,
        }
    }

    /// Generate compatibility suggestions based on trace statistics.
    fn generate_suggestions(
        stats: &TraceStatistics,
        suggestions: &mut Vec<CompatibilitySuggestion>,
        _plugins: &mut Vec<PluginRequest>,
        notes: &mut Vec<String>,
    ) {
        for (dll, count) in &stats.api_calls_by_dll {
            notes.push(format!("DLL {} had {} API calls", dll, count));
        }

        // Check for common patterns that suggest missing functionality
        for (func, count) in &stats.api_calls_by_function {
            if func.contains("Unknown") {
                suggestions.push(CompatibilitySuggestion {
                    title: format!("Implement unknown API: {}", func),
                    description: format!(
                        "The program called '{}' {} times. This function is not currently \
                         handled by the Win32 dispatch layer. Implementing it may improve \
                         compatibility.",
                        func, count
                    ),
                    affected_dll: "Unknown".to_string(),
                    affected_function: Some(func.clone()),
                    implementation_hint: format!(
                        "Add a handler for '{}' in the Win32Dispatcher. \
                         Check MSDN documentation for the expected behavior.",
                        func
                    ),
                    priority: if *count > 10 { 1 } else { 3 },
                });
            }
        }
    }

    /// Build statistics from a replay result.
    fn build_statistics(replay: &ReplayResult) -> TraceStatistics {
        let mut api_calls_by_dll: HashMap<String, usize> = HashMap::new();
        let mut api_calls_by_function: HashMap<String, usize> = HashMap::new();

        for call in &replay.api_calls {
            *api_calls_by_dll.entry(call.dll.clone()).or_insert(0) += 1;
            *api_calls_by_function.entry(call.function.to_string()).or_insert(0) += 1;
        }

        let unique_dlls = api_calls_by_dll.len();
        let unique_functions = api_calls_by_function.len();

        TraceStatistics {
            total_events: replay.events_replayed,
            api_calls_total: replay.api_calls.len(),
            api_calls_by_dll,
            api_calls_by_function,
            unique_dlls,
            unique_functions,
            console_output_lines: replay.console_output.len(),
            duration_ms: replay.trace_duration_ms,
        }
    }

    /// Analyze a trace directory directly (convenience method).
    pub fn analyze_trace_dir(trace_dir: &Path) -> std::io::Result<AnalysisReport> {
        let mut session = ReplaySession::load(trace_dir)?;
        let replay = session.analyze();
        Ok(Self::analyze(&replay))
    }

    /// Print a human-readable analysis report to stdout.
    pub fn print_report(report: &AnalysisReport) {
        println!("═══════════════════════════════════════════════════════");
        println!("  AI ANALYSIS REPORT");
        println!("═══════════════════════════════════════════════════════");
        println!("  Trace ID:    {}", report.trace_id);
        println!("  Timestamp:   {}", report.timestamp);
        println!("  Verdict:     {:?}", report.verdict);
        println!("───────────────────────────────────────────────────────");

        println!("\n📊 STATISTICS:");
        println!("  Total events:       {}", report.statistics.total_events);
        println!("  API calls:          {}", report.statistics.api_calls_total);
        println!("  Unique DLLs:        {}", report.statistics.unique_dlls);
        println!("  Unique functions:   {}", report.statistics.unique_functions);
        println!("  Console output:     {} lines", report.statistics.console_output_lines);
        if let Some(ms) = report.statistics.duration_ms {
            println!("  Duration:           {:.1}ms", ms);
        }

        if !report.statistics.api_calls_by_dll.is_empty() {
            println!("\n📦 API CALLS BY DLL:");
            for (dll, count) in &report.statistics.api_calls_by_dll {
                println!("  {}: {} calls", dll, count);
            }
        }

        if let Some(ref root_cause) = report.root_cause {
            println!("\n🔥 ROOT CAUSE ANALYSIS:");
            println!("  Type:       {:?}", root_cause.cause_type);
            println!("  Priority:   {}", root_cause.priority);
            println!("  Confidence: {:.0}%", root_cause.confidence * 100.0);
            println!("  Explanation:");
            for line in root_cause.explanation.lines() {
                println!("    {}", line);
            }
        }

        if !report.suggestions.is_empty() {
            println!("\n💡 SUGGESTIONS:");
            for (i, sug) in report.suggestions.iter().enumerate() {
                println!("  {}. [P{}] {} (DLL: {})",
                    i + 1, sug.priority, sug.title, sug.affected_dll);
                println!("     {}", sug.description);
            }
        }

        if !report.plugin_requests.is_empty() {
            println!("\n🔧 PLUGIN REQUESTS:");
            for (i, plugin) in report.plugin_requests.iter().enumerate() {
                println!("  {}. {} (priority: {})", i + 1, plugin.name, plugin.priority);
                println!("     Target DLLs: {:?}", plugin.target_dlls);
                println!("     Functions: {:?}", plugin.target_functions);
            }
        }

        println!("\n📝 NOTES:");
        for note in &report.notes {
            println!("  • {}", note);
        }
        println!("═══════════════════════════════════════════════════════");
    }
}
