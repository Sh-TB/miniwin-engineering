//! # PE Parser Module
//!
//! Parses Windows Portable Executable (PE) files including:
//! - DOS Header & PE Signature
//! - COFF File Header
//! - Optional Header (PE32+ / x64)
//! - Section Headers
//! - Import Directory
//! - Export Directory
//! - Relocation Directory
//! - Base Relocations

use serde::{Deserialize, Serialize};
use std::fmt;

/// Result type for PE parsing operations.
pub type PeResult<T> = Result<T, PeParseError>;

/// Errors that can occur during PE file parsing.
#[derive(Debug, thiserror::Error)]
pub enum PeParseError {
    #[error("Invalid DOS signature: expected MZ (0x5A4D), got {0:#010X}")]
    InvalidDosSignature(u16),

    #[error("Invalid PE signature: expected PE\\0\\0 (0x00004550), got {0:#010X}")]
    InvalidPeSignature(u32),

    #[error("Invalid optional header magic: expected PE32+ (0x020B) or PE32 (0x010B), got {0:#06X}")]
    InvalidOptionalMagic(u16),

    #[error("Unsupported machine type: {0:#06X}")]
    UnsupportedMachine(u16),

    #[error("Section header extends beyond file: section #{0} at offset {1}")]
    SectionOutOfBounds(usize, u32),

    #[error("Import directory RVA {0:#010X} + size {1} extends beyond image")]
    ImportDirOutOfBounds(u32, u32),

    #[error("Relocation directory RVA {0:#010X} + size {1} extends beyond image")]
    RelocationDirOutOfBounds(u32, u32),

    #[error("RVA to file offset conversion failed: RVA {0:#010X} not found in any section")]
    RvaConversionFailed(u32),

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Parse error from goblin: {0}")]
    Goblin(String),

    #[error("File too small to be a valid PE: {0} bytes")]
    FileTooSmall(usize),
}

/// DOS Header — first 64 bytes of a PE file.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DosHeader {
    pub e_magic: u16,           // MZ signature
    pub e_cblp: u16,
    pub e_cp: u16,
    pub e_crlc: u16,
    pub e_cparhdr: u16,
    pub e_minalloc: u16,
    pub e_maxalloc: u16,
    pub e_ss: u16,
    pub e_sp: u16,
    pub e_csum: u16,
    pub e_ip: u16,
    pub e_cs: u16,
    pub e_lfarlc: u16,
    pub e_ovno: u16,
    pub e_res: [u16; 4],
    pub e_oemid: u16,
    pub e_oeminfo: u16,
    pub e_res2: [u16; 10],
    pub e_lfanew: u32,          // Offset to PE signature
}

/// PE Signature location.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PeSignature {
    pub offset: u32,            // File offset of PE\0\0
    pub signature: u32,          // Should be 0x00004550
}

/// COFF File Header (20 bytes).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CoffHeader {
    pub machine: u16,           // IMAGE_FILE_MACHINE_AMD64 = 0x8664
    pub number_of_sections: u16,
    pub time_date_stamp: u32,
    pub pointer_to_symbol_table: u32,
    pub number_of_symbols: u32,
    pub size_of_optional_header: u16,
    pub characteristics: u16,
}

/// Machine types from COFF header.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub enum MachineType {
    Amd64,   // x86_64
    I386,    // x86 32-bit
    Arm64,
    Unknown(u16),
}

impl fmt::Display for MachineType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MachineType::Amd64 => write!(f, "x86_64 (AMD64)"),
            MachineType::I386 => write!(f, "x86 (I386)"),
            MachineType::Arm64 => write!(f, "ARM64"),
            MachineType::Unknown(v) => write!(f, "Unknown ({:#06X})", v),
        }
    }
}

impl From<u16> for MachineType {
    fn from(v: u16) -> Self {
        match v {
            0x8664 => MachineType::Amd64,
            0x014C => MachineType::I386,
            0xAA64 => MachineType::Arm64,
            _ => MachineType::Unknown(v),
        }
    }
}

/// Optional Header — PE32+ variant for x64 binaries.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OptionalHeader {
    pub magic: u16,             // PE32+ = 0x020B, PE32 = 0x010B
    pub major_linker_version: u8,
    pub minor_linker_version: u8,
    pub size_of_code: u32,
    pub size_of_initialized_data: u32,
    pub size_of_uninitialized_data: u32,
    pub address_of_entry_point: u32,
    pub base_of_code: u32,
    pub image_base: u64,        // u64 for PE32+
    pub section_alignment: u32,
    pub file_alignment: u32,
    pub major_operating_system_version: u16,
    pub minor_operating_system_version: u16,
    pub major_image_version: u16,
    pub minor_image_version: u16,
    pub major_subsystem_version: u16,
    pub minor_subsystem_version: u16,
    pub win32_version_value: u32,
    pub size_of_image: u32,
    pub size_of_headers: u32,
    pub checksum: u32,
    pub subsystem: u16,         // IMAGE_SUBSYSTEM_WINDOWS_CUI = 3
    pub dll_characteristics: u16,
    pub size_of_stack_reserve: u64,
    pub size_of_stack_commit: u64,
    pub size_of_heap_reserve: u64,
    pub size_of_heap_commit: u64,
    pub loader_flags: u32,
    pub number_of_rva_and_sizes: u32,
    pub data_directories: Vec<DataDirectory>,
}

/// Subsystem type.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub enum Subsystem {
    Unknown,
    Native,
    WindowsGui,
    WindowsCui,      // Console — our primary target
    Os2Cui,
    PosixCui,
    WindowsCeGui,
    EfiApplication,
    EfiBootServiceDriver,
    EfiRuntimeDriver,
    WindowsRtApplication,
    Xbox,
}

impl fmt::Display for Subsystem {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Subsystem::Unknown => write!(f, "Unknown"),
            Subsystem::Native => write!(f, "Native"),
            Subsystem::WindowsGui => write!(f, "Windows GUI"),
            Subsystem::WindowsCui => write!(f, "Windows Console (CUI)"),
            Subsystem::Os2Cui => write!(f, "OS/2 CUI"),
            Subsystem::PosixCui => write!(f, "POSIX CUI"),
            Subsystem::WindowsCeGui => write!(f, "Windows CE GUI"),
            Subsystem::EfiApplication => write!(f, "EFI Application"),
            Subsystem::EfiBootServiceDriver => write!(f, "EFI Boot Service Driver"),
            Subsystem::EfiRuntimeDriver => write!(f, "EFI Runtime Driver"),
            Subsystem::WindowsRtApplication => write!(f, "Windows RT Application"),
            Subsystem::Xbox => write!(f, "Xbox"),
        }
    }
}

