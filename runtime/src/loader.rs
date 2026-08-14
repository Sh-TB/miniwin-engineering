use crate::error::{Result, WinRuntimeError};
use crate::mem::MemoryManager;
use crate::pe::{PeFile, RelocationType};
use crate::trace::{TraceCategory, TraceLevel};
use crate::win32::Win32Dispatcher;
use std::collections::HashMap;
use std::path::{Path, PathBuf};

/// Main runtime state
pub struct Runtime {
    pe: Option<PeFile>,
    mem: MemoryManager,
    dispatcher: Win32Dispatcher,
    resolved_imports: HashMap<String, u64>,
    image_base: u64,
    entry_point: u64,
    trace_log_dir: PathBuf,
    replay_dir: PathBuf,
    /// Maximum instructions to execute before timeout
    max_instructions: u64,
}

impl Runtime {
    pub fn new() -> Self {
        let log_dir = PathBuf::from("/home/z/my-project/runtime/logs");
        let replay_dir = PathBuf::from("/home/z/my-project/runtime/replays");
        Self {
            pe: None,
            mem: MemoryManager::new(),
            dispatcher: Win32Dispatcher::new(),
            resolved_imports: HashMap::new(),
            image_base: 0,
            entry_point: 0,
            trace_log_dir: log_dir,
            replay_dir,
            max_instructions: 1_000_000,
        }
    }

    /// Load a PE file
    pub fn load_pe(&mut self, path: &Path) -> Result<()> {
        trace_event!(TraceCategory::System, TraceLevel::Info, "runtime",
            format!("Loading PE file: {}", path.display()));

        let data = std::fs::read(path)?;
        let pe = PeFile::parse(data)?;

        if !pe.is_64bit {
            trace_event!(TraceCategory::System, TraceLevel::Warn, "runtime",
                "PE is not x64. Only x64 is fully supported.");
        }

        let summary = pe.summary();
        trace_event!(TraceCategory::PeParse, TraceLevel::Info, "runtime",
            format!("PE loaded successfully:\n{}", summary));

        self.pe = Some(pe);
        Ok(())
    }

    /// Map the loaded PE into memory and resolve imports
    pub fn map_and_resolve(&mut self) -> Result<()> {
        if self.pe.is_none() {
            return Err(WinRuntimeError::NotInitialized);
        }

        // Map PE into memory
        let base = self.mem.map_pe(self.pe.as_ref().unwrap())?;
        self.image_base = base;

        trace_event!(TraceCategory::Memory, TraceLevel::Info, "runtime",
            format!("PE mapped at base {:#018x}", base));

        // Apply relocations (only if image was loaded at a different address)
        let delta = base.wrapping_sub(self.pe.as_ref().unwrap().image_base()) as i64;
        if delta != 0 {
            Self::apply_relocations(&mut self.mem, self.image_base, self.pe.as_ref().unwrap(), delta)?;
        } else {
            trace_event!(TraceCategory::Relocation, TraceLevel::Info, "runtime",
                "No relocations needed (loaded at preferred base)");
        }

        // Resolve imports and write IAT (before setting permissions)
        self.resolved_imports = self.dispatcher.resolve_imports(self.pe.as_ref().unwrap(), &mut self.mem)?;
        Self::write_iat(&mut self.mem, self.image_base, &self.resolved_imports, self.pe.as_ref().unwrap())?;

        // Now apply section permissions (R-X for code, R-- for data)
        self.mem.apply_permissions()?;

        // Set entry point
        self.entry_point = base + self.pe.as_ref().unwrap().entry_point_rva() as u64;

        trace_event!(TraceCategory::System, TraceLevel::Info, "runtime",
            format!("Entry point: {:#018x}", self.entry_point));

        Ok(())
    }

    /// Apply base relocations
    fn apply_relocations(mem: &mut MemoryManager, image_base: u64, pe: &PeFile, delta: i64) -> Result<()> {
        trace_event!(TraceCategory::Relocation, TraceLevel::Info, "runtime",
            format!("Applying relocations, delta={}", delta));

        for block in &pe.relocations {
            let page_guest_addr = image_base + block.page_rva as u64;

            for entry in &block.entries {
                let reloc_addr = page_guest_addr + entry.offset as u64;
                let host_addr = mem.guest_to_host(reloc_addr)?;

                match entry.typ {
                    RelocationType::Dir64 => {
                        // 64-bit relocation
                        let original = unsafe { std::ptr::read(host_addr as *const u64) };
                        let new_value = (original as i64 + delta) as u64;
                        unsafe { std::ptr::write(host_addr as *mut u64, new_value); }
                    }
                    RelocationType::HighLow => {
                        // 32-bit relocation
                        let original = unsafe { std::ptr::read(host_addr as *const u32) };
                        let new_value = (original as i32 + delta as i32) as u32;
                        unsafe { std::ptr::write(host_addr as *mut u32, new_value); }
                    }
                    RelocationType::High => {
                        let original = unsafe { std::ptr::read(host_addr as *const u16) };
                        let new_value = ((original as i32 + delta as i32) >> 16) as u16;
                        unsafe { std::ptr::write(host_addr as *mut u16, new_value); }
                    }
                    RelocationType::Low => {
                        let original = unsafe { std::ptr::read(host_addr as *const u16) };
                        let new_value = (original as i16 + delta as i16) as u16;
                        unsafe { std::ptr::write(host_addr as *mut u16, new_value); }
                    }
                    RelocationType::HighAdj => {
                        // Need next entry for adj value, skip for now
                    }
                    RelocationType::Absolute => {
                        // Padding, skip
                    }
                }
            }
        }

        trace_event!(TraceCategory::Relocation, TraceLevel::Info, "runtime",
            format!("Applied {} relocation blocks", pe.relocations.len()));
        Ok(())
    }

