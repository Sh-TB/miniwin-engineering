//! # AI-Native Windows Compatibility Runtime
//!
//! Main CLI entry point.
//!
//! ## Usage
//! ```bash
//! winrt-ai analyze <pe-file>          # Parse and analyze a PE file
//! winrt-ai run <pe-file>             # Execute and trace
//! winrt-ai replay <trace-dir>         # Replay a trace
//! winrt-ai crash-analyze <crash-dir> # Analyze a crash report
//! winrt-ai dump <pe-file>            # Dump PE structure
//! ```

use clap::{Parser, Subcommand};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "winrt-ai")]
#[command(about = "AI-Native Windows Compatibility Runtime — PE loader, trace, replay, and AI analysis")]
#[command(version)]
struct Cli {
    #[command(subcommand)]
    command: Commands,

    /// Log level: trace, debug, info, warn, error
    #[arg(long, default_value = "info", global = true)]
    log_level: String,

    /// Output directory for traces and reports
    #[arg(long, default_value = "/home/z/my-project/replays", global = true)]
    output_dir: PathBuf,
}

#[derive(Subcommand)]
enum Commands {
    /// Parse and analyze a PE file structure
    Analyze {
        /// Path to the PE file
        pe_file: PathBuf,
    },

    /// Execute a PE file and capture traces
    Run {
        /// Path to the PE file
        pe_file: PathBuf,

        /// Execution backend: wine or simulated
        #[arg(long, default_value = "simulated")]
        backend: String,
    },

    /// Replay a previously recorded trace
    Replay {
        /// Path to the trace directory
        trace_dir: PathBuf,
    },

    /// Analyze a crash report directory
    CrashAnalyze {
        /// Path to the crash directory
        crash_dir: PathBuf,
    },

    /// Dump the full PE structure as JSON
    Dump {
        /// Path to the PE file
        pe_file: PathBuf,
    },

    /// Run the full AI debug loop: execute → trace → replay → analyze
    Loop {
        /// Path to the PE file
        pe_file: PathBuf,

        /// Number of iterations
        #[arg(long, default_value = "1")]
        iterations: u32,
    },
}

fn main() {
    let cli = Cli::parse();

    // Initialize logger
    let log_level = match cli.log_level.as_str() {
        "trace" => log::LevelFilter::Trace,
        "debug" => log::LevelFilter::Debug,
        "info" => log::LevelFilter::Info,
        "warn" => log::LevelFilter::Warn,
        "error" => log::LevelFilter::Error,
        _ => log::LevelFilter::Info,
    };
    env_logger::Builder::from_default_env()
        .filter_level(log_level)
        .format_timestamp(Some(env_logger::TimestampPrecision::Millis))
        .init();

    let result = match cli.command {
        Commands::Analyze { pe_file } => cmd_analyze(&pe_file, &cli.output_dir),
        Commands::Run { pe_file, backend } => cmd_run(&pe_file, &cli.output_dir, &backend),
        Commands::Replay { trace_dir } => cmd_replay(&trace_dir),
        Commands::CrashAnalyze { crash_dir } => cmd_crash_analyze(&crash_dir),
        Commands::Dump { pe_file } => cmd_dump(&pe_file),
        Commands::Loop { pe_file, iterations } => cmd_loop(&pe_file, &cli.output_dir, iterations),
    };

    if let Err(e) = result {
        eprintln!("ERROR: {}", e);
        std::process::exit(1);
    }
}

