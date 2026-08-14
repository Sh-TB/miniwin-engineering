use crate::error::{Result, WinRuntimeError};
use crate::pe::{PeFile, SectionFlags};
use crate::trace::{TraceCategory, TraceLevel};
use std::collections::HashMap;
use std::ptr::null_mut;

/// Represents a mapped region of memory
#[derive(Debug)]
pub struct MemoryRegion {
    pub host_addr: usize,
    pub guest_addr: u64,
    pub size: usize,
    pub permissions: RegionPermissions,
    pub name: String,
}

#[derive(Debug, Clone, Copy)]
pub struct RegionPermissions {
    pub read: bool,
    pub write: bool,
    pub execute: bool,
}

impl std::fmt::Display for RegionPermissions {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut s = String::new();
        if self.read { s.push('R'); } else { s.push('-'); }
        if self.write { s.push('W'); } else { s.push('-'); }
        if self.execute { s.push('X'); } else { s.push('-'); }
        write!(f, "{}", s)
    }
}

impl RegionPermissions {
    pub fn from_section_flags(flags: SectionFlags) -> Self {
        Self {
            read: flags.contains(SectionFlags::MEM_READ),
            write: flags.contains(SectionFlags::MEM_WRITE),
            execute: flags.contains(SectionFlags::MEM_EXECUTE),
        }
    }

    pub fn rw() -> Self {
        Self { read: true, write: true, execute: false }
    }

    pub fn rwx() -> Self {
        Self { read: true, write: true, execute: true }
    }

    pub fn rx() -> Self {
        Self { read: true, write: false, execute: true }
    }

    pub fn to_prot_c_int(&self) -> libc::c_int {
        let mut prot = 0i32;
        if self.read { prot |= libc::PROT_READ; }
        if self.write { prot |= libc::PROT_WRITE; }
        if self.execute { prot |= libc::PROT_EXEC; }
        prot
    }
}

/// Guest-to-host memory mapping state
#[derive(Debug)]
pub struct MappedSection {
    pub guest_rva: u32,
    pub guest_addr: u64,
    pub host_addr: usize,
    pub size: usize,
    pub permissions: RegionPermissions,
}

/// Virtual memory manager for the Windows guest address space
pub struct MemoryManager {
    /// All allocated regions
    regions: Vec<MemoryRegion>,
    /// PE base address in guest space
    image_base: u64,
    /// Image size
    pub image_size: u64,
    /// Page size (host)
    page_size: usize,
    /// Mapped PE sections
    mapped_sections: Vec<MappedSection>,
    /// Import Address Table (IAT) mapped area
    iat_base: u64,
    iat_size: usize,
    iat_host_addr: usize,
    /// Heap area
    pub heap_base: u64,
    pub heap_size: usize,
    heap_host_addr: usize,
    /// Heap allocations tracker
    heap_allocations: HashMap<u64, usize>,
    /// Stack area (pre-allocated)
    pub stack_base: u64,
    pub stack_size: usize,
    stack_host_addr: usize,
}

impl MemoryManager {
    /// Default page size
    const DEFAULT_PAGE_SIZE: usize = 4096;

    /// Default stack size
    const STACK_SIZE: usize = 1024 * 1024; // 1MB

    /// Default heap size
    const HEAP_SIZE: usize = 4 * 1024 * 1024; // 4MB

    pub fn new() -> Self {
        Self {
            regions: Vec::new(),
            image_base: 0,
            image_size: 0,
            page_size: Self::DEFAULT_PAGE_SIZE,
            mapped_sections: Vec::new(),
            iat_base: 0,
            iat_size: 0,
            iat_host_addr: 0,
            heap_base: 0,
            heap_size: Self::HEAP_SIZE,
            heap_host_addr: 0,
            heap_allocations: HashMap::new(),
            stack_base: 0,
            stack_size: Self::STACK_SIZE,
            stack_host_addr: 0,
        }
    }