    /// Write resolved import addresses to the Import Address Table (IAT)
    fn write_iat(mem: &mut MemoryManager, image_base: u64, resolved_imports: &HashMap<String, u64>, pe: &PeFile) -> Result<()> {
        for import in &pe.imports {
            let dll_lower = import.dll_name.to_lowercase();
            let iat_rva = import.first_thunk;
            if iat_rva == 0 {
                continue;
            }

            for (i, func) in import.functions.iter().enumerate() {
                let func_name = match func {
                    crate::pe::ImportFunction::ByName(name) => name.clone(),
                    crate::pe::ImportFunction::ByOrdinal(ord) => format!("Ordinal_{}", ord),
                };
                let full_name = format!("{}.{}", dll_lower, func_name);

                let resolved_addr = resolved_imports.get(&full_name)
                    .copied()
                    .unwrap_or(0);

                let entry_guest_addr = image_base + iat_rva as u64
                    + (i as u64 * if pe.is_64bit { 8 } else { 4 });

                if pe.is_64bit {
                    mem.write_guest_u64(entry_guest_addr, resolved_addr)?;
                } else {
                    mem.write_guest_u32(entry_guest_addr, resolved_addr as u32)?;
                }

                trace_event!(TraceCategory::Import, TraceLevel::Trace, "iat",
                    format!("IAT[{}]: {} -> {:#018x}", i, full_name, resolved_addr));
            }
        }

        Ok(())
    }