fn cmd_analyze(pe_path: &PathBuf, output_dir: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    println!("═══ PE Analysis: {} ═══", pe_path.display());

    let pe = winrt_ai::PeFile::from_file(pe_path)?;
    println!("{}", pe.summary());

    println!("\n--- Sections ---");
    for section in &pe.sections {
        println!(
            "  {} VA={:#010X} VS={:#010X} Raw={:#010X} Chars={:#010X} [{}{}{}{}]",
            section.name,
            section.virtual_address,
            section.virtual_size,
            section.size_of_raw_data,
            section.characteristics,
            if section.contains_code() { "C" } else { "-" },
            if section.is_executable() { "X" } else { "-" },
            if section.is_readable() { "R" } else { "-" },
            if section.is_writable() { "W" } else { "-" },
        );
    }

    println!("\n--- Import DLLs ---");
    for dll in pe.get_import_dlls() {
        let funcs: Vec<_> = pe.imports.iter()
            .filter(|i| i.dll_name == dll)
            .map(|i| format!("    {} (thunk RVA {:#010X})", i.name, i.thunk_rva))
            .collect();
        println!("  {}", dll);
        for f in funcs {
            println!("{}", f);
        }
    }

    if !pe.relocations.is_empty() {
        println!("\n--- Relocations ---");
        for block in &pe.relocations {
            println!("  Block at page {:#010X}: {} entries", block.page_rva, block.entries.len());
            for entry in &block.entries {
                println!("    offset={:#06X} type={:?}", entry.offset, entry.relocation_type);
            }
        }
    }

    Ok(())
}

fn cmd_run(pe_path: &PathBuf, output_dir: &PathBuf, backend: &str) -> Result<(), Box<dyn std::error::Error>> {
    println!("═══ Running: {} ═══", pe_path.display());

    let backend_type = match backend.to_lowercase().as_str() {
        "wine" => winrt_ai::BackendType::Wine,
        _ => winrt_ai::BackendType::Simulated,
    };

    let trace_dir = output_dir.join("traces");
    let crash_dir = output_dir.join("crashes");

    let config = winrt_ai::ExecutionConfig {
        backend: backend_type,
        trace_dir,
        crash_dir,
        timeout_seconds: 30,
    };

    let engine = winrt_ai::ExecutionEngine::new(config);
    let mut pe = winrt_ai::PeFile::from_file(pe_path)?;
    let result = engine.execute(&mut pe)?;

    println!("\n═══ Execution Result ═══");
    println!("  Trace dir:    {}", result.trace_dir.display());
    println!("  Exit code:    {:?}", result.exit_code);
    println!("  Crashed:      {}", result.crashed);
    println!("  Time:         {:.1}ms", result.execution_time_ms);
    if !result.console_output.is_empty() {
        println!("  Console output:");
        for line in &result.console_output {
            println!("    {}", line);
        }
    }

    // Auto-analyze the trace
    println!("\n═══ Auto-Analyzing Trace ═══");
    let mut replay = winrt_ai::ReplaySession::load(&result.trace_dir)?;
    let replay_result = replay.analyze();
    let report = winrt_ai::AiAnalyzer::analyze(&replay_result);
    winrt_ai::AiAnalyzer::print_report(&report);

    Ok(())
}

fn cmd_replay(trace_dir: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    println!("═══ Replaying: {} ═══", trace_dir.display());

    let mut session = winrt_ai::ReplaySession::load(trace_dir)?;
    let result = session.analyze();

    println!("\n═══ Replay Result ═══");
    println!("  Trace ID:       {}", result.trace_id);
    println!("  Events replayed: {}", result.events_replayed);
    println!("  API calls:      {}", result.api_calls.len());
    println!("  Exit code:      {:?}", result.exit_code);
    println!("  Crash detected: {}", result.crash_detected);
    if !result.console_output.is_empty() {
        println!("  Console output:");
        for line in &result.console_output {
            println!("    {}", line);
        }
    }

    // Generate regression spec
    let spec = session.generate_regression_spec();
    let spec_path = trace_dir.join("regression_spec.json");
    std::fs::write(&spec_path, serde_json::to_string_pretty(&spec)?)?;
    println!("\n  Regression spec saved to: {}", spec_path.display());

    Ok(())
}

