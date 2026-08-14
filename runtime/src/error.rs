use thiserror::Error;

pub type Result<T> = std::result::Result<T, WinRuntimeError>;

#[derive(Error, Debug)]
pub enum WinRuntimeError {
    #[error("PE parse error: {0}")]
    PeParse(String),

    #[error("Invalid PE signature at offset {offset}: found {found:#010x}, expected {expected:#010x}")]
    PeSignature { offset: u64, found: u32, expected: u32 },

    #[error("Unsupported machine type: {0:#06x}")]
    UnsupportedMachine(u16),

    #[error("Unsupported PE format: {0}")]
    UnsupportedFormat(String),

    #[error("Section not found: {0}")]
    SectionNotFound(String),

    #[error("Import not found: {module}.{function}")]
    ImportNotFound { module: String, function: String },

    #[error("Memory allocation failed: {0}")]
    MemoryAllocation(String),

    #[error("Memory protection error: {0}")]
    MemoryProtection(String),

    #[error("Execution error: {0}")]
    Execution(String),

    #[error("Unsupported syscall: {0:#010x}")]
    UnsupportedSyscall(u32),

    #[error("Invalid address: {address:#018x}")]
    InvalidAddress { address: u64 },

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Goblin/PE parse error: {0}")]
    Goblin(String),

    #[error("Relocation error: {0}")]
    Relocation(String),

    #[error("Export not found: {module}.{symbol}")]
    ExportNotFound { module: String, symbol: String },

    #[error("Overflow error: {0}")]
    Overflow(String),

    #[error("Runtime not initialized")]
    NotInitialized,

    #[error("Slice conversion error: {0}")]
    SliceError(#[from] std::array::TryFromSliceError),

    #[error("Nix/errno error: {0}")]
    NixError(#[from] nix::errno::Errno),
}
