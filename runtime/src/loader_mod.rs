//! # PE Loader
//!
//! Responsible for loading a parsed PE file into memory:
//! - Map sections to virtual addresses (using mmap)
//! - Apply base relocations
//! - Resolve imports via the Win32 dispatch layer
//! - Set memory permissions (R/W/X)
//! - Prepare the execution environment
//!
//! Does NOT execute code — that's the execution backend's job.

use crate::pe::{PeFile, PeResult};
use crate::trace::{TraceRecorder, TraceCategory};
use crate::dispatch::Win32Dispatcher;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Result of a load operation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LoadResult {
    /// The base address where the image was loaded in memory.
    pub base_address: u64,
    /// Size of the loaded image.
    pub image_size: u64,
    /// Entry point VA (base_address + entry_point_rva).
    pub entry_point_va: u64,
    /// Number of sections mapped.
    pub sections_mapped: usize,
    /// Number of relocations applied.
    pub relocations_applied: usize,
    /// Number of imports resolved.
    pub imports_resolved: usize,
    /// Map of thunk RVA → resolved function pointer.
    pub resolved_imports: HashMap<u32, u64>,
}

/// The PE loader — maps sections into process memory.
pub struct PELoader {
    /// Trace recorder for this load session.
    pub(crate) trace: TraceRecorder,
}

impl PELoader {
    /// Create a new PE loader with trace recording.
    pub fn new(trace: TraceRecorder) -> Self {
        Self { trace }
    }

    /// Load a PE file into memory (simulated — no actual mmap in analysis mode).
    /// Returns a LoadResult with all the computed addresses and resolved imports.
    pub fn load(&mut self, pe: &PeFile, dispatcher: &mut Win32Dispatcher)
        -> PeResult<LoadResult>
    {
        let base_address = pe.image_base;
        let image_size = pe.size_of_image as u64;

        self.trace.record_system(
            &format!("Loading PE: {} at base {:#018X}", pe.path.display(), base_address),
            Some(serde_json::json!({
                "image_size": image_size,
                "sections": pe.sections.len(),
                "imports": pe.imports.len(),
                "relocations": pe.relocations.len(),
            })),
        );

        let mut sections_mapped = 0;
        let mut relocations_applied = 0;
        let mut resolved_imports = HashMap::new();

        // Phase 1: Map sections
        self.trace.record_system("Phase 1: Mapping sections", None);
        for section in &pe.sections {
            let permissions = format!(
                "{}{}{}",
                if section.is_readable() { "R" } else { "-" },
                if section.is_writable() { "W" } else { "-" },
                if section.is_executable() { "X" } else { "-" },
            );

            let va = base_address + section.virtual_address as u64;
            self.trace.record_memory_load(
                &section.name,
                section.virtual_address,
                section.pointer_to_raw_data,
                section.size_of_raw_data.max(section.virtual_size),
                &permissions,
            );
            sections_mapped += 1;
        }

        // Phase 2: Apply relocations
        self.trace.record_system("Phase 2: Applying base relocations", None);
        let relocation_delta = 0i64; // base_address == pe.image_base, so delta = 0

        for block in &pe.relocations {
            for entry in &block.entries {
                let target_rva = block.page_rva + entry.offset;
                let target_va = base_address + target_rva as u64;

                match entry.relocation_type {
                    crate::pe::RelocationType::Dir64 => {
                        // 64-bit relocation
                        let old_value = 0u64; // Simulated
                        let new_value = old_value.wrapping_add(relocation_delta as u64);
                        self.trace.record_relocation(
                            block.page_rva,
                            "DIR64",
                            entry.offset,
                            old_value,
                            new_value,
                        );
                    }
                    crate::pe::RelocationType::HighLow => {
                        let old_value = 0u64;
                        let new_value = old_value.wrapping_add(relocation_delta as u64);
                        self.trace.record_relocation(
                            block.page_rva,
                            "HIGHLOW",
                            entry.offset,
                            old_value,
                            new_value,
                        );
                    }
                    _ => {
                        log::debug!(
                            "Skipping relocation type {:?} at {:#010X}",
                            entry.relocation_type,
                            target_rva
                        );
                    }
                }
                relocations_applied += 1;
            }
        }

        // Phase 3: Resolve imports
        self.trace.record_system("Phase 3: Resolving imports", None);
        for import in &pe.imports {
            if let Some(addr) = dispatcher.resolve(&import.dll_name, &import.name) {
                resolved_imports.insert(import.thunk_rva, addr);
            } else {
                log::warn!(
                    "Unresolved import: {}.{} (ordinal: {})",
                    import.dll_name,
                    import.name,
                    import.ordinal.map(|o| o.to_string()).unwrap_or_else(|| "-".to_string()),
                );
            }
        }

        let entry_point_va = base_address + pe.entry_point_rva as u64;

        let result = LoadResult {
            base_address,
            image_size,
            entry_point_va,
            sections_mapped,
            relocations_applied,
            imports_resolved: resolved_imports.len(),
            resolved_imports,
        };

        self.trace.record_pe_parse(
            &pe.path.to_string_lossy(),
            &pe.machine_type.to_string(),
            &pe.subsystem.to_string(),
            pe.entry_point_rva,
            pe.image_base,
            pe.sections.len(),
            pe.imports.len(),
            pe.relocations.len(),
        );

        self.trace.record_system(
            &format!(
                "Load complete: {} sections, {} relocations, {} imports resolved, entry at {:#018X}",
                result.sections_mapped,
                result.relocations_applied,
                result.imports_resolved,
                result.entry_point_va,
            ),
            None,
        );

        Ok(result)
    }

    /// Get mutable reference to the trace recorder.
    pub fn trace_mut(&mut self) -> &mut TraceRecorder {
        &mut self.trace
    }
}