    /// Execute the loaded PE (interpretation mode)
    ///
    /// This is a CPU interpreter that reads x86_64 instructions and executes them.
    /// For a first prototype, we handle a limited set of instructions.
    pub fn execute(&mut self) -> Result<ExecutionResult> {
        trace_event!(TraceCategory::Execution, TraceLevel::Info, "runtime",
            format!("Starting execution at {:#018x}", self.entry_point));

        let pe = self.pe.as_ref().ok_or(WinRuntimeError::NotInitialized)?;
        let mut rip = self.entry_point;
        let mut instruction_count = 0u64;
        let mut exit_code = 0u32;
        let mut exited = false;
        let mut exit_reason = String::from("running");

        // Register state
        let mut regs: [u64; 16] = [0; 16]; // rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15
        let mut rflags: u64 = 0x200; // IF flag set

        // Set up stack
        regs[4] = self.mem.stack_base as u64 + self.mem.stack_size as u64 - 16; // RSP

        // CPUID feature detection result
        let mut cpu_features: HashMap<u32, (u32, u32, u32, u32)> = HashMap::new();
        // Standard CPUID features
        cpu_features.insert(0, (0x16, 0x756E6547, 0x6C65746E, 0x49656E69)); // max leaf, vendor
        cpu_features.insert(1, (
            0x80000001, // EAX: version info
            0x00100800, // EBX: brand index, CLFLUSH, SSE, SSE2
            0x00003FEB, // ECX: SSE3, SSSE3, etc.
            0x00000000, // EDX: no FPU emulation
        ));

        // Decode and execute loop

        while instruction_count < self.max_instructions && !exited {
            // Check if we hit an API trampoline
            if let Some((name, _addr)) = self.resolved_imports.iter()
                .find(|(_, &addr)| addr == rip) {
                // Extract arguments (simplified: assume RCX, RDX, R8, R9)
                let args = [regs[1], regs[2], regs[8], regs[9]];
                trace_event!(TraceCategory::Execution, TraceLevel::Info, "interpreter",
                    format!("API CALL: {} (RCX={:#x}, RDX={:#x}, R8={:#x}, R9={:#x})",
                        name, args[0], args[1], args[2], args[3]));
                let result = self.dispatcher.dispatch(name, &args, &self.mem)?;
                regs[0] = result; // RAX = return value
                // After an API call, simulate a RET
                if regs[4] >= self.mem.stack_base as u64 {
                    let ret_addr = self.mem.read_guest_u64(regs[4])?;
                    rip = ret_addr;
                    regs[4] += 8;
                }
                if self.dispatcher.has_exited() {
                    exited = true;
                    exit_code = self.dispatcher.get_exit_code().unwrap_or(0);
                    exit_reason = format!("ExitProcess({})", exit_code);
                    break;
                }
                instruction_count += 1;
                continue;
            }

            // Read bytes at current RIP
            let bytes = match self.mem.read_guest_memory(rip, 15) {
                Ok(b) => b,
                Err(e) => {
                    exit_reason = format!("Memory read error at RIP={:#018x}: {}", rip, e);
                    trace_event!(TraceCategory::Execution, TraceLevel::Error, "interpreter",
                        &exit_reason);
                    break;
                }
            };

            let (opcode, instruction_len, opcode_offset) = Self::decode_instruction(&bytes);
            if instruction_count <= 5 || opcode == 0xFF {
                eprintln!("[DEBUG] bytes=[{:02x?}] opcode={:02x} len={} opcode_offset={}", &bytes[..bytes.len().min(15)], opcode, instruction_len, opcode_offset);
            }
            instruction_count += 1;

            // Slice bytes to start at the actual opcode (after REX prefix)
            let opcode_bytes = &bytes[opcode_offset..];
            // Extract REX prefix byte (0 if no REX)
            let rex = if opcode_offset > 0 && bytes[0] >= 0x40 && bytes[0] <= 0x4F {
                bytes[0]
            } else {
                0
            };
            let rex_w = ((rex >> 3) & 1) != 0;
            let rex_r = ((rex >> 2) & 1) != 0;
            let rex_b = (rex & 1) != 0;
            // REX register extension values (0 or 8)
            let rex_r_ext: usize = if rex_r { 8 } else { 0 };
            let rex_b_ext: usize = if rex_b { 8 } else { 0 };

            // Print every Nth instruction for tracing
            if instruction_count % 1000 == 0 || instruction_count <= 50 {
                trace_event!(TraceCategory::Execution, TraceLevel::Trace, "interpreter",
                    format!("[{}] RIP={:#018x} opcode={:02x} rex={:02x} len={}",
                        instruction_count, rip, opcode, rex, instruction_len));
            }

            match opcode {
                // NOP
                0x90 => {
                    rip += 1;
                }

                // RET (near)
                0xC3 => {
                    if regs[4] >= self.mem.stack_base as u64
                        && regs[4] < self.mem.stack_base as u64 + self.mem.stack_size as u64 {
                        let ret_addr = self.mem.read_guest_u64(regs[4])?;
                        trace_event!(TraceCategory::Execution, TraceLevel::Trace, "interpreter",
                            format!("RET to {:#018x}", ret_addr));
                        rip = ret_addr;
                        regs[4] += 8;
                    } else {
                        exit_reason = format!("RET with invalid RSP: {:#018x}", regs[4]);
                        break;
                    }
                }

                // RET imm16
                0xC2 => {
                    if opcode_bytes.len() >= 3 {
                        let imm = u16::from_le_bytes([opcode_bytes[1], opcode_bytes[2]]) as u64;
                        let ret_addr = self.mem.read_guest_u64(regs[4])?;
                        rip = ret_addr;
                        regs[4] += 8 + imm;
                    } else {
                        rip += 3;
                    }
                }

                // CALL rel32
                0xE8 => {
                    if instruction_len >= 5 {
                        let rel = i32::from_le_bytes([
                            opcode_bytes[1], opcode_bytes[2],
                            opcode_bytes[3], opcode_bytes[4],
                        ]) as i64;
                        let next_rip = rip + instruction_len;
                        // Push return address
                        regs[4] -= 8;
                        self.mem.write_guest_u64(regs[4], next_rip)?;
                        rip = (rip as i64 + rel) as u64;
                        trace_event!(TraceCategory::Execution, TraceLevel::Trace, "interpreter",
                            format!("CALL rel32 to {:#018x}", rip));
                    } else {
                        rip += instruction_len;
                    }
                }

                // CALL [rip+disp32] (FF 15 ...), JMP [rip+disp32] (FF 25 ...), CALL r/m64 (FF /2)
                0xFF => {
                    if opcode_bytes.len() >= 6 && opcode_bytes[1] == 0x15 {
                        // CALL [rip+disp32]
                        let disp = i32::from_le_bytes([
                            opcode_bytes[2], opcode_bytes[3],
                            opcode_bytes[4], opcode_bytes[5],
                        ]) as i64;
                        let next_rip = rip + instruction_len;
                        let target_addr = next_rip as u64 + disp as u64;
                        let target = self.mem.read_guest_u64(target_addr)?;

                        trace_event!(TraceCategory::Execution, TraceLevel::Info, "interpreter",
                            format!("CALL [rip+{:#x}] -> [{:#018x}] = {:#018x}", disp, target_addr, target));

                        // Check if target is an API trampoline
                        if let Some((name, _addr)) = self.resolved_imports.iter()
                            .find(|(_, &addr)| addr == target) {
                            let args = [regs[1], regs[2], regs[8], regs[9]];
                            let result = self.dispatcher.dispatch(name, &args, &self.mem)?;
                            regs[0] = result;
                            rip = next_rip;
                            if self.dispatcher.has_exited() {
                                exited = true;
                                exit_code = self.dispatcher.get_exit_code().unwrap_or(0);
                                exit_reason = format!("ExitProcess({})", exit_code);
                                break;
                            }
                        } else {
                            // Indirect call - push return and jump
                            regs[4] -= 8;
                            self.mem.write_guest_u64(regs[4], next_rip)?;
                            rip = target;
                        }
                    } else if opcode_bytes.len() >= 6 && opcode_bytes[1] == 0x25 {
                        // JMP [rip+disp32] (FF 25 ...)
                        let disp = i32::from_le_bytes([
                            opcode_bytes[2], opcode_bytes[3],
                            opcode_bytes[4], opcode_bytes[5],
                        ]) as i64;
                        let next_rip = rip + instruction_len;
                        let target_addr = next_rip as u64 + disp as u64;
                        let target = self.mem.read_guest_u64(target_addr)?;
                        trace_event!(TraceCategory::Execution, TraceLevel::Trace, "interpreter",
                            format!("JMP [rip+{:#x}] -> {:#018x}", disp, target));
                        rip = target;
                    } else if opcode_bytes.len() >= 2 {
                        // Other FF variants: INC/DEC/CALL/JMP/PUSH r/m
                        let modrm = opcode_bytes[1];
                        let reg_op = (modrm >> 3) & 7;
                        let rm_base = (modrm & 7) as usize;
                        let rm = rm_base | rex_b_ext;
                        if modrm >= 0xC0 {
                            // Register direct
                            match reg_op {
                                2 => {
                                    // CALL r/m64
                                    let target = regs[rm as usize];
                                    let next_rip = rip + instruction_len;
                                    regs[4] -= 8;
                                    self.mem.write_guest_u64(regs[4], next_rip)?;
                                    trace_event!(TraceCategory::Execution, TraceLevel::Trace, "interpreter",
                                        format!("CALL r{} to {:#018x}", rm, target));
                                    rip = target;
                                }
                                4 => {
                                    // JMP r/m64
                                    let target = regs[rm as usize];
                                    trace_event!(TraceCategory::Execution, TraceLevel::Trace, "interpreter",
                                        format!("JMP r{} to {:#018x}", rm, target));
                                    rip = target;
                                }
                                0 => {
                                    // INC r/m64
                                    regs[rm as usize] = regs[rm as usize].wrapping_add(1);
                                }
                                1 => {
                                    // DEC r/m64
                                    regs[rm as usize] = regs[rm as usize].wrapping_sub(1);
                                }
                                6 => {
                                    // PUSH r/m64
                                    regs[4] -= 8;
                                    self.mem.write_guest_u64(regs[4], regs[rm as usize])?;
                                }
                                _ => {
                                    rip += instruction_len;
                                }
                            }
                        } else {
                            rip += instruction_len;
                        }
                    } else {
                        rip += instruction_len;
                    }
                }

                // JMP rel8
                0xEB => {
                    if opcode_bytes.len() >= 2 {
                        let rel = opcode_bytes[1] as i8 as i64;
                        rip = (rip as i64 + rel + 2) as u64;
                    } else {
                        rip += 2;
                    }
                }

                // JMP rel32
                0xE9 => {
                    if instruction_len >= 5 {
                        let rel = i32::from_le_bytes([
                            opcode_bytes[1], opcode_bytes[2],
                            opcode_bytes[3], opcode_bytes[4],
                        ]) as i64;
                        rip = (rip as i64 + rel + instruction_len as i64) as u64;
                        trace_event!(TraceCategory::Execution, TraceLevel::Trace, "interpreter",
                            format!("JMP rel32 to {:#018x}", rip));
                    } else {
                        rip += instruction_len;
                    }
                }

                // JE/JZ rel32 (0F 84)
                // JNE/JNZ rel32 (0F 85)
                0x0F => {
                    if opcode_bytes.len() >= 2 {
                        let sub_opcode = opcode_bytes[1];
                        match sub_opcode {
                            // CPUID (0F A2)
                            0xA2 => {
                                let leaf = regs[0] as u32;
                                if let Some(&(eax, ebx, ecx, edx)) = cpu_features.get(&leaf) {
                                    regs[0] = eax as u64;
                                    regs[1] = ebx as u64;
                                    regs[2] = ecx as u64;
                                    regs[3] = edx as u64;
                                } else {
                                    regs[0] = 0;
                                    regs[1] = 0;
                                    regs[2] = 0;
                                    regs[3] = 0;
                                }
                                rip += instruction_len;
                            }
                            // UD2 (0F 0B)
                            0x0B => {
                                exit_reason = format!("UD2 at {:#018x}", rip);
                                break;
                            }
                            // Jcc rel32 (0F 80-8F)
                            0x80..=0x8F if opcode_bytes.len() >= 6 => {
                                let rel = i32::from_le_bytes([
                                    opcode_bytes[2], opcode_bytes[3],
                                    opcode_bytes[4], opcode_bytes[5],
                                ]) as i64;
                                let zf = (rflags & 0x40) != 0;
                                let sf = (rflags & 0x80) != 0;
                                let cf = (rflags & 0x01) != 0;
                                let of = (rflags & 0x800) != 0;

                                let take = match sub_opcode {
                                    0x84 => zf, // JE/JZ
                                    0x85 => !zf, // JNE/JNZ
                                    0x82 => cf, // JB/JNAE
                                    0x83 => !cf, // JAE/JNB
                                    0x86 => cf || zf, // JBE/JNA
                                    0x87 => !cf && !zf, // JA/JNBE
                                    0x8C => sf != of, // JL/JNGE
                                    0x8D => sf == of, // JGE/JNL
                                    0x8E => of || (zf && (sf != of)), // JLE/JNG
                                    0x8F => !of && !(zf && (sf != of)), // JG/JNLE
                                    _ => false,
                                };

                                if take {
                                    rip = (rip as i64 + rel + instruction_len as i64) as u64;
                                } else {
                                    rip += instruction_len;
                                }
                            }
                            _ => {
                                rip += instruction_len;
                            }
                        }
                    } else {
                        rip += instruction_len;
                    }
                }

                // JE/JZ rel8 (74)
                0x74 => {
                    if opcode_bytes.len() >= 2 {
                        let zf = (rflags & 0x40) != 0;
                        let rel = opcode_bytes[1] as i8 as i64;
                        if zf {
                            rip = (rip as i64 + rel + 2) as u64;
                        } else {
                            rip += 2;
                        }
                    } else { rip += 2; }
                }

                // JNE/JNZ rel8 (75)
                0x75 => {
                    if opcode_bytes.len() >= 2 {
                        let zf = (rflags & 0x40) != 0;
                        let rel = opcode_bytes[1] as i8 as i64;
                        if !zf {
                            rip = (rip as i64 + rel + 2) as u64;
                        } else {
                            rip += 2;
                        }
                    } else { rip += 2; }
                }

                // MOV r32, imm32 (B8+rd) / MOV r64, imm32 sign-ext (REX.W + B8+rd)
                // Always 32-bit immediate; REX.W controls sign-extension to 64 bits
                0xB8..=0xBF => {
                    let reg_base = (opcode & 0x07) as usize;
                    let reg_idx = reg_base | rex_b_ext;
                    if opcode_bytes.len() >= 5 {
                        let imm32 = u32::from_le_bytes([
                            opcode_bytes[1], opcode_bytes[2],
                            opcode_bytes[3], opcode_bytes[4],
                        ]);
                        regs[reg_idx] = if rex_w {
                            (imm32 as i32) as u64 // sign-extend to 64-bit
                        } else {
                            imm32 as u64 // zero-extend to 64-bit
                        };
                    }
                    rip += instruction_len;
                }



                // MOV r/m, reg (89) — With REX.W: MOV r/m64, r64
                0x89 => {
                    if opcode_bytes.len() >= 2 {
                        let modrm = opcode_bytes[1];
                        let reg_src = (((modrm >> 3) & 7) as usize) | rex_r_ext;
                        let rm_dst_base = (modrm & 7) as usize;
                        let rm_dst = rm_dst_base | rex_b_ext;
                        if modrm >= 0xC0 {
                            // Register direct: MOV reg, reg
                            regs[rm_dst] = regs[reg_src];
                        } else if modrm == 0x05 && opcode_bytes.len() >= 6 {
                            // MOV [rip+disp32], reg
                            let disp = i32::from_le_bytes([
                                opcode_bytes[2], opcode_bytes[3],
                                opcode_bytes[4], opcode_bytes[5],
                            ]) as i64;
                            let target = rip + instruction_len + disp as u64;
                            if rex_w {
                                self.mem.write_guest_u64(target, regs[reg_src])?;
                            } else {
                                self.mem.write_guest_u32(target, regs[reg_src] as u32)?;
                            }
                        } else if modrm == 0x04 && opcode_bytes.len() >= 3 {
                            // SIB byte follows
                            let sib = opcode_bytes[2];
                            let base = (sib & 7) as usize;
                            let scale = (sib >> 6) & 3;
                            let index = ((sib >> 3) & 7) as usize;
                            let mut addr = regs[base];
                            if index != 4 {
                                addr = addr.wrapping_add(regs[index] << scale);
                            }
                            let mod_val = (modrm >> 6) & 3;
                            let mut extra = 3; // past opcode + modrm + sib
                            if mod_val == 1 && opcode_bytes.len() > extra {
                                addr = addr.wrapping_add(opcode_bytes[extra] as i8 as i64 as u64);
                            } else if mod_val == 2 && opcode_bytes.len() > extra + 3 {
                                let d = i32::from_le_bytes([
                                    opcode_bytes[extra], opcode_bytes[extra+1],
                                    opcode_bytes[extra+2], opcode_bytes[extra+3],
                                ]) as i64 as u64;
                                addr = addr.wrapping_add(d);
                            }
                            if rex_w {
                                self.mem.write_guest_u64(addr, regs[reg_src])?;
                            } else {
                                self.mem.write_guest_u32(addr, regs[reg_src] as u32)?;
                            }
                        }
                    }
                    rip += instruction_len;
                }

                // MOV reg, r/m (8B) — With REX.W: MOV r64, r/m64
                0x8B => {
                    if opcode_bytes.len() >= 2 {
                        let modrm = opcode_bytes[1];
                        let reg_dst = (((modrm >> 3) & 7) as usize) | rex_r_ext;
                        let rm_src_base = (modrm & 7) as usize;
                        let rm_src = rm_src_base | rex_b_ext;
                        if modrm >= 0xC0 {
                            regs[reg_dst] = regs[rm_src];
                        } else if modrm == 0x05 && opcode_bytes.len() >= 6 {
                            // RIP-relative: MOV reg, [rip+disp32]
                            let disp = i32::from_le_bytes([
                                opcode_bytes[2], opcode_bytes[3],
                                opcode_bytes[4], opcode_bytes[5],
                            ]) as i64;
                            let target = rip + instruction_len + disp as u64;
                            if rex_w {
                                let value = self.mem.read_guest_u64(target)?;
                                regs[reg_dst] = value;
                            } else {
                                let value = self.mem.read_guest_u32(target)?;
                                regs[reg_dst] = value as u64;
                            }
                        } else if modrm == 0x04 && opcode_bytes.len() >= 3 {
                            // SIB byte
                            let sib = opcode_bytes[2];
                            let base = (sib & 7) as usize;
                            let scale = (sib >> 6) & 3;
                            let index = ((sib >> 3) & 7) as usize;
                            let mut addr = regs[base];
                            if index != 4 {
                                addr = addr.wrapping_add(regs[index] << scale);
                            }
                            let mod_val = (modrm >> 6) & 3;
                            let mut extra = 3;
                            if mod_val == 1 && opcode_bytes.len() > extra {
                                addr = addr.wrapping_add(opcode_bytes[extra] as i8 as i64 as u64);
                            } else if mod_val == 2 && opcode_bytes.len() > extra + 3 {
                                let d = i32::from_le_bytes([
                                    opcode_bytes[extra], opcode_bytes[extra+1],
                                    opcode_bytes[extra+2], opcode_bytes[extra+3],
                                ]) as i64 as u64;
                                addr = addr.wrapping_add(d);
                            }
                            if rex_w {
                                let value = self.mem.read_guest_u64(addr)?;
                                regs[reg_dst] = value;
                            } else {
                                let value = self.mem.read_guest_u32(addr)?;
                                regs[reg_dst] = value as u64;
                            }
                        }
                    }
                    rip += instruction_len;
                }

                // XOR reg, r/m (33) — With REX.W: XOR r64, r/m64
                0x33 => {
                    if opcode_bytes.len() >= 2 {
                        let modrm = opcode_bytes[1];
                        let reg1 = (((modrm >> 3) & 7) as usize) | rex_r_ext;
                        let reg2 = ((modrm & 7) as usize) | rex_b_ext;
                        if modrm >= 0xC0 {
                            regs[reg1] ^= regs[reg2];
                            if rex_w {
                                // 64-bit: result already in place
                            } else {
                                regs[reg1] &= 0xFFFFFFFF;
                            }
                            if regs[reg1] == 0 {
                                rflags |= 0x40;
                            } else {
                                rflags &= !0x40;
                            }
                        }
                    }
                    rip += instruction_len;
                }

                // ALU r/m, imm8 (83) — ADD/OR/ADC/SBB/AND/SUB/XOR/CMP r/m, imm8
                // With REX.W: operates on 64-bit registers
                0x83 => {
                    if opcode_bytes.len() >= 3 {
                        let modrm = opcode_bytes[1];
                        let reg_op = (modrm >> 3) & 7;
                        let rm_base = (modrm & 7) as usize;
                        let rm = rm_base | rex_b_ext;
                        let imm = opcode_bytes[2] as i8 as i64;
                        if modrm >= 0xC0 {
                            // Register-direct
                            let val = regs[rm as usize] as i64;
                            match reg_op {
                                0 => { // ADD
                                    regs[rm as usize] = val.wrapping_add(imm) as u64;
                                    rflags = Self::update_flags_arithmetic(val.wrapping_add(imm), val, imm, 8);
                                }
                                1 => { // OR
                                    regs[rm as usize] = (val | imm) as u64;
                                    rflags = Self::update_flags_arithmetic(val | imm, val, imm, 8);
                                }
                                4 => { // AND
                                    regs[rm as usize] = (val & imm) as u64;
                                    rflags = Self::update_flags_arithmetic(val & imm, val, imm, 8);
                                }
                                5 => { // SUB
                                    regs[rm as usize] = val.wrapping_sub(imm) as u64;
                                    rflags = Self::update_flags_arithmetic(val.wrapping_sub(imm), val, imm, 8);
                                }
                                6 => { // XOR
                                    regs[rm as usize] = (val ^ imm) as u64;
                                    rflags = Self::update_flags_arithmetic(val ^ imm, val, imm, 8);
                                }
                                7 => { // CMP (no writeback)
                                    let r = val.wrapping_sub(imm);
                                    rflags = Self::update_flags_arithmetic(r, val, imm, 8);
                                }
                                _ => {}
                            }
                        } else {
                            // Memory operand - simplified: handle SIB + disp8 for [rsp+disp8]
                            let mut extra_offset = 2; // past opcode + modrm
                            if modrm & 7 == 4 && opcode_bytes.len() > extra_offset {
                                // SIB byte
                                let sib = opcode_bytes[extra_offset];
                                let base = (sib & 7) as usize;
                                let scale = (sib >> 6) & 3;
                                let index = ((sib >> 3) & 7) as usize;
                                extra_offset += 1;
                                let mut addr = regs[base];
                                if index != 4 {
                                    addr = addr.wrapping_add(regs[index] << scale);
                                }
                                let mod_val = (modrm >> 6) & 3;
                                if mod_val == 1 && opcode_bytes.len() > extra_offset {
                                    let d = opcode_bytes[extra_offset] as i8 as i64 as u64;
                                    addr = addr.wrapping_add(d);
                                } else if mod_val == 2 && opcode_bytes.len() > extra_offset + 3 {
                                    let d = i32::from_le_bytes([
                                        opcode_bytes[extra_offset], opcode_bytes[extra_offset+1],
                                        opcode_bytes[extra_offset+2], opcode_bytes[extra_offset+3],
                                    ]) as i64 as u64;
                                    addr = addr.wrapping_add(d);
                                }
                                // Perform the operation on memory
                                let val = self.mem.read_guest_u64(addr).unwrap_or(0) as i64;
                                match reg_op {
                                    5 => {
                                        let result = val.wrapping_sub(imm);
                                        self.mem.write_guest_u64(addr, result as u64).ok();
                                        rflags = Self::update_flags_arithmetic(result, val, imm, 8);
                                    }
                                    7 => {
                                        let result = val.wrapping_sub(imm);
                                        rflags = Self::update_flags_arithmetic(result, val, imm, 8);
                                    }
                                    0 => {
                                        let result = val.wrapping_add(imm);
                                        self.mem.write_guest_u64(addr, result as u64).ok();
                                        rflags = Self::update_flags_arithmetic(result, val, imm, 8);
                                    }
                                    _ => {}
                                }
                            }
                        }
                    }
                    rip += instruction_len;
                }

                // LEA reg, [r/m] (8D) — With REX.W: LEA r64, [r/m]
                0x8D => {
                    if opcode_bytes.len() >= 2 {
                        let modrm = opcode_bytes[1];
                        let reg_dst = (((modrm >> 3) & 7) as usize) | rex_r_ext;
                        let rm_src_base = (modrm & 7) as usize;
                        if modrm == 0x05 && opcode_bytes.len() >= 6 {
                            // RIP-relative: LEA reg, [rip+disp32]
                            let disp = i32::from_le_bytes([
                                opcode_bytes[2], opcode_bytes[3],
                                opcode_bytes[4], opcode_bytes[5],
                            ]) as i64;
                            regs[reg_dst] = rip + instruction_len + disp as u64;
                        } else if modrm >= 0xC0 {
                            // LEA reg, [reg] (effectively a MOV)
                            let rm_src = rm_src_base | rex_b_ext;
                            regs[reg_dst] = regs[rm_src];
                        }
                    }
                    rip += instruction_len;
                }

                // ALU r/m8, imm8 (80)
                0x80 => {
                    if opcode_bytes.len() >= 3 {
                        let modrm = opcode_bytes[1];
                        let reg_op = (modrm >> 3) & 7;
                        let rm = modrm & 7;
                        let imm = opcode_bytes[2] as i8 as i64;
                        if modrm >= 0xC0 && reg_op == 7 {
                            let val = (regs[rm as usize] & 0xFF) as i64;
                            let result = val.wrapping_sub(imm);
                            rflags = Self::update_flags_arithmetic(result, val, imm, 8);
                        }
                    }
                    rip += instruction_len;
                }

                // Note: 0x40..=0x4F (REX prefixes) are consumed by decode_instruction
                // and never returned as opcode. The following handlers are dead code
                // and removed. REX-aware handling is now in the respective opcode handlers.

                // TEST r/m8, imm8 (F6)
                0xF6 => {
                    if opcode_bytes.len() >= 3 {
                        let modrm = opcode_bytes[1];
                        let reg_op = (modrm >> 3) & 7;
                        let rm = modrm & 7;
                        if reg_op == 0 && modrm >= 0xC0 {
                            // TEST r/m8, imm8
                            let result = (regs[rm as usize] & 0xFF) & (opcode_bytes[2] as u64);
                            rflags = if result == 0 { rflags | 0x40 } else { rflags & !0x40 };
                        }
                    }
                    rip += instruction_len;
                }

                // INT3
                0xCC => {
                    exit_reason = format!("INT3 breakpoint at {:#018x}", rip);
                    trace_event!(TraceCategory::Execution, TraceLevel::Error, "interpreter",
                        &exit_reason);
                    break;
                }

                // CPUID (0F A2) - handled inside 0x0F arm
                // (merged into the 0x0F match arm above)

                // PUSH/POP
                0x50..=0x57 => {
                    // PUSH r64
                    let reg = (opcode - 0x50) as usize;
                    regs[4] -= 8;
                    self.mem.write_guest_u64(regs[4], regs[reg])?;
                    rip += 1;
                }
                0x58..=0x5F => {
                    // POP r64
                    let reg = (opcode - 0x58) as usize;
                    regs[reg] = self.mem.read_guest_u64(regs[4])?;
                    regs[4] += 8;
                    rip += 1;
                }

                // PUSH imm8
                0x6A => {
                    if opcode_bytes.len() >= 2 {
                        let imm = opcode_bytes[1] as i8 as i64 as u64;
                        regs[4] -= 8;
                        self.mem.write_guest_u64(regs[4], imm)?;
                    }
                    rip += 2;
                }

                // PUSH imm32
                0x68 => {
                    if opcode_bytes.len() >= 5 {
                        let imm = u32::from_le_bytes([
                            opcode_bytes[1], opcode_bytes[2],
                            opcode_bytes[3], opcode_bytes[4],
                        ]) as u64;
                        regs[4] -= 8;
                        self.mem.write_guest_u64(regs[4], imm)?;
                    }
                    rip += instruction_len;
                }

                // ALU r/m, imm32 (81) — ADD/OR/ADC/SBB/AND/SUB/XOR/CMP r/m, imm32
                0x81 => {
                    if opcode_bytes.len() >= 6 {
                        let modrm = opcode_bytes[1];
                        let reg_op = (modrm >> 3) & 7;
                        let rm = modrm & 7;
                        let imm = i32::from_le_bytes([
                            opcode_bytes[2], opcode_bytes[3],
                            opcode_bytes[4], opcode_bytes[5],
                        ]) as i64;
                        if modrm >= 0xC0 {
                            let val = regs[rm as usize] as i64;
                            match reg_op {
                                0 => { regs[rm as usize] = val.wrapping_add(imm) as u64; }
                                1 => { regs[rm as usize] = (val | imm) as u64; }
                                4 => { regs[rm as usize] = (val & imm) as u64; }
                                5 => { regs[rm as usize] = val.wrapping_sub(imm) as u64; }
                                6 => { regs[rm as usize] = (val ^ imm) as u64; }
                                7 => {
                                    let r = val.wrapping_sub(imm);
                                    rflags = Self::update_flags_arithmetic(r, val, imm, 8);
                                }
                                _ => {}
                            }
                        }
                    }
                    rip += instruction_len;
                }

                // TEST r/m, r (85) — With REX.W: TEST r/m64, r64
                0x85 => {
                    if opcode_bytes.len() >= 2 {
                        let modrm = opcode_bytes[1];
                        let reg_src = (((modrm >> 3) & 7) as usize) | rex_r_ext;
                        let rm_dst = ((modrm & 7) as usize) | rex_b_ext;
                        if modrm >= 0xC0 {
                            let result = regs[rm_dst] & regs[reg_src];
                            rflags = if result == 0 { rflags | 0x40 } else { rflags & !0x40 };
                        }
                    }
                    rip += instruction_len;
                }

                // MOV r/m, imm32 (C7) — With REX.W: MOV r/m64, imm32 (sign-extended)
                0xC7 => {
                    if opcode_bytes.len() >= 2 {
                        let modrm = opcode_bytes[1];
                        let rm_base = (modrm & 7) as usize;
                        let rm = rm_base | rex_b_ext;
                        if modrm >= 0xC0 && opcode_bytes.len() >= 6 {
                            let imm = i32::from_le_bytes([
                                opcode_bytes[2], opcode_bytes[3],
                                opcode_bytes[4], opcode_bytes[5],
                            ]);
                            regs[rm] = if rex_w {
                                imm as i64 as u64 // sign-extend
                            } else {
                                imm as u32 as u64 // zero-extend
                            };
                        } else if modrm == 0x04 && opcode_bytes.len() >= 3 {
                            // SIB + disp + imm32
                            let sib = opcode_bytes[2];
                            let base = (sib & 7) as usize;
                            let scale = (sib >> 6) & 3;
                            let index = ((sib >> 3) & 7) as usize;
                            let mut addr = regs[base];
                            if index != 4 {
                                addr = addr.wrapping_add(regs[index] << scale);
                            }
                            let mod_val = (modrm >> 6) & 3;
                            let mut extra = 3; // past opcode + modrm + sib
                            if mod_val == 1 && opcode_bytes.len() > extra {
                                addr = addr.wrapping_add(opcode_bytes[extra] as i8 as i64 as u64);
                                extra += 1;
                            } else if mod_val == 2 && opcode_bytes.len() > extra + 3 {
                                let d = i32::from_le_bytes([
                                    opcode_bytes[extra], opcode_bytes[extra+1],
                                    opcode_bytes[extra+2], opcode_bytes[extra+3],
                                ]) as i64 as u64;
                                addr = addr.wrapping_add(d);
                                extra += 4;
                            }
                            if opcode_bytes.len() >= extra + 4 {
                                let imm = i32::from_le_bytes([
                                    opcode_bytes[extra], opcode_bytes[extra+1],
                                    opcode_bytes[extra+2], opcode_bytes[extra+3],
                                ]);
                                if rex_w {
                                    self.mem.write_guest_u64(addr, imm as i64 as u64)?;
                                } else {
                                    self.mem.write_guest_u32(addr, imm as u32)?;
                                }
                            }
                        }
                    }
                    rip += instruction_len;
                }

                // ALU r/m, reg (01=ADD, 29=SUB, 31=XOR, 09=OR) — With REX.W: 64-bit
                0x01 | 0x29 | 0x31 | 0x09 => {
                    if opcode_bytes.len() >= 2 {
                        let modrm = opcode_bytes[1];
                        let reg_src = (((modrm >> 3) & 7) as usize) | rex_r_ext;
                        let rm_dst = ((modrm & 7) as usize) | rex_b_ext;
                        if modrm >= 0xC0 {
                            match opcode {
                                0x01 => { regs[rm_dst] = regs[rm_dst].wrapping_add(regs[reg_src]); } // ADD
                                0x29 => { regs[rm_dst] = regs[rm_dst].wrapping_sub(regs[reg_src]); } // SUB
                                0x31 => { regs[rm_dst] ^= regs[reg_src]; } // XOR
                                0x09 => { regs[rm_dst] |= regs[reg_src]; } // OR
                                _ => {}
                            }
                        }
                    }
                    rip += instruction_len;
                }

                // UD2 (0F 0B) - handled inside 0x0F arm
                // (merged into the 0x0F match arm above)

                // Default: advance by instruction length
                _ => {
                    if instruction_len == 0 {
                        // Unknown instruction, try single byte advance
                        trace_event!(TraceCategory::Execution, TraceLevel::Warn, "interpreter",
                            format!("Unknown opcode {:02x} at {:#018x}, advancing 1 byte", opcode, rip));
                        rip += 1;
                    } else {
                        rip += instruction_len;
                    }
                }
            }

            // Check for execution out of image bounds
            if rip < self.image_base || rip >= self.image_base + self.mem.image_size {
                // Check if it's in our API area
                let in_api = self.resolved_imports.values().any(|&addr| addr == rip);
                if !in_api {
                    exit_reason = format!("RIP went out of bounds: {:#018x}", rip);
                    trace_event!(TraceCategory::Execution, TraceLevel::Error, "interpreter",
                        &exit_reason);
                    break;
                }
            }
        }

        if instruction_count >= self.max_instructions {
            exit_reason = format!("Execution limit reached ({} instructions)", self.max_instructions);
        }

        trace_event!(TraceCategory::Execution, TraceLevel::Info, "runtime",
            format!("Execution finished: {} instructions, reason: {}", instruction_count, exit_reason));

        Ok(ExecutionResult {
            exit_code,
            exit_reason,
            instruction_count,
            output: self.dispatcher.get_output(),
            api_calls: self.dispatcher.get_api_log(),
        })
    }

