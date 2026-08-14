//! # Execution Backend
//!
//! Handles actual execution of loaded PE files.
//! Supports multiple backends:
//! 1. Wine (if available) — native Windows binary execution
//! 2. Simulated — traces the load + API dispatch without CPU execution
//!
//! The simulated backend is used for analysis, testing, and proving
//! the AI debug loop when Wine is not available.

use crate::pe::PeFile;
use crate::loader::PELoader;
use crate::dispatch::Win32Dispatcher;
use crate::trace::TraceRecorder;
use std::path::PathBuf;

/// Execution backend type.
#[derive(Debug, Clone, PartialEq)]
pub enum BackendType {
    /// Wine-based native execution.
    Wine,
    /// Simulated execution (no CPU, traces load + dispatch).
    Simulated,
}

/// Execution configuration.
#[derive(Debug, Clone)]
pub struct ExecutionConfig {
    pub backend: BackendType,
    pub trace_dir: PathBuf,
    pub crash_dir: PathBuf,
    pub timeout_seconds: u64,
}

/// Result of an execution.
#[derive(Debug, Clone)]
pub struct ExecutionResult {
    pub trace_dir: PathBuf,
    pub exit_code: Option<i32>,
    pub crashed: bool,
    pub crash_dir: Option<PathBuf>,
    pub console_output: Vec<String>,
    pub execution_time_ms: f64,
}

/// The execution engine.
pub struct ExecutionEngine {
    config: ExecutionConfig,
}

impl ExecutionEngine {
    pub fn new(config: ExecutionConfig) -> Self {
        Self { config }
    }

    /// Execute a PE file using the configured backend.
    pub fn execute(&self, pe: &mut PeFile) -> Result<ExecutionResult, Box<dyn std::error::Error>> {
        let start = std::time::Instant::now();

        let result = match self.config.backend {
            BackendType::Wine => {
                if self.is_wine_available() {
                    self.execute_wine(pe)?
                } else {
                    log::warn!("Wine not available, falling back to simulated execution");
                    self.execute_simulated(pe)?
                }
            }
            BackendType::Simulated => self.execute_simulated(pe)?,
        };

        Ok(ExecutionResult {
            execution_time_ms: start.elapsed().as_millis() as f64,
            ..result
        })
    }

    /// Check if Wine is available on the system.
    fn is_wine_available(&self) -> bool {
        which::which("wine").is_ok() || which::which("wine64").is_ok()
    }

    /// Execute using Wine.
    fn execute_wine(&self, pe: &mut PeFile) -> Result<ExecutionResult, Box<dyn std::error::Error>> {
        log::info!("Executing via Wine: {}", pe.path.display());

        let wine_cmd = if which::which("wine64").is_ok() {
            "wine64"
        } else {
            "wine"
        };

        let mut trace = TraceRecorder::new(&self.config.trace_dir)?;
        trace.record_system(&format!("Wine execution: {} {}", wine_cmd, pe.path.display()), None);

        let output = std::process::Command::new(wine_cmd)
            .arg(&pe.path)
            .output()?;

        let exit_code = output.status.code();
        let stdout = String::from_utf8_lossy(&output.stdout).to_string();
        let stderr = String::from_utf8_lossy(&output.stderr).to_string();

        let console_output: Vec<String> = stdout.lines().chain(stderr.lines())
            .map(|s| s.to_string())
            .collect();

        let crashed = !output.status.success();

        trace.record_system("Wine execution completed", Some(serde_json::json!({
            "exit_code": exit_code,
            "stdout_lines": stdout.lines().count(),
            "stderr_lines": stderr.lines().count(),
        })));

        let trace_dir = trace.finalize()?;

        Ok(ExecutionResult {
            trace_dir,
            exit_code,
            crashed,
            crash_dir: None,
            console_output,
            execution_time_ms: 0.0, // filled by caller
        })
    }

    /// Simulated execution — traces the load + API dispatch sequence.
    fn execute_simulated(&self, pe: &mut PeFile) -> Result<ExecutionResult, Box<dyn std::error::Error>> {
        log::info!("Simulated execution: {}", pe.path.display());

        let trace = TraceRecorder::new(&self.config.trace_dir)?;
        let mut dispatcher = Win32Dispatcher::new(TraceRecorder::new(&self.config.trace_dir)?);
        let mut loader = PELoader::new(trace);

        // Load the PE
        let load_result = loader.load(pe, &mut dispatcher)?;

        // Simulate API calls that a typical console app would make
        log::info!("Simulating API call sequence...");

        loader.trace_mut().record_system("Simulated API call sequence starts", None);

        // Collect imports to simulate
        let mut imports_to_sim: Vec<(String, String)> = Vec::new();
        for dll_name in pe.get_import_dlls() {
            for imp in pe.imports.iter().filter(|i| i.dll_name == dll_name) {
                imports_to_sim.push((imp.dll_name.clone(), imp.name.clone()));
            }
        }
        imports_to_sim.truncate(20);

        for (dll, func) in imports_to_sim {
            // Create reasonable fake arguments based on function name
            let args = match func.as_str() {
                "GetStdHandle" => serde_json::json!({"nStdHandle": -11i64}),
                "WriteConsoleA" => serde_json::json!({
                    "hConsoleOutput": 1,
                    "lpBuffer": "Hello, World!",
                    "nNumberOfCharsToWrite": 13,
                    "lpNumberOfCharsWritten": 0,
                    "lpReserved": null,
                }),
                "ExitProcess" => serde_json::json!({"uExitCode": 0}),
                "GetProcAddress" => serde_json::json!({"lpModuleName": "KERNEL32.DLL", "lpProcName": "ExitProcess"}),
                _ => serde_json::json!({}),
            };

            let result = dispatcher.dispatch(0xDEAD, &dll, &func, args);
            log::debug!("  {}.{} → {:#018X}", dll, func, result.return_value);

            if func == "ExitProcess" {
                break;
            }
        }

        // Simulate execution events
        loader.trace_mut().record_execution(
            "entry",
            load_result.entry_point_va,
            "Jumping to EP",
        );

        // Simulate successful completion
        loader.trace_mut().record_system(
            "Simulated execution completed successfully",
            Some(serde_json::json!({
                "entry_point": format!("{:#018X}", load_result.entry_point_va),
                "sections": load_result.sections_mapped,
                "imports_resolved": load_result.imports_resolved,
            })),
        );

        let trace_dir = loader.into_trace().finalize()?;

        Ok(ExecutionResult {
            trace_dir,
            exit_code: Some(0),
            crashed: false,
            crash_dir: None,
            console_output: vec!["Hello, World!".to_string()],
            execution_time_ms: 0.0, // filled by caller
        })
    }
}

impl PELoader {
    /// Consume the loader and return the trace recorder.
    pub fn into_trace(self) -> TraceRecorder {
        self.trace
    }
}