    /// Map a PE file into memory
    pub fn map_pe(&mut self, pe: &PeFile) -> Result<u64> {
        let image_size = pe.optional_header.size_of_image as u64;
        let image_base = pe.optional_header.image_base;
        let _section_align = pe.optional_header.section_alignment as u64;

        trace_event!(TraceCategory::Memory, TraceLevel::Info, "map_pe",
            format!("Mapping PE: base={:#018x}, size={:#010x}, sections={}",
                image_base, image_size, pe.sections.len()));

        // Allocate the full image region as RWX initially (we'll set section permissions later)
        let host_base = self.mmap_aligned(
            image_size as usize,
            libc::PROT_READ | libc::PROT_WRITE | libc::PROT_EXEC,
        )?;

        self.image_base = image_base;
        self.image_size = image_size;

        trace_event!(TraceCategory::Memory, TraceLevel::Info, "map_pe",
            format!("Image allocated: host={:#018x} guest={:#018x}",
                host_base as u64, image_base));

        // Map each section
        for section in &pe.sections {
            if section.size_of_raw_data == 0 && section.virtual_size == 0 {
                continue;
            }

            let guest_addr = image_base + section.virtual_address as u64;
            let raw_size = section.size_of_raw_data as usize;
            let virtual_size = section.virtual_size as usize;
            let offset_in_host = section.virtual_address as usize;

            // Copy raw data to mapped memory
            if raw_size > 0 {
                let section_data = pe.get_section_data(section)
                    .ok_or_else(|| WinRuntimeError::SectionNotFound(section.name.clone()))?;
                let dest = unsafe {
                    std::slice::from_raw_parts_mut(
                        (host_base + offset_in_host) as *mut u8,
                        raw_size.min(section_data.len()),
                    )
                };
                dest.copy_from_slice(&section_data[..raw_size.min(section_data.len())]);
            }

            // Zero remaining (BSS-like)
            if virtual_size > raw_size {
                unsafe {
                    std::ptr::write_bytes(
                        (host_base + offset_in_host + raw_size) as *mut u8,
                        0,
                        virtual_size - raw_size,
                    );
                }
            }

            let perms = RegionPermissions::from_section_flags(section.characteristics);
            self.mapped_sections.push(MappedSection {
                guest_rva: section.virtual_address,
                guest_addr,
                host_addr: host_base + offset_in_host,
                size: virtual_size.max(self.align_to_page(raw_size)),
                permissions: perms,
            });

            trace_event!(TraceCategory::Memory, TraceLevel::Trace, "map_pe",
                format!("Mapped section '{}': guest={:#018x} host={:#018x} size={:#010x} perms={}",
                    section.name, guest_addr, host_base + offset_in_host,
                    virtual_size, perms));
        }

        // Note: section permissions are NOT set here.
        // Call apply_permissions() after the IAT is written.

        // Allocate stack space
        let stack_size_aligned = self.align_to_page(Self::STACK_SIZE);
        self.stack_host_addr = self.mmap_anon(stack_size_aligned, libc::PROT_READ | libc::PROT_WRITE)?;
        // Stack grows downward, so stack_base is at the top
        self.stack_base = image_base + image_size + 0x10000; // place stack after image
        trace_event!(TraceCategory::Memory, TraceLevel::Info, "map_pe",
            format!("Stack allocated: host={:#018x} size={:#010x}",
                self.stack_host_addr as u64, stack_size_aligned));

        // Allocate heap space
        let heap_size_aligned = self.align_to_page(Self::HEAP_SIZE);
        self.heap_host_addr = self.mmap_anon(heap_size_aligned, libc::PROT_READ | libc::PROT_WRITE)?;
        self.heap_base = self.stack_base + stack_size_aligned as u64;
        trace_event!(TraceCategory::Memory, TraceLevel::Info, "map_pe",
            format!("Heap allocated: host={:#018x} size={:#010x}",
                self.heap_host_addr as u64, heap_size_aligned));

        Ok(image_base)
    }

    /// Map the Import Address Table (IAT)
    pub fn map_iat(&mut self, pe: &PeFile, _resolved_addresses: &HashMap<String, u64>) -> Result<u64> {
        if pe.optional_header.data_directories.len() <= crate::pe::IMAGE_DIRECTORY_ENTRY_IAT {
            return Ok(0);
        }
        let iat_dir = &pe.optional_header.data_directories[crate::pe::IMAGE_DIRECTORY_ENTRY_IAT];
        if iat_dir.virtual_address == 0 {
            return Ok(0);
        }

        // IAT is within the image, already mapped. We just need to write resolved addresses.
        let iat_size = iat_dir.size as usize;
        let iat_rva = iat_dir.virtual_address as usize;

        // The IAT is already mapped as part of the image; return its guest address
        self.iat_base = self.image_base + iat_dir.virtual_address as u64;
        self.iat_size = iat_size;
        self.iat_host_addr = self.image_host_base() as usize + iat_rva;

        trace_event!(TraceCategory::Memory, TraceLevel::Info, "map_iat",
            format!("IAT at guest={:#018x}, size={}", self.iat_base, iat_size));

        Ok(self.iat_base)
    }

