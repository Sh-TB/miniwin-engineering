/// # AI-Native Windows Compatibility Runtime
///
/// Core library for parsing PE files, tracing execution,
/// recording crashes, and enabling deterministic replay.
///
/// ## Architecture
/// ```text
/// EXE ──► PE Parser ──► Loader ──► Execution Backend (Wine/Native)
///   │                                    │
///   └──► Trace System ◄─────────────────┘
///          │
///          ▼
///     Crash Recorder
///          │
///          ▼
///   Deterministic Replay
///          │
///          ▼
///    AI Analysis Engine
///          │
///          ▼
///   Compatibility Plugins
/// ```

pub mod pe;
pub mod loader;
pub mod trace;
pub mod replay;
pub mod dispatch;
pub mod analysis;
pub mod execution;
pub mod crash_recorder;

pub use pe::{PeFile, PeParseError};
pub use loader::{LoadResult, PELoader};
pub use trace::{TraceEvent, TraceRecorder, ApiCallKind};
pub use replay::{ReplaySession, ReplayResult};
pub use dispatch::{Win32Dispatcher, ApiHandlerResult};
pub use analysis::{AiAnalyzer, AnalysisReport};
pub use execution::{ExecutionEngine, ExecutionConfig, BackendType, ExecutionResult};
pub use crash_recorder::CrashRecorder;