    /// Minimal x86_64 instruction decoder
    /// Returns (primary_opcode, total_instruction_length)
    fn decode_instruction(bytes: &[u8]) -> (u8, u64, usize) {
        if bytes.is_empty() {
            return (0, 0, 0);
        }

        let mut pos = 0;

        // REX prefix (40-4F)
        let mut has_rex = false;
        while pos < bytes.len() && bytes[pos] >= 0x40 && bytes[pos] <= 0x4F {
            has_rex = true;
            pos += 1;
        }

        if pos >= bytes.len() {
            return (0, pos as u64, pos);
        }

        let opcode = bytes[pos];

        // Simple length estimation based on opcode patterns
        let len = match opcode {
            // 1-byte instructions
            0x90 | 0xC3 | 0xCC => pos + 1,

            // RET imm16
            0xC2 => pos + 3,

            // PUSH/POP reg
            0x50..=0x5F => pos + 1,

            // PUSH imm8
            0x6A => pos + 2,

            // PUSH imm32
            0x68 => pos + 5,

            // MOV reg, imm32
            0xB8..=0xBF => {
                if has_rex { pos + 9 } else { pos + 5 }
            }

            // Short conditional jumps
            0x70..=0x7F => pos + 2,

            // Short JMP
            0xEB => pos + 2,

            // CALL rel32
            0xE8 => pos + 5,

            // JMP rel32
            0xE9 => pos + 5,

            // MOV r/m, reg and MOV reg, r/m (89, 8B)
            0x89 | 0x8B | 0x33 | 0x29 | 0x01 | 0x31 | 0x09 => {
                pos + 2 + if bytes.len() > pos + 1 {
                    let modrm = bytes[pos + 1];
                    Self::modrm_extra_bytes(modrm, bytes, pos + 2)
                } else { 0 }
            }

            // LEA
            0x8D => {
                pos + 2 + if bytes.len() > pos + 1 {
                    let modrm = bytes[pos + 1];
                    Self::modrm_extra_bytes(modrm, bytes, pos + 2)
                } else { 0 }
            }

            // ALU r/m, imm8 (80, 83)
            0x80 | 0x83 => {
                pos + 3 + if bytes.len() > pos + 1 {
                    let modrm = bytes[pos + 1];
                    Self::modrm_extra_bytes(modrm, bytes, pos + 2)
                } else { 0 }
            }

            // ALU r/m, imm32 (81)
            0x81 => {
                pos + 5 + if bytes.len() > pos + 1 {
                    let modrm = bytes[pos + 1];
                    Self::modrm_extra_bytes(modrm, bytes, pos + 2)
                } else { 0 }
            }

            // TEST r/m8, imm8 (F6)
            0xF6 => pos + 3,

            // TEST r/m, r (85)
            0x85 => {
                pos + 2 + if bytes.len() > pos + 1 {
                    let modrm = bytes[pos + 1];
                    Self::modrm_extra_bytes(modrm, bytes, pos + 2)
                } else { 0 }
            }

            // MOV r/m, imm32 (C7)
            0xC7 => {
                let extra = if bytes.len() > pos + 1 {
                    let modrm = bytes[pos + 1];
                    Self::modrm_extra_bytes(modrm, bytes, pos + 2)
                } else { 0 };
                pos + 2 + extra + 4 // opcode + modrm + [SIB/disp] + imm32
            }

            // Note: 0x48..=0x4F are REX prefixes consumed by the loop above,
            // so they are never returned as opcode. No handler needed here.

            // FF (various)
            0xFF => {
                pos + 2 + if bytes.len() > pos + 1 {
                    let modrm = bytes[pos + 1];
                    Self::modrm_extra_bytes(modrm, bytes, pos + 2)
                } else { 0 }
            }

            // 0F prefix (2-byte opcodes)
            0x0F => {
                if bytes.len() > pos + 1 {
                    let op2 = bytes[pos + 1];
                    match op2 {
                        0x84 | 0x85 | 0x8C | 0x8D | 0x8E | 0x8F => pos + 6, // Jcc rel32
                        0xA2 => pos + 2, // CPUID
                        0x0B => pos + 2, // UD2
                        _ => pos + 2,
                    }
                } else {
                    pos + 2
                }
            }

            // Default: minimum length
            _ => pos + 1,
        };

        (opcode, len as u64, pos)
    }