    /// Allocate memory from the heap (simplified VirtualAlloc/HeapAlloc)
    pub fn heap_alloc(&mut self, size: usize) -> Result<u64> {
        let aligned_size = self.align_to_page(size.max(16));
        // Simple bump allocator from heap
        let offset = self.heap_allocations.len() * aligned_size;
        if offset + aligned_size > self.heap_size {
            return Err(WinRuntimeError::MemoryAllocation(format!(
                "Heap exhausted: requested {} bytes, heap size {}", size, self.heap_size
            )));
        }
        let guest_addr = self.heap_base + offset as u64;
        let host_addr = self.heap_host_addr + offset;
        // Zero the allocation
        unsafe {
            std::ptr::write_bytes(host_addr as *mut u8, 0, aligned_size);
        }
        self.heap_allocations.insert(guest_addr, aligned_size);
        trace_event!(TraceCategory::Memory, TraceLevel::Trace, "heap_alloc",
            format!("HeapAlloc: guest={:#018x} size={}", guest_addr, aligned_size));
        Ok(guest_addr)
    }

    /// Free heap memory (simplified)
    pub fn heap_free(&mut self, addr: u64) -> Result<()> {
        if self.heap_allocations.remove(&addr).is_some() {
            trace_event!(TraceCategory::Memory, TraceLevel::Trace, "heap_free",
                format!("HeapFree: guest={:#018x}", addr));
            Ok(())
        } else {
            Err(WinRuntimeError::InvalidAddress { address: addr })
        }
    }

    /// Convert guest address to host address
    pub fn guest_to_host(&self, guest_addr: u64) -> Result<usize> {
        if guest_addr >= self.image_base && guest_addr < self.image_base + self.image_size {
            let offset = (guest_addr - self.image_base) as usize;
            return Ok(self.image_host_base() as usize + offset);
        }
        // Stack
        if guest_addr >= self.stack_base && guest_addr < self.stack_base + self.stack_size as u64 {
            let offset = (guest_addr - self.stack_base) as usize;
            return Ok(self.stack_host_addr + offset);
        }
        // Heap
        if guest_addr >= self.heap_base && guest_addr < self.heap_base + self.heap_size as u64 {
            let offset = (guest_addr - self.heap_base) as usize;
            return Ok(self.heap_host_addr + offset);
        }
        Err(WinRuntimeError::InvalidAddress { address: guest_addr })
    }

    /// Convert host address back to guest address
    pub fn host_to_guest(&self, host_addr: usize) -> Result<u64> {
        let image_host = self.image_host_base() as usize;
        if host_addr >= image_host && host_addr < image_host + self.image_size as usize {
            return Ok(self.image_base + (host_addr - image_host) as u64);
        }
        Err(WinRuntimeError::InvalidAddress { address: host_addr as u64 })
    }

    /// Get image host base address
    pub fn image_host_base(&self) -> u64 {
        self.regions.first()
            .map(|r| r.host_addr as u64)
            .unwrap_or(0)
    }

    /// Read bytes from guest memory
    pub fn read_guest_memory(&self, guest_addr: u64, size: usize) -> Result<Vec<u8>> {
        let host_addr = self.guest_to_host(guest_addr)?;
        let data = unsafe { std::slice::from_raw_parts(host_addr as *const u8, size) };
        Ok(data.to_vec())
    }

    /// Write bytes to guest memory
    pub fn write_guest_memory(&self, guest_addr: u64, data: &[u8]) -> Result<()> {
        let host_addr = self.guest_to_host(guest_addr)?;
        unsafe {
            std::ptr::copy_nonoverlapping(data.as_ptr(), host_addr as *mut u8, data.len());
        }
        Ok(())
    }

    /// Read a u64 from guest memory
    pub fn read_guest_u64(&self, guest_addr: u64) -> Result<u64> {
        let host_addr = self.guest_to_host(guest_addr)?;
        let data = unsafe { std::slice::from_raw_parts(host_addr as *const u8, 8) };
        Ok(u64::from_le_bytes(data.try_into()?))
    }

    /// Write a u64 to guest memory
    pub fn write_guest_u64(&self, guest_addr: u64, value: u64) -> Result<()> {
        self.write_guest_memory(guest_addr, &value.to_le_bytes())
    }

    /// Read a u32 from guest memory
    pub fn read_guest_u32(&self, guest_addr: u64) -> Result<u32> {
        let host_addr = self.guest_to_host(guest_addr)?;
        let data = unsafe { std::slice::from_raw_parts(host_addr as *const u8, 4) };
        Ok(u32::from_le_bytes(data.try_into()?))
    }

    /// Write a u32 to guest memory
    pub fn write_guest_u32(&self, guest_addr: u64, value: u32) -> Result<()> {
        self.write_guest_memory(guest_addr, &value.to_le_bytes())
    }