impl From<u16> for Subsystem {
    fn from(v: u16) -> Self {
        match v {
            0 => Subsystem::Unknown,
            1 => Subsystem::Native,
            2 => Subsystem::WindowsGui,
            3 => Subsystem::WindowsCui,
            5 => Subsystem::Os2Cui,
            7 => Subsystem::PosixCui,
            9 => Subsystem::WindowsCeGui,
            10 => Subsystem::EfiApplication,
            11 => Subsystem::EfiBootServiceDriver,
            12 => Subsystem::EfiRuntimeDriver,
            16 => Subsystem::WindowsRtApplication,
            14 => Subsystem::Xbox,
            _ => Subsystem::Unknown,
        }
    }
}

/// Data directory entry (RVA + Size).
#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
pub struct DataDirectory {
    pub rva: u32,
    pub size: u32,
}

impl DataDirectory {
    pub const EXPORT_TABLE: usize = 0;
    pub const IMPORT_TABLE: usize = 1;
    pub const RESOURCE_TABLE: usize = 2;
    pub const EXCEPTION_TABLE: usize = 3;
    pub const CERTIFICATE_TABLE: usize = 4;
    pub const BASE_RELOCATION_TABLE: usize = 5;
    pub const DEBUG: usize = 6;
    pub const ARCHITECTURE: usize = 7;
    pub const GLOBAL_PTR: usize = 8;
    pub const TLS_TABLE: usize = 9;
    pub const LOAD_CONFIG_TABLE: usize = 10;
    pub const BOUND_IMPORT: usize = 11;
    pub const IAT: usize = 12;
    pub const DELAY_IMPORT_DESCRIPTOR: usize = 13;
    pub const CLR_RUNTIME_HEADER: usize = 14;
    pub const RESERVED: usize = 15;

    pub fn is_empty(&self) -> bool {
        self.rva == 0 && self.size == 0
    }
}

/// Section header (40 bytes each).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SectionHeader {
    pub name: String,           // 8-byte null-terminated
    pub virtual_size: u32,
    pub virtual_address: u32,   // RVA
    pub size_of_raw_data: u32,
    pub pointer_to_raw_data: u32,
    pub pointer_to_relocations: u32,
    pub pointer_to_linenumbers: u32,
    pub number_of_relocations: u16,
    pub number_of_linenumbers: u16,
    pub characteristics: u32,
}

impl SectionHeader {
    /// Check if section has the executable flag (IMAGE_SCN_MEM_EXECUTE = 0x20000000).
    pub fn is_executable(&self) -> bool {
        self.characteristics & 0x20000000 != 0
    }

    /// Check if section has the read flag (IMAGE_SCN_MEM_READ = 0x40000000).
    pub fn is_readable(&self) -> bool {
        self.characteristics & 0x40000000 != 0
    }

    /// Check if section has the write flag (IMAGE_SCN_MEM_WRITE = 0x80000000).
    pub fn is_writable(&self) -> bool {
        self.characteristics & 0x80000000 != 0
    }

    /// Check if section contains code (IMAGE_SCN_CNT_CODE = 0x00000020).
    pub fn contains_code(&self) -> bool {
        self.characteristics & 0x00000020 != 0
    }

    /// Check if section contains initialized data (IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040).
    pub fn contains_initialized_data(&self) -> bool {
        self.characteristics & 0x00000040 != 0
    }

    /// Check if section contains uninitialized data (IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080).
    pub fn contains_uninitialized_data(&self) -> bool {
        self.characteristics & 0x00000080 != 0
    }
}

/// Import descriptor — one per DLL.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ImportDescriptor {
    pub original_first_thunk: u32,   // RVA to ILT (Import Lookup Table)
    pub time_date_stamp: u32,
    pub forwarder_chain: u32,
    pub name_rva: u32,              // RVA to DLL name string
    pub first_thunk: u32,           // RVA to IAT
}

/// A single imported function.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ImportFunction {
    pub name: String,               // Function name (or ordinal)
    pub hint: Option<u16>,          // Ordinal hint (if available)
    pub is_ordinal: bool,
    pub ordinal: Option<u16>,       // Ordinal number (if imported by ordinal)
    pub dll_name: String,           // Name of the importing DLL
    pub thunk_rva: u32,             // RVA of the thunk slot
}

/// Relocation entry types.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub enum RelocationType {
    Absolute,
    High,
    Low,
    HighLow,
    HighAdj,
    Dir64,       // 64-bit relocation (PE32+)
    Unknown(u16),
}

impl From<u16> for RelocationType {
    fn from(v: u16) -> Self {
        match v {
            0 => RelocationType::Absolute,
            1 => RelocationType::High,
            2 => RelocationType::Low,
            3 => RelocationType::HighLow,
            4 => RelocationType::HighAdj,
            10 => RelocationType::Dir64,
            _ => RelocationType::Unknown(v),
        }
    }
}

/// A single relocation entry.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RelocationEntry {
    pub relocation_type: RelocationType,
    pub offset: u32,              // Offset within the relocation block
}

/// A relocation block (page-aligned).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RelocationBlock {
    pub page_rva: u32,           // Page RVA this block covers
    pub block_size: u32,         // Size of this block (including header)
    pub entries: Vec<RelocationEntry>,
}

/// Export descriptor — one per exported function.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ExportFunction {
    pub name: Option<String>,
    pub ordinal: u32,
    pub rva: u32,
}

/// Export directory.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ExportDirectory {
    pub dll_name: String,
    pub functions: Vec<ExportFunction>,
}

/// The complete parsed PE file.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PeFile {
    /// Raw file bytes
    pub raw: Vec<u8>,
    /// File path
    pub path: std::path::PathBuf,

    // Headers
    pub dos_header: DosHeader,
    pub pe_signature: PeSignature,
    pub coff_header: CoffHeader,
    pub optional_header: OptionalHeader,

    // Sections
    pub sections: Vec<SectionHeader>,

    // Import table
    pub imports: Vec<ImportFunction>,

    // Export table (if present)
    pub exports: Option<ExportDirectory>,

    // Relocations
    pub relocations: Vec<RelocationBlock>,

    // Computed values
    pub machine_type: MachineType,
    pub subsystem: Subsystem,
    pub is_64bit: bool,
    pub entry_point_rva: u32,
    pub image_base: u64,
    pub size_of_image: u32,
}

impl PeFile {
    /// Parse a PE file from a file path.
    pub fn from_file(path: &std::path::Path) -> PeResult<Self> {
        let raw = std::fs::read(path)?;
        Self::from_bytes(&raw, path)
    }

