use chrono::Utc;
use serde::{Deserialize, Serialize};
use std::fs::{self, File, OpenOptions};
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

static TRACE_SEQ: AtomicU64 = AtomicU64::new(1);

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TraceEvent {
    pub seq: u64,
    pub timestamp_ns: u64,
    pub category: TraceCategory,
    pub level: TraceLevel,
    pub component: String,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub details: Option<serde_json::Value>,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub enum TraceCategory {
    PeParse,
    Memory,
    Import,
    Relocation,
    Execution,
    Syscall,
    Crash,
    Replay,
    System,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub enum TraceLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
}

impl std::fmt::Display for TraceCategory {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::PeParse => write!(f, "PE_PARSE"),
            Self::Memory => write!(f, "MEMORY"),
            Self::Import => write!(f, "IMPORT"),
            Self::Relocation => write!(f, "RELOC"),
            Self::Execution => write!(f, "EXEC"),
            Self::Syscall => write!(f, "SYSCALL"),
            Self::Crash => write!(f, "CRASH"),
            Self::Replay => write!(f, "REPLAY"),
            Self::System => write!(f, "SYSTEM"),
        }
    }
}

impl std::fmt::Display for TraceLevel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Trace => write!(f, "TRACE"),
            Self::Debug => write!(f, "DEBUG"),
            Self::Info => write!(f, "INFO"),
            Self::Warn => write!(f, "WARN"),
            Self::Error => write!(f, "ERROR"),
        }
    }
}

pub struct TraceSystem {
    events: Mutex<Vec<TraceEvent>>,
    writer: Mutex<Option<BufWriter<File>>>,
    log_dir: PathBuf,
    enabled: bool,
}

impl TraceSystem {
    pub fn new(log_dir: &Path) -> anyhow::Result<Self> {
        fs::create_dir_all(log_dir)?;
        let trace_file = log_dir.join("execution.trace");
        let file = OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(&trace_file)?;
        let writer = BufWriter::new(file);

        Ok(Self {
            events: Mutex::new(Vec::new()),
            writer: Mutex::new(Some(writer)),
            log_dir: log_dir.to_path_buf(),
            enabled: true,
        })
    }

    pub fn emit(&self, category: TraceCategory, level: TraceLevel, component: &str, message: impl Into<String>) {
        if !self.enabled {
            return;
        }
        let message = message.into();
        let event = TraceEvent {
            seq: TRACE_SEQ.fetch_add(1, Ordering::Relaxed),
            timestamp_ns: Utc::now().timestamp_nanos_opt().unwrap_or(0) as u64,
            category,
            level,
            component: component.to_string(),
            message: message.to_string(),
            details: None,
        };
        self.push_event(event);
    }

    pub fn emit_with_details(
        &self,
        category: TraceCategory,
        level: TraceLevel,
        component: &str,
        message: impl Into<String>,
        details: serde_json::Value,
    ) {
        if !self.enabled {
            return;
        }
        let message = message.into();
        let event = TraceEvent {
            seq: TRACE_SEQ.fetch_add(1, Ordering::Relaxed),
            timestamp_ns: Utc::now().timestamp_nanos_opt().unwrap_or(0) as u64,
            category,
            level,
            component: component.to_string(),
            message: message.to_string(),
            details: Some(details),
        };
        self.push_event(event);
    }

    fn push_event(&self, event: TraceEvent) {
        // Write to file
        if let Ok(mut writer_guard) = self.writer.lock() {
            if let Some(ref mut writer) = *writer_guard {
                if let Ok(line) = serde_json::to_string(&event) {
                    let _ = writeln!(writer, "{}", line);
                }
            }
        }
        // Keep in memory (bounded)
        if let Ok(mut events_guard) = self.events.lock() {
            if events_guard.len() < 100_000 {
                events_guard.push(event.clone());
            }
        }
        // Print to console for important events
        match event.level {
            TraceLevel::Error | TraceLevel::Warn => {
                eprintln!("[{}] [{}] [{}] {}", event.category, event.level, event.component, event.message);
            }
            _ => {
                println!("[{}] [{}] [{}] {}", event.category, event.level, event.component, event.message);
            }
        }
    }

    pub fn flush(&self) {
        if let Ok(mut writer_guard) = self.writer.lock() {
            if let Some(ref mut writer) = *writer_guard {
                let _ = writer.flush();
            }
        }
    }

    pub fn get_events(&self) -> Vec<TraceEvent> {
        self.events.lock().map(|g| g.clone()).unwrap_or_default()
    }

    pub fn save_crash_dump(&self, crash_dir: &Path, reason: &str, details: serde_json::Value) -> anyhow::Result<()> {
        fs::create_dir_all(crash_dir)?;
        let report_path = crash_dir.join("report.json");
        let report = serde_json::json!({
            "reason": reason,
            "timestamp": Utc::now().to_rfc3339(),
            "details": details,
            "trace_events_count": self.events.lock().map(|g| g.len()).unwrap_or(0),
        });
        let file = File::create(&report_path)?;
        serde_json::to_writer_pretty(file, &report)?;

        let trace_path = crash_dir.join("execution.trace");
        let events = self.get_events();
        let file = File::create(&trace_path)?;
        let writer = BufWriter::new(file);
        serde_json::to_writer(writer, &events)?;

        Ok(())
    }

    pub fn log_dir(&self) -> &Path {
        &self.log_dir
    }
}

/// Global trace system reference
static mut GLOBAL_TRACE: Option<*const TraceSystem> = None;

pub fn init_global_trace(log_dir: &Path) -> anyhow::Result<()> {
    let trace = TraceSystem::new(log_dir)?;
    unsafe {
        GLOBAL_TRACE = Some(Box::into_raw(Box::new(trace)));
    }
    Ok(())
}

pub fn get_trace() -> Option<&'static TraceSystem> {
    unsafe { GLOBAL_TRACE.map(|p| &*p) }
}

/// Macro for convenient tracing
#[macro_export]
macro_rules! trace_event {
    ($cat:expr, $level:expr, $comp:expr, $msg:expr) => {
        if let Some(tracer) = $crate::trace::get_trace() {
            tracer.emit($cat, $level, $comp, $msg);
        }
    };
    ($cat:expr, $level:expr, $comp:expr, $msg:expr, $details:expr) => {
        if let Some(tracer) = $crate::trace::get_trace() {
            tracer.emit_with_details($cat, $level, $comp, $msg, $details);
        }
    };
}