    /// Get all mapped regions info
    pub fn regions_info(&self) -> Vec<(u64, usize, &str)> {
        let mut info = Vec::new();
        for ms in &self.mapped_sections {
            info.push((ms.guest_addr, ms.size, "section"));
        }
        info.push((self.stack_base, self.stack_size, "stack"));
        info.push((self.heap_base, self.heap_size, "heap"));
        info
    }

    /// Apply section permissions (call after IAT is written)
    pub fn apply_permissions(&self) -> Result<()> {
        let host_base = self.image_host_base() as usize;
        let header_size = 0x400;
        // Set headers to read-only
        self.set_permissions(host_base, header_size, RegionPermissions { read: true, write: false, execute: false })?;
        // Set section permissions
        for ms in &self.mapped_sections {
            self.set_permissions(host_base + ms.guest_rva as usize, ms.size, ms.permissions)?;
        }
        trace_event!(TraceCategory::Memory, TraceLevel::Info, "permissions",
            "Section permissions applied");
        Ok(())
    }

    /// Memory layout summary
    pub fn layout_summary(&self) -> String {
        let mut s = format!("=== Memory Layout ===\n");
        s.push_str(&format!("Image Base:   {:#018x}\n", self.image_base));
        s.push_str(&format!("Image Size:   {:#010x}\n", self.image_size));
        s.push_str(&format!("Image Host:   {:#018x}\n", self.image_host_base()));
        s.push_str(&format!("Stack:        {:#018x} ({:#010x})\n", self.stack_base, self.stack_size));
        s.push_str(&format!("Heap:         {:#018x} ({:#010x})\n", self.heap_base, self.heap_size));

        for ms in &self.mapped_sections {
            s.push_str(&format!("  Section '{}': guest={:#018x} host={:#018x} size={:#010x} {}\n",
                "", /* We'd need section name here - use mapped_sections */
                ms.guest_addr, ms.host_addr as u64, ms.size, ms.permissions));
        }
        s
    }

    // === Private helpers ===

    fn mmap_aligned(&mut self, size: usize, prot_flags: libc::c_int) -> Result<usize> {
        let aligned_size = self.align_to_page(size);
        let ptr = unsafe {
            libc::mmap(
                null_mut(),
                aligned_size,
                prot_flags,
                libc::MAP_PRIVATE | libc::MAP_ANONYMOUS,
                -1,
                0,
            )
        };

        if ptr == libc::MAP_FAILED {
            return Err(WinRuntimeError::MemoryAllocation(format!(
                "mmap failed for size {}", aligned_size
            )));
        }

        let addr = ptr as usize;
        self.regions.push(MemoryRegion {
            host_addr: addr,
            guest_addr: 0,
            size: aligned_size,
            permissions: RegionPermissions { read: true, write: true, execute: true },
            name: "image".to_string(),
        });

        trace_event!(TraceCategory::Memory, TraceLevel::Trace, "mmap",
            format!("mmap: host={:#018x} size={:#010x}", addr as u64, aligned_size));
        Ok(addr)
    }

    fn mmap_anon(&mut self, size: usize, prot_flags: libc::c_int) -> Result<usize> {
        let ptr = unsafe {
            libc::mmap(
                null_mut(),
                size,
                prot_flags,
                libc::MAP_PRIVATE | libc::MAP_ANONYMOUS,
                -1,
                0,
            )
        };

        if ptr == libc::MAP_FAILED {
            return Err(WinRuntimeError::MemoryAllocation(format!(
                "mmap failed for size {}", size
            )));
        }

        let addr = ptr as usize;
        trace_event!(TraceCategory::Memory, TraceLevel::Trace, "mmap_anon",
            format!("mmap_anon: host={:#018x} size={:#010x}", addr as u64, size));
        Ok(addr)
    }

    fn set_permissions(&self, host_addr: usize, size: usize, perms: RegionPermissions) -> Result<()> {
        let aligned_addr = host_addr & !(self.page_size - 1);
        let aligned_size = self.align_to_page(size + (host_addr - aligned_addr));
        let result = unsafe {
            libc::mprotect(
                aligned_addr as *mut libc::c_void,
                aligned_size,
                perms.to_prot_c_int(),
            )
        };
        if result != 0 {
            return Err(WinRuntimeError::MemoryProtection(format!(
                "mprotect failed for addr={:#018x}", aligned_addr as u64
            )));
        }
        Ok(())
    }

    fn align_to_page(&self, size: usize) -> usize {
        (size + self.page_size - 1) & !(self.page_size - 1)
    }
}