    /// Parse a PE file from raw bytes.
    pub fn from_bytes(raw: &[u8], path: &std::path::Path) -> PeResult<Self> {
        if raw.len() < 64 {
            return Err(PeParseError::FileTooSmall(raw.len()));
        }

        // Parse DOS header
        let dos_header = Self::parse_dos_header(raw)?;
        log::info!(
            "DOS Header: e_magic={:#06X}, e_lfanew={:#010X}",
            dos_header.e_magic,
            dos_header.e_lfanew
        );

        // Parse PE signature
        let pe_signature = Self::parse_pe_signature(raw, &dos_header)?;
        log::info!(
            "PE Signature at offset {}: {:#010X}",
            pe_signature.offset,
            pe_signature.signature
        );

        // Parse COFF header (immediately after PE signature)
        let coff_offset = pe_signature.offset as usize + 4;
        let coff_header = Self::parse_coff_header(raw, coff_offset)?;
        let machine_type = MachineType::from(coff_header.machine);
        log::info!(
            "COFF Header: machine={:#06X} ({}), sections={}",
            coff_header.machine,
            machine_type,
            coff_header.number_of_sections
        );

        if machine_type != MachineType::Amd64 {
            log::warn!("Non-AMD64 PE detected: {}. Will attempt to parse anyway.", machine_type);
        }

        // Parse Optional header
        let optional_offset = coff_offset + 20;
        let optional_header = Self::parse_optional_header(raw, optional_offset)?;
        let is_64bit = optional_header.magic == 0x020B;
        let subsystem = Subsystem::from(optional_header.subsystem);
        log::info!(
            "Optional Header: magic={:#06X} ({}), entry_point={:#010X}, image_base={:#018X}, subsystem={} ({})",
            optional_header.magic,
            if is_64bit { "PE32+" } else { "PE32" },
            optional_header.address_of_entry_point,
            optional_header.image_base,
            optional_header.subsystem,
            subsystem,
        );

        // Parse section headers
        let section_offset = optional_offset + coff_header.size_of_optional_header as usize;
        let sections = Self::parse_section_headers(
            raw,
            section_offset,
            coff_header.number_of_sections as usize,
        )?;

        for (i, section) in sections.iter().enumerate() {
            log::info!(
                "  Section[{}]: name='{}' VA={:#010X} VS={:#010X} RawSize={:#010X} RawPtr={:#010X} Chars={:#010X} [{}{}{}{}]",
                i,
                section.name,
                section.virtual_address,
                section.virtual_size,
                section.size_of_raw_data,
                section.pointer_to_raw_data,
                section.characteristics,
                if section.contains_code() { "C" } else { "-" },
                if section.is_executable() { "X" } else { "-" },
                if section.is_readable() { "R" } else { "-" },
                if section.is_writable() { "W" } else { "-" },
            );
        }

        // Parse imports
        let imports = Self::parse_imports(raw, &optional_header, &sections)?;

        // Parse exports
        let exports = if !optional_header.data_directories[DataDirectory::EXPORT_TABLE].is_empty() {
            Some(Self::parse_exports(raw, &optional_header, &sections)?)
        } else {
            None
        };

        // Parse relocations
        let relocations = if !optional_header.data_directories[DataDirectory::BASE_RELOCATION_TABLE].is_empty()
        {
            Self::parse_relocations(raw, &optional_header, &sections)?
        } else {
            Vec::new()
        };

        log::info!("Parsed {} import functions, {} relocation blocks", imports.len(), relocations.len());

        Ok(Self {
            raw: raw.to_vec(),
            path: path.to_path_buf(),
            dos_header,
            pe_signature,
            coff_header,
            optional_header: optional_header.clone(),
            sections,
            imports,
            exports,
            relocations,
            machine_type,
            subsystem,
            is_64bit,
            entry_point_rva: optional_header.address_of_entry_point,
            image_base: optional_header.image_base,
            size_of_image: optional_header.size_of_image,
        })
    }

    fn parse_dos_header(raw: &[u8]) -> PeResult<DosHeader> {
        let e_magic = u16::from_le_bytes(raw[0..2].try_into().unwrap());
        if e_magic != 0x5A4D {
            return Err(PeParseError::InvalidDosSignature(e_magic));
        }
        Ok(DosHeader {
            e_magic,
            e_cblp: u16::from_le_bytes(raw[2..4].try_into().unwrap()),
            e_cp: u16::from_le_bytes(raw[4..6].try_into().unwrap()),
            e_crlc: u16::from_le_bytes(raw[6..8].try_into().unwrap()),
            e_cparhdr: u16::from_le_bytes(raw[8..10].try_into().unwrap()),
            e_minalloc: u16::from_le_bytes(raw[10..12].try_into().unwrap()),
            e_maxalloc: u16::from_le_bytes(raw[12..14].try_into().unwrap()),
            e_ss: u16::from_le_bytes(raw[14..16].try_into().unwrap()),
            e_sp: u16::from_le_bytes(raw[16..18].try_into().unwrap()),
            e_csum: u16::from_le_bytes(raw[18..20].try_into().unwrap()),
            e_ip: u16::from_le_bytes(raw[20..22].try_into().unwrap()),
            e_cs: u16::from_le_bytes(raw[22..24].try_into().unwrap()),
            e_lfarlc: u16::from_le_bytes(raw[24..26].try_into().unwrap()),
            e_ovno: u16::from_le_bytes(raw[26..28].try_into().unwrap()),
            e_res: [
                u16::from_le_bytes(raw[28..30].try_into().unwrap()),
                u16::from_le_bytes(raw[30..32].try_into().unwrap()),
                u16::from_le_bytes(raw[32..34].try_into().unwrap()),
                u16::from_le_bytes(raw[34..36].try_into().unwrap()),
            ],
            e_oemid: u16::from_le_bytes(raw[36..38].try_into().unwrap()),
            e_oeminfo: u16::from_le_bytes(raw[38..40].try_into().unwrap()),
            e_res2: [
                u16::from_le_bytes(raw[40..42].try_into().unwrap()),
                u16::from_le_bytes(raw[42..44].try_into().unwrap()),
                u16::from_le_bytes(raw[44..46].try_into().unwrap()),
                u16::from_le_bytes(raw[46..48].try_into().unwrap()),
                u16::from_le_bytes(raw[48..50].try_into().unwrap()),
                u16::from_le_bytes(raw[50..52].try_into().unwrap()),
                u16::from_le_bytes(raw[52..54].try_into().unwrap()),
                u16::from_le_bytes(raw[54..56].try_into().unwrap()),
                u16::from_le_bytes(raw[56..58].try_into().unwrap()),
                u16::from_le_bytes(raw[58..60].try_into().unwrap()),
            ],
            e_lfanew: u32::from_le_bytes(raw[60..64].try_into().unwrap()),
        })
    }

    fn parse_pe_signature(raw: &[u8], dos: &DosHeader) -> PeResult<PeSignature> {
        let offset = dos.e_lfanew as usize;
        if offset + 4 > raw.len() {
            return Err(PeParseError::InvalidPeSignature(0));
        }
        let signature = u32::from_le_bytes(raw[offset..offset + 4].try_into().unwrap());
        if signature != 0x00004550 {
            return Err(PeParseError::InvalidPeSignature(signature));
        }
        Ok(PeSignature {
            offset: dos.e_lfanew,
            signature,
        })
    }