    fn modrm_extra_bytes(modrm: u8, bytes: &[u8], pos: usize) -> usize {
        let mod_val = (modrm >> 6) & 3;
        let rm = modrm & 7;
        match mod_val {
            0 => {
                if rm == 4 {
                    // SIB byte
                    if bytes.len() > pos { 1 } else { 0 }
                } else if rm == 5 {
                    4 // disp32
                } else {
                    0
                }
            }
            1 => {
                if rm == 4 && bytes.len() > pos {
                    2 // SIB + disp8
                } else {
                    1 // disp8
                }
            }
            2 => {
                if rm == 4 && bytes.len() > pos {
                    5 // SIB + disp32
                } else {
                    4 // disp32
                }
            }
            3 => 0, // register direct
            _ => 0,
        }
    }

    fn update_flags_arithmetic(result: i64, _a: i64, _b: i64, _bits: u8) -> u64 {
        let mut flags = 0u64;
        if result == 0 { flags |= 0x40; } // ZF
        if result < 0 { flags |= 0x80; } // SF
        flags | 0x200 // IF
    }

    /// Get the PE summary
    pub fn pe_summary(&self) -> Option<String> {
        self.pe.as_ref().map(|pe| pe.summary())
    }

    /// Get memory layout
    pub fn memory_layout(&self) -> String {
        format!(
            "Image Base: {:#018x}\n\
             Entry Point: {:#018x}\n\
             Image Size: {:#010x}\n\
             Stack: {:#018x} ({:#x})\n\
             Heap: {:#018x} ({:#x})\n",
            self.image_base,
            self.entry_point,
            self.mem.image_size,
            self.mem.stack_base, self.mem.stack_size,
            self.mem.heap_base, self.mem.heap_size,
        )
    }