fn cmd_crash_analyze(crash_dir: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    println!("═══ Crash Analysis: {} ═══", crash_dir.display());

    let report_path = crash_dir.join("report.json");
    if !report_path.exists() {
        return Err(format!("No report.json found in {}", crash_dir.display()).into());
    }

    let report_contents = std::fs::read_to_string(&report_path)?;
    let report: serde_json::Value = serde_json::from_str(&report_contents)?;

    println!("  Crash ID:       {}", report["crash_id"]);
    println!("  Timestamp:      {}", report["timestamp"]);
    println!("  PE File:        {}", report["pe_path"]);
    println!("  Exception:      {:#010X}", report["exception_code"].as_u64().unwrap_or(0));
    println!("  Address:        {:#018X}", report["exception_address"].as_u64().unwrap_or(0));
    println!("  Signal:         {:?}", report["signal"]);
    println!("  Events before:  {}", report["events_before_crash"]);
    println!("  Exit code:      {:?}", report["exit_code"]);

    // Check for API trace
    let api_trace_path = crash_dir.join("api.trace");
    if api_trace_path.exists() {
        let api_contents = std::fs::read_to_string(&api_trace_path)?;
        let api_count = api_contents.lines().count();
        println!("  API calls before crash: {}", api_count);
    }

    Ok(())
}

fn cmd_dump(pe_path: &PathBuf) -> Result<(), Box<dyn std::error::Error>> {
    let pe = winrt_ai::PeFile::from_file(pe_path)?;
    let json = serde_json::to_string_pretty(&pe)?;
    println!("{}", json);
    Ok(())
}

fn cmd_loop(pe_path: &PathBuf, output_dir: &PathBuf, iterations: u32) -> Result<(), Box<dyn std::error::Error>> {
    println!("═══ AI Debug Loop: {} iterations ═══", iterations);

    for i in 1..=iterations {
        println!("\n━━━ Iteration {}/{} ━━━", i, iterations);

        // Execute
        let iter_trace_dir = output_dir.join("loop").join(format!("iter-{}", i));
        let iter_crash_dir = output_dir.join("loop-crashes").join(format!("iter-{}", i));

        let config = winrt_ai::ExecutionConfig {
            backend: winrt_ai::BackendType::Simulated,
            trace_dir: iter_trace_dir,
            crash_dir: iter_crash_dir,
            timeout_seconds: 30,
        };

        let engine = winrt_ai::ExecutionEngine::new(config);
        let mut pe = winrt_ai::PeFile::from_file(pe_path)?;
        let result = engine.execute(&mut pe)?;

        println!("  Execution: exit={:?}, crashed={}, time={:.1}ms",
            result.exit_code, result.crashed, result.execution_time_ms);

        // Replay
        let mut replay = winrt_ai::ReplaySession::load(&result.trace_dir)?;
        let replay_result = replay.analyze();

        // Analyze
        let report = winrt_ai::AiAnalyzer::analyze(&replay_result);
        winrt_ai::AiAnalyzer::print_report(&report);

        // Generate and save regression spec
        let spec = replay.generate_regression_spec();
        let spec_path = result.trace_dir.join("regression_spec.json");
        std::fs::write(&spec_path, serde_json::to_string_pretty(&spec)?)?;

        // Regression check (if we have a previous spec)
        if i > 1 {
            let prev_spec_path = output_dir
                .join("loop")
                .join(format!("iter-{}", i - 1))
                .join("regression_spec.json");
            if prev_spec_path.exists() {
                let prev_spec: serde_json::Value =
                    serde_json::from_str(&std::fs::read_to_string(&prev_spec_path)?)?;
                let check = replay.check_regression(&prev_spec);
                if check.passed {
                    println!("  ✅ Regression check: PASSED");
                } else {
                    println!("  ❌ Regression check: FAILED");
                    for mismatch in &check.mismatches {
                        println!("     - {}", mismatch);
                    }
                }
            }
        }
    }

    println!("\n═══ AI Debug Loop Complete ═══");
    Ok(())
}