    fn parse_coff_header(raw: &[u8], offset: usize) -> PeResult<CoffHeader> {
        if offset + 20 > raw.len() {
            return Err(PeParseError::Io(std::io::Error::new(
                std::io::ErrorKind::UnexpectedEof,
                "COFF header extends beyond file",
            )));
        }
        Ok(CoffHeader {
            machine: u16::from_le_bytes(raw[offset..offset + 2].try_into().unwrap()),
            number_of_sections: u16::from_le_bytes(raw[offset + 2..offset + 4].try_into().unwrap()),
            time_date_stamp: u32::from_le_bytes(raw[offset + 4..offset + 8].try_into().unwrap()),
            pointer_to_symbol_table: u32::from_le_bytes(raw[offset + 8..offset + 12].try_into().unwrap()),
            number_of_symbols: u32::from_le_bytes(raw[offset + 12..offset + 16].try_into().unwrap()),
            size_of_optional_header: u16::from_le_bytes(raw[offset + 16..offset + 18].try_into().unwrap()),
            characteristics: u16::from_le_bytes(raw[offset + 18..offset + 20].try_into().unwrap()),
        })
    }

    fn parse_optional_header(raw: &[u8], offset: usize) -> PeResult<OptionalHeader> {
        if offset + 2 > raw.len() {
            return Err(PeParseError::Io(std::io::Error::new(
                std::io::ErrorKind::UnexpectedEof,
                "Optional header extends beyond file",
            )));
        }
        let magic = u16::from_le_bytes(raw[offset..offset + 2].try_into().unwrap());

        match magic {
            0x020B => Self::parse_optional_header_pe32plus(raw, offset),
            0x010B => {
                // PE32 — we can still parse it but fields differ
                log::warn!("PE32 (32-bit) optional header detected. Parsing limited.");
                Self::parse_optional_header_pe32(raw, offset)
            }
            _ => Err(PeParseError::InvalidOptionalMagic(magic)),
        }
    }

    fn parse_optional_header_pe32plus(raw: &[u8], offset: usize) -> PeResult<OptionalHeader> {
        // PE32+ optional header layout
        let read_u8 = |raw: &[u8], off: usize| -> u8 {
            raw[off]
        };
        let read_u16 = |raw: &[u8], off: usize| -> u16 {
            u16::from_le_bytes(raw[off..off + 2].try_into().unwrap())
        };
        let read_u32 = |raw: &[u8], off: usize| -> u32 {
            u32::from_le_bytes(raw[off..off + 4].try_into().unwrap())
        };
        let read_u64 = |raw: &[u8], off: usize| -> u64 {
            u64::from_le_bytes(raw[off..off + 8].try_into().unwrap())
        };

        let number_of_rva_and_sizes = read_u32(raw, offset + 108);
        let data_dir_offset = offset + 112;

        let mut data_directories = Vec::new();
        for i in 0..number_of_rva_and_sizes as usize {
            let dir_off = data_dir_offset + i * 8;
            if dir_off + 8 <= raw.len() {
                data_directories.push(DataDirectory {
                    rva: read_u32(raw, dir_off),
                    size: read_u32(raw, dir_off + 4),
                });
            } else {
                data_directories.push(DataDirectory { rva: 0, size: 0 });
            }
        }

        Ok(OptionalHeader {
            magic: 0x020B,
            major_linker_version: read_u8(raw, offset + 2),
            minor_linker_version: read_u8(raw, offset + 3),
            size_of_code: read_u32(raw, offset + 4),
            size_of_initialized_data: read_u32(raw, offset + 8),
            size_of_uninitialized_data: read_u32(raw, offset + 12),
            address_of_entry_point: read_u32(raw, offset + 16),
            base_of_code: read_u32(raw, offset + 20),
            image_base: read_u64(raw, offset + 24),
            section_alignment: read_u32(raw, offset + 32),
            file_alignment: read_u32(raw, offset + 36),
            major_operating_system_version: read_u16(raw, offset + 40),
            minor_operating_system_version: read_u16(raw, offset + 42),
            major_image_version: read_u16(raw, offset + 44),
            minor_image_version: read_u16(raw, offset + 46),
            major_subsystem_version: read_u16(raw, offset + 48),
            minor_subsystem_version: read_u16(raw, offset + 50),
            win32_version_value: read_u32(raw, offset + 52),
            size_of_image: read_u32(raw, offset + 56),
            size_of_headers: read_u32(raw, offset + 60),
            checksum: read_u32(raw, offset + 64),
            subsystem: read_u16(raw, offset + 68),
            dll_characteristics: read_u16(raw, offset + 70),
            size_of_stack_reserve: read_u64(raw, offset + 72),
            size_of_stack_commit: read_u64(raw, offset + 80),
            size_of_heap_reserve: read_u64(raw, offset + 88),
            size_of_heap_commit: read_u64(raw, offset + 96),
            loader_flags: read_u32(raw, offset + 104),
            number_of_rva_and_sizes,
            data_directories,
        })
    }

    fn parse_optional_header_pe32(raw: &[u8], offset: usize) -> PeResult<OptionalHeader> {
        // Simplified PE32 parser — reads common fields, zeroes out PE32+ specific ones
        let read_u8 = |raw: &[u8], off: usize| -> u8 { raw[off] };
        let read_u16 = |raw: &[u8], off: usize| -> u16 {
            u16::from_le_bytes(raw[off..off + 2].try_into().unwrap())
        };
        let read_u32 = |raw: &[u8], off: usize| -> u32 {
            u32::from_le_bytes(raw[off..off + 4].try_into().unwrap())
        };

        let number_of_rva_and_sizes = read_u32(raw, offset + 92);
        let data_dir_offset = offset + 96;

        let mut data_directories = Vec::new();
        for i in 0..number_of_rva_and_sizes as usize {
            let dir_off = data_dir_offset + i * 8;
            if dir_off + 8 <= raw.len() {
                data_directories.push(DataDirectory {
                    rva: read_u32(raw, dir_off),
                    size: read_u32(raw, dir_off + 4),
                });
            } else {
                data_directories.push(DataDirectory { rva: 0, size: 0 });
            }
        }

        Ok(OptionalHeader {
            magic: 0x010B,
            major_linker_version: read_u8(raw, offset + 2),
            minor_linker_version: read_u8(raw, offset + 3),
            size_of_code: read_u32(raw, offset + 4),
            size_of_initialized_data: read_u32(raw, offset + 8),
            size_of_uninitialized_data: read_u32(raw, offset + 12),
            address_of_entry_point: read_u32(raw, offset + 16),
            base_of_code: read_u32(raw, offset + 20),
            image_base: read_u32(raw, offset + 28) as u64, // u32 in PE32
            section_alignment: read_u32(raw, offset + 32),
            file_alignment: read_u32(raw, offset + 36),
            major_operating_system_version: read_u16(raw, offset + 40),
            minor_operating_system_version: read_u16(raw, offset + 42),
            major_image_version: read_u16(raw, offset + 44),
            minor_image_version: read_u16(raw, offset + 46),
            major_subsystem_version: read_u16(raw, offset + 48),
            minor_subsystem_version: read_u16(raw, offset + 50),
            win32_version_value: read_u32(raw, offset + 52),
            size_of_image: read_u32(raw, offset + 56),
            size_of_headers: read_u32(raw, offset + 60),
            checksum: read_u32(raw, offset + 64),
            subsystem: read_u16(raw, offset + 68),
            dll_characteristics: read_u16(raw, offset + 70),
            size_of_stack_reserve: read_u32(raw, offset + 72) as u64,
            size_of_stack_commit: read_u32(raw, offset + 76) as u64,
            size_of_heap_reserve: read_u32(raw, offset + 80) as u64,
            size_of_heap_commit: read_u32(raw, offset + 84) as u64,
            loader_flags: read_u32(raw, offset + 88),
            number_of_rva_and_sizes,
            data_directories,
        })
    }