    pub fn trace_log_dir(&self) -> &Path {
        &self.trace_log_dir
    }

    pub fn replay_dir(&self) -> &Path {
        &self.replay_dir
    }

    pub fn dispatcher(&self) -> &Win32Dispatcher {
        &self.dispatcher
    }

    pub fn mem(&self) -> &MemoryManager {
        &self.mem
    }
}

/// Execution result
#[derive(Debug, Clone)]
pub struct ExecutionResult {
    pub exit_code: u32,
    pub exit_reason: String,
    pub instruction_count: u64,
    pub output: String,
    pub api_calls: Vec<crate::win32::ApiCall>,
}

impl std::fmt::Display for ExecutionResult {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f, "=== Execution Result ===")?;
        writeln!(f, "Exit Code: {}", self.exit_code)?;
        writeln!(f, "Exit Reason: {}", self.exit_reason)?;
        writeln!(f, "Instructions Executed: {}", self.instruction_count)?;
        writeln!(f, "API Calls: {}", self.api_calls.len())?;
        writeln!(f, "\nCaptured Output:")?;
        writeln!(f, "{}", self.output)?;
        writeln!(f, "\nAPI Call Log:")?;
        for call in &self.api_calls {
            writeln!(f, "  {}.{}() -> {:#018x}", call.module, call.function, call.return_value)?;
        }
        Ok(())
    }
}