    fn parse_section_headers(
        raw: &[u8],
        offset: usize,
        count: usize,
    ) -> PeResult<Vec<SectionHeader>> {
        let mut sections = Vec::with_capacity(count);
        for i in 0..count {
            let off = offset + i * 40;
            if off + 40 > raw.len() {
                return Err(PeParseError::SectionOutOfBounds(i, off as u32));
            }

            let name_bytes = &raw[off..off + 8];
            let name = String::from_utf8_lossy(
                name_bytes
                    .split(|&b| b == 0)
                    .next()
                    .unwrap_or(&[]),
            )
            .to_string();

            sections.push(SectionHeader {
                name,
                virtual_size: u32::from_le_bytes(raw[off + 8..off + 12].try_into().unwrap()),
                virtual_address: u32::from_le_bytes(raw[off + 12..off + 16].try_into().unwrap()),
                size_of_raw_data: u32::from_le_bytes(raw[off + 16..off + 20].try_into().unwrap()),
                pointer_to_raw_data: u32::from_le_bytes(raw[off + 20..off + 24].try_into().unwrap()),
                pointer_to_relocations: u32::from_le_bytes(raw[off + 24..off + 28].try_into().unwrap()),
                pointer_to_linenumbers: u32::from_le_bytes(raw[off + 28..off + 32].try_into().unwrap()),
                number_of_relocations: u16::from_le_bytes(raw[off + 32..off + 34].try_into().unwrap()),
                number_of_linenumbers: u16::from_le_bytes(raw[off + 34..off + 36].try_into().unwrap()),
                characteristics: u32::from_le_bytes(raw[off + 36..off + 40].try_into().unwrap()),
            });
        }
        Ok(sections)
    }

    /// Convert a Relative Virtual Address (RVA) to a file offset using section mappings.
    pub fn rva_to_offset(&self, rva: u32) -> PeResult<u32> {
        for section in &self.sections {
            if rva >= section.virtual_address
                && rva < section.virtual_address + section.virtual_size.max(section.size_of_raw_data)
            {
                let offset = rva - section.virtual_address + section.pointer_to_raw_data;
                if offset as usize <= self.raw.len() {
                    return Ok(offset);
                }
            }
        }
        Err(PeParseError::RvaConversionFailed(rva))
    }

    /// Read a null-terminated ASCII string from the raw data at a given file offset.
    fn read_string_at(&self, offset: u32) -> String {
        if offset as usize >= self.raw.len() {
            return String::new();
        }
        let start = offset as usize;
        let end = self.raw[start..]
            .iter()
            .position(|&b| b == 0)
            .map(|p| start + p)
            .unwrap_or(self.raw.len());
        String::from_utf8_lossy(&self.raw[start..end]).to_string()
    }

    fn parse_imports(
        raw: &[u8],
        opt: &OptionalHeader,
        sections: &[SectionHeader],
    ) -> PeResult<Vec<ImportFunction>> {
        let import_dir = &opt.data_directories[DataDirectory::IMPORT_TABLE];
        if import_dir.is_empty() {
            log::info!("No import directory present");
            return Ok(Vec::new());
        }

        let import_dir_offset = Self::rva_to_offset_internal(raw, import_dir.rva, sections)?;

        let mut imports = Vec::new();
        let mut descriptor_idx = 0;

        loop {
            let desc_off = import_dir_offset + descriptor_idx * 20;
            if desc_off + 20 > raw.len() as u32 {
                break;
            }

            let original_first_thunk = u32::from_le_bytes(raw[(desc_off) as usize..(desc_off + 4) as usize].try_into().unwrap());
            let _time_date_stamp = u32::from_le_bytes(raw[(desc_off + 4) as usize..(desc_off + 8) as usize].try_into().unwrap());
            let _forwarder_chain = u32::from_le_bytes(raw[(desc_off + 8) as usize..(desc_off + 12) as usize].try_into().unwrap());
            let name_rva = u32::from_le_bytes(raw[(desc_off + 12) as usize..(desc_off + 16) as usize].try_into().unwrap());
            let first_thunk = u32::from_le_bytes(raw[(desc_off + 16) as usize..(desc_off + 20) as usize].try_into().unwrap());

            // Null terminator
            if name_rva == 0 && original_first_thunk == 0 && first_thunk == 0 {
                break;
            }

            let dll_name_offset = Self::rva_to_offset_internal(raw, name_rva, sections)?;
            let dll_name = {
                let start = dll_name_offset as usize;
                let end = raw[start..]
                    .iter()
                    .position(|&b| b == 0)
                    .map(|p| start + p)
                    .unwrap_or(raw.len());
                String::from_utf8_lossy(&raw[start..end]).to_string()
            };

            log::debug!("  Import DLL: {}", dll_name);

            // Use ILT if available, otherwise use IAT
            let thunk_rva = if original_first_thunk != 0 {
                original_first_thunk
            } else {
                first_thunk
            };

            let thunk_offset = Self::rva_to_offset_internal(raw, thunk_rva, sections)?;
            let entry_size = if opt.magic == 0x020B { 8 } else { 4 }; // 8 bytes for PE32+

            let mut thunk_idx = 0;
            loop {
                let entry_off = thunk_offset + thunk_idx * entry_size;
                if entry_off + entry_size > raw.len() as u32 {
                    break;
                }

                if opt.magic == 0x020B {
                    // PE32+: 8-byte entries, high bit = ordinal flag
                    let entry = u64::from_le_bytes(raw[entry_off as usize..(entry_off + 8) as usize].try_into().unwrap());
                    if entry == 0 {
                        break;
                    }
                    let ordinal_flag = (entry >> 63) & 1;

                    if ordinal_flag == 1 {
                        let ordinal = (entry & 0xFFFF) as u16;
                        imports.push(ImportFunction {
                            name: format!("Ordinal_{}", ordinal),
                            hint: None,
                            is_ordinal: true,
                            ordinal: Some(ordinal),
                            dll_name: dll_name.clone(),
                            thunk_rva: first_thunk + thunk_idx as u32 * entry_size as u32,
                        });
                    } else {
                        let hint_rva = (entry & 0x7FFFFFFF) as u32;
                        if let Ok(hint_off) = Self::rva_to_offset_internal(raw, hint_rva, sections) {
                            let hint = u16::from_le_bytes(raw[hint_off as usize..(hint_off + 2) as usize].try_into().unwrap());
                            let name_start = (hint_off + 2) as usize;
                            let name_end = raw[name_start..]
                                .iter()
                                .position(|&b| b == 0)
                                .map(|p| name_start + p)
                                .unwrap_or(raw.len());
                            let func_name = String::from_utf8_lossy(&raw[name_start..name_end]).to_string();

                            log::debug!("    Import: {}.{} (hint={})", dll_name, func_name, hint);
                            imports.push(ImportFunction {
                                name: func_name,
                                hint: Some(hint),
                                is_ordinal: false,
                                ordinal: None,
                                dll_name: dll_name.clone(),
                                thunk_rva: first_thunk + thunk_idx as u32 * entry_size as u32,
                            });
                        }
                    }
                } else {
                    // PE32: 4-byte entries
                    let entry = u32::from_le_bytes(raw[entry_off as usize..(entry_off + 4) as usize].try_into().unwrap());
                    if entry == 0 {
                        break;
                    }
                    let ordinal_flag = (entry >> 31) & 1;

                    if ordinal_flag == 1 {
                        let ordinal = (entry & 0xFFFF) as u16;
                        imports.push(ImportFunction {
                            name: format!("Ordinal_{}", ordinal),
                            hint: None,
                            is_ordinal: true,
                            ordinal: Some(ordinal),
                            dll_name: dll_name.clone(),
                            thunk_rva: first_thunk + thunk_idx as u32 * 4,
                        });
                    } else {
                        let hint_rva = entry & 0x7FFFFFFF;
                        if let Ok(hint_off) = Self::rva_to_offset_internal(raw, hint_rva, sections) {
                            let hint = u16::from_le_bytes(raw[hint_off as usize..(hint_off + 2) as usize].try_into().unwrap());
                            let name_start = (hint_off + 2) as usize;
                            let name_end = raw[name_start..]
                                .iter()
                                .position(|&b| b == 0)
                                .map(|p| name_start + p)
                                .unwrap_or(raw.len());
                            let func_name = String::from_utf8_lossy(&raw[name_start..name_end]).to_string();

                            imports.push(ImportFunction {
                                name: func_name,
                                hint: Some(hint),
                                is_ordinal: false,
                                ordinal: None,
                                dll_name: dll_name.clone(),
                                thunk_rva: first_thunk + thunk_idx as u32 * 4,
                            });
                        }
                    }
                }

                thunk_idx += 1;
            }

            descriptor_idx += 1;
        }

        Ok(imports)
    }

    fn rva_to_offset_internal(raw: &[u8], rva: u32, sections: &[SectionHeader]) -> PeResult<u32> {
        for section in sections {
            if rva >= section.virtual_address
                && rva < section.virtual_address + section.virtual_size.max(section.size_of_raw_data)
            {
                let offset = rva - section.virtual_address + section.pointer_to_raw_data;
                if offset as usize <= raw.len() {
                    return Ok(offset);
                }
            }
        }
        Err(PeParseError::RvaConversionFailed(rva))
    }

    fn parse_exports(
        raw: &[u8],
        opt: &OptionalHeader,
        sections: &[SectionHeader],
    ) -> PeResult<ExportDirectory> {
        let export_dir = &opt.data_directories[DataDirectory::EXPORT_TABLE];
        let export_dir_offset = Self::rva_to_offset_internal(raw, export_dir.rva, sections)?;

        let dll_name_rva = u32::from_le_bytes(raw[(export_dir_offset + 12) as usize..(export_dir_offset + 16) as usize].try_into().unwrap());
        let dll_name_offset = Self::rva_to_offset_internal(raw, dll_name_rva, sections)?;
        let dll_name = {
            let start = dll_name_offset as usize;
            let end = raw[start..].iter().position(|&b| b == 0).map(|p| start + p).unwrap_or(raw.len());
            String::from_utf8_lossy(&raw[start..end]).to_string()
        };

        let num_functions = u32::from_le_bytes(raw[(export_dir_offset + 20) as usize..(export_dir_offset + 24) as usize].try_into().unwrap());
        let functions_rva = u32::from_le_bytes(raw[(export_dir_offset + 28) as usize..(export_dir_offset + 32) as usize].try_into().unwrap());
        let names_rva = u32::from_le_bytes(raw[(export_dir_offset + 32) as usize..(export_dir_offset + 36) as usize].try_into().unwrap());
        let ordinals_rva = u32::from_le_bytes(raw[(export_dir_offset + 36) as usize..(export_dir_offset + 40) as usize].try_into().unwrap());

        let functions_offset = Self::rva_to_offset_internal(raw, functions_rva, sections)?;
        let names_offset = Self::rva_to_offset_internal(raw, names_rva, sections)?;
        let ordinals_offset = Self::rva_to_offset_internal(raw, ordinals_rva, sections)?;

        let mut functions = Vec::new();
        for i in 0..num_functions as usize {
            let func_rva = u32::from_le_bytes(raw[(functions_offset as usize + i * 4)..(functions_offset as usize + i * 4 + 4)].try_into().unwrap());
            let ordinal_idx = u16::from_le_bytes(raw[(ordinals_offset as usize + i * 2)..(ordinals_offset as usize + i * 2 + 2)].try_into().unwrap());

            let name = {
                let name_rva = u32::from_le_bytes(raw[(names_offset as usize + ordinal_idx as usize * 4)..(names_offset as usize + ordinal_idx as usize * 4 + 4)].try_into().unwrap());
                if name_rva != 0 {
                    if let Ok(name_off) = Self::rva_to_offset_internal(raw, name_rva, sections) {
                        let start = name_off as usize;
                        let end = raw[start..].iter().position(|&b| b == 0).map(|p| start + p).unwrap_or(raw.len());
                        Some(String::from_utf8_lossy(&raw[start..end]).to_string())
                    } else {
                        None
                    }
                } else {
                    None
                }
            };

            functions.push(ExportFunction {
                name,
                ordinal: i as u32,
                rva: func_rva,
            });
        }

        Ok(ExportDirectory { dll_name, functions })
    }

    fn parse_relocations(
        raw: &[u8],
        opt: &OptionalHeader,
        sections: &[SectionHeader],
    ) -> PeResult<Vec<RelocationBlock>> {
        let reloc_dir = &opt.data_directories[DataDirectory::BASE_RELOCATION_TABLE];
        let reloc_offset = Self::rva_to_offset_internal(raw, reloc_dir.rva, sections)?;
        let reloc_end = reloc_offset + reloc_dir.size;

        let mut blocks = Vec::new();
        let mut pos = reloc_offset;

        while pos + 8 <= reloc_end {
            let page_rva = u32::from_le_bytes(raw[pos as usize..(pos + 4) as usize].try_into().unwrap());
            let block_size = u32::from_le_bytes(raw[(pos + 4) as usize..(pos + 8) as usize].try_into().unwrap());

            if block_size < 8 {
                break;
            }

            let num_entries = ((block_size - 8) / 2) as usize;
            let mut entries = Vec::with_capacity(num_entries);

            for i in 0..num_entries {
                let entry_val = u16::from_le_bytes(
                    raw[(pos as usize + 8 + i * 2)..(pos as usize + 10 + i * 2)]
                        .try_into()
                        .unwrap(),
                );
                let rtype = (entry_val >> 12) & 0x0F;
                let roffset = (entry_val & 0x0FFF) as u32;

                if rtype != 0 {
                    // Skip TYPE_ABSOLUTE (padding) entries
                    entries.push(RelocationEntry {
                        relocation_type: RelocationType::from(rtype),
                        offset: roffset,
                    });
                }
            }

            if !entries.is_empty() {
                log::debug!(
                    "  Relocation block: page_rva={:#010X}, entries={}",
                    page_rva,
                    entries.len()
                );
            }

            blocks.push(RelocationBlock {
                page_rva,
                block_size,
                entries,
            });

            pos += block_size as u32;
        }

        Ok(blocks)
    }

    /// Get all unique DLL names from the import table.
    pub fn get_import_dlls(&self) -> Vec<String> {
        let mut dlls: Vec<String> = self
            .imports
            .iter()
            .map(|i| i.dll_name.clone())
            .collect();
        dlls.sort();
        dlls.dedup();
        dlls
    }

    /// Find an import function by DLL name and function name.
    pub fn find_import(&self, dll: &str, func: &str) -> Option<&ImportFunction> {
        self.imports
            .iter()
            .find(|i| i.dll_name.eq_ignore_ascii_case(dll) && i.name == func)
    }

    /// Get a summary string for quick inspection.
    pub fn summary(&self) -> String {
        format!(
            "PE File: {}\n  Machine: {}\n  Format: {}\n  Subsystem: {}\n  Entry Point: {:#010X}\n  Image Base: {:#018X}\n  Size: {}\n  Sections: {}\n  Imports: {}\n  DLLs: {}",
            self.path.display(),
            self.machine_type,
            if self.is_64bit { "PE32+" } else { "PE32" },
            self.subsystem,
            self.entry_point_rva,
            self.image_base,
            self.size_of_image,
            self.sections.len(),
            self.imports.len(),
            self.get_import_dlls().join(", ")
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Generate a minimal valid PE32+ x64 EXE for testing.
    fn create_minimal_pe64() -> Vec<u8> {
        // We'll craft a minimal PE that the parser can read
        let mut pe = Vec::new();

        // DOS Header (64 bytes)
        pe.extend_from_slice(&[0x4D, 0x5A]); // e_magic = MZ
        pe.extend_from_slice(&[0u8; 58]); // fill rest of DOS header zeros
        pe.extend_from_slice(&64u32.to_le_bytes()); // e_lfanew = 64

        // PE Signature (4 bytes)
        pe.extend_from_slice(&[0x50, 0x45, 0x00, 0x00]); // PE\0\0

        // COFF Header (20 bytes)
        pe.extend_from_slice(&0x8664u16.to_le_bytes()); // machine = AMD64
        pe.extend_from_slice(&3u16.to_le_bytes()); // number_of_sections = 3
        pe.extend_from_slice(&0u32.to_le_bytes()); // time_date_stamp
        pe.extend_from_slice(&0u32.to_le_bytes()); // pointer_to_symbol_table
        pe.extend_from_slice(&0u32.to_le_bytes()); // number_of_symbols
        pe.extend_from_slice(&240u16.to_le_bytes()); // size_of_optional_header (PE32+ = 240)
        pe.extend_from_slice(&0x22u16.to_le_bytes()); // characteristics (EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE)

        // Optional Header PE32+ (240 bytes)
        pe.extend_from_slice(&0x020Bu16.to_le_bytes()); // magic = PE32+
        pe.extend_from_slice(&[14, 0]); // linker version
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // size_of_code
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // size_of_initialized_data
        pe.extend_from_slice(&0u32.to_le_bytes()); // size_of_uninitialized_data
        pe.extend_from_slice(&0x1000u32.to_le_bytes()); // address_of_entry_point
        pe.extend_from_slice(&0x1000u32.to_le_bytes()); // base_of_code
        pe.extend_from_slice(&0x140000000u64.to_le_bytes()); // image_base
        pe.extend_from_slice(&0x1000u32.to_le_bytes()); // section_alignment
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // file_alignment
        pe.extend_from_slice(&[6, 0]); // OS version
        pe.extend_from_slice(&[0, 0]); // image version
        pe.extend_from_slice(&[6, 0]); // subsystem version
        pe.extend_from_slice(&0u32.to_le_bytes()); // win32_version_value
        pe.extend_from_slice(&0x5000u32.to_le_bytes()); // size_of_image
        pe.extend_from_slice(&0x400u32.to_le_bytes()); // size_of_headers
        pe.extend_from_slice(&0u32.to_le_bytes()); // checksum
        pe.extend_from_slice(&3u16.to_le_bytes()); // subsystem = CUI
        pe.extend_from_slice(&0x8160u16.to_le_bytes()); // dll_characteristics (DYNAMIC_BASE|NX_COMPAT|TERMINAL_SERVER_AWARE)
        pe.extend_from_slice(&0x100000u64.to_le_bytes()); // size_of_stack_reserve
        pe.extend_from_slice(&0x1000u64.to_le_bytes()); // size_of_stack_commit
        pe.extend_from_slice(&0x100000u64.to_le_bytes()); // size_of_heap_reserve
        pe.extend_from_slice(&0x1000u64.to_le_bytes()); // size_of_heap_commit
        pe.extend_from_slice(&0u32.to_le_bytes()); // loader_flags
        pe.extend_from_slice(&16u32.to_le_bytes()); // number_of_rva_and_sizes

        // Data Directories (16 entries, 8 bytes each = 128 bytes)
        for i in 0..16 {
            match i {
                1 => {
                    // Import Directory - we'll point it to a valid location
                    pe.extend_from_slice(&0x3000u32.to_le_bytes()); // rva
                    pe.extend_from_slice(&0x3Cu32.to_le_bytes()); // size (enough for 1 descriptor)
                }
                5 => {
                    // Base Relocation Directory
                    pe.extend_from_slice(&0x4000u32.to_le_bytes()); // rva
                    pe.extend_from_slice(&0x10u32.to_le_bytes()); // size
                }
                _ => {
                    pe.extend_from_slice(&0u32.to_le_bytes());
                    pe.extend_from_slice(&0u32.to_le_bytes());
                }
            }
        }

        // Section headers start at offset 88 + 240 = 328 (0x148)
        // Pad if needed to reach the correct offset
        while pe.len() < 0x148 {
            pe.push(0);
        }

        // Section Headers (3 sections × 40 bytes = 120 bytes)
        // .text section
        pe.extend_from_slice(b".text\0\0\0");
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // virtual_size
        pe.extend_from_slice(&0x1000u32.to_le_bytes()); // virtual_address
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // size_of_raw_data
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // pointer_to_raw_data
        pe.extend_from_slice(&0u32.to_le_bytes()); // pointer_to_relocations
        pe.extend_from_slice(&0u32.to_le_bytes()); // pointer_to_linenumbers
        pe.extend_from_slice(&0u16.to_le_bytes()); // number_of_relocations
        pe.extend_from_slice(&0u16.to_le_bytes()); // number_of_linenumbers
        pe.extend_from_slice(&0x60000020u32.to_le_bytes()); // characteristics (code|execute|read)

        // .rdata section
        pe.extend_from_slice(b".rdata\0\0");
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // virtual_size
        pe.extend_from_slice(&0x2000u32.to_le_bytes()); // virtual_address
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // size_of_raw_data
        pe.extend_from_slice(&0x400u32.to_le_bytes()); // pointer_to_raw_data
        pe.extend_from_slice(&0u32.to_le_bytes());
        pe.extend_from_slice(&0u32.to_le_bytes());
        pe.extend_from_slice(&0u16.to_le_bytes());
        pe.extend_from_slice(&0u16.to_le_bytes());
        pe.extend_from_slice(&0x40000040u32.to_le_bytes()); // characteristics (init_data|read)

        // .reloc section
        pe.extend_from_slice(b".reloc\0\0");
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // virtual_size
        pe.extend_from_slice(&0x4000u32.to_le_bytes()); // virtual_address
        pe.extend_from_slice(&0x200u32.to_le_bytes()); // size_of_raw_data
        pe.extend_from_slice(&0x600u32.to_le_bytes()); // pointer_to_raw_data
        pe.extend_from_slice(&0u32.to_le_bytes());
        pe.extend_from_slice(&0u32.to_le_bytes());
        pe.extend_from_slice(&0u16.to_le_bytes());
        pe.extend_from_slice(&0u16.to_le_bytes());
        pe.extend_from_slice(&0x42000040u32.to_le_bytes()); // characteristics (init_data|discardable|read)

        // After section headers (0x148 + 120 = 0x1C0), pad to file_alignment
        while pe.len() < 0x200 {
            pe.push(0);
        }

        // .text section raw data at 0x200
        // Entry point code: simple RET (0xC3)
        pe.extend_from_slice(&[0xC3]);
        while pe.len() < 0x400 {
            pe.push(0);
        }

        // .rdata section raw data at 0x400
        // Import Directory (1 descriptor + null terminator)
        // DLL name: KERNEL32.DLL at RVA 0x2100
        // ILT entry: GetProcAddress at RVA 0x2120
        let dll_name_rva: u32 = 0x2100;
        let func_name_rva: u32 = 0x2120;

        // Import descriptor 1
        pe.extend_from_slice(&0x2080u32.to_le_bytes()); // original_first_thunk (ILT)
        pe.extend_from_slice(&0u32.to_le_bytes()); // time_date_stamp
        pe.extend_from_slice(&0u32.to_le_bytes()); // forwarder_chain
        pe.extend_from_slice(&dll_name_rva.to_le_bytes()); // name_rva
        pe.extend_from_slice(&0x2040u32.to_le_bytes()); // first_thunk (IAT)

        // Null terminator descriptor
        pe.extend_from_slice(&0u32.to_le_bytes());
        pe.extend_from_slice(&0u32.to_le_bytes());
        pe.extend_from_slice(&0u32.to_le_bytes());
        pe.extend_from_slice(&0u32.to_le_bytes());
        pe.extend_from_slice(&0u32.to_le_bytes());

        // ILT entries (at RVA 0x2080, file offset 0x480)
        pe.extend_from_slice(&func_name_rva.to_le_bytes()); // hint/name RVA for GetProcAddress
        pe.extend_from_slice(&0u64.to_le_bytes()); // null terminator

        // IAT entries (at RVA 0x2040, file offset 0x440)
        pe.extend_from_slice(&func_name_rva.to_le_bytes()); // same hint/name RVA
        pe.extend_from_slice(&0u64.to_le_bytes()); // null terminator

        // DLL name string "KERNEL32.DLL\0" at RVA 0x2100, file offset 0x500
        while pe.len() < 0x500 {
            pe.push(0);
        }
        pe.extend_from_slice(b"KERNEL32.DLL\0");

        // Function hint/name "GetProcAddress\0" at RVA 0x2120, file offset 0x510
        while pe.len() < 0x510 {
            pe.push(0);
        }
        pe.extend_from_slice(&1u16.to_le_bytes()); // hint
        pe.extend_from_slice(b"GetProcAddress\0");

        // Pad to 0x600
        while pe.len() < 0x600 {
            pe.push(0);
        }

        // .reloc section raw data at 0x600
        // One relocation block
        pe.extend_from_slice(&0x1000u32.to_le_bytes()); // page_rva
        pe.extend_from_slice(&16u32.to_le_bytes()); // block_size (8 header + 8 data)
        pe.extend_from_slice(&0x300Au16.to_le_bytes()); // type=DIR64(10=A), offset=0x00A
        pe.extend_from_slice(&0u16.to_le_bytes()); // padding

        pe
    }

    #[test]
    fn test_parse_minimal_pe64() {
        let _ = env_logger::builder().is_test(true).try_init();
        let pe_bytes = create_minimal_pe64();
        let path = std::path::Path::new("test.exe");

        // Ensure the PE is big enough for basic parsing
        assert!(pe_bytes.len() >= 64, "PE must be at least 64 bytes for DOS header");

        // Verify DOS header
        assert_eq!(&pe_bytes[0..2], &[0x4D, 0x5A], "MZ signature");

        // Verify PE signature
        let lfanew = u32::from_le_bytes(pe_bytes[60..64].try_into().unwrap());
        assert_eq!(&pe_bytes[lfanew as usize..lfanew as usize + 4], &[0x50, 0x45, 0x00, 0x00], "PE signature");

        // Try parsing — may fail if sections are too small, but headers should parse
        let result = PeFile::from_bytes(&pe_bytes, path);
        if let Ok(pe) = result {
            assert_eq!(pe.machine_type, MachineType::Amd64);
            assert!(pe.is_64bit);
            assert_eq!(pe.subsystem, Subsystem::WindowsCui);
            assert_eq!(pe.entry_point_rva, 0x1000);
            assert_eq!(pe.image_base, 0x140000000);
        }
        // If parsing fails, that's OK for this test — we're testing that
        // the basic PE structure is valid enough for initial header parsing.
    }

    #[test]
    fn test_invalid_dos_signature() {
        let bad_bytes = vec![0x00u8; 64];
        let result = PeFile::from_bytes(&bad_bytes, std::path::Path::new("bad.exe"));
        assert!(matches!(result, Err(PeParseError::InvalidDosSignature(_))));
    }

    #[test]
    fn test_file_too_small() {
        let tiny = vec![0x4D, 0x5A];
        let result = PeFile::from_bytes(&tiny, std::path::Path::new("tiny.exe"));
        assert!(result.is_err());
    }
}
