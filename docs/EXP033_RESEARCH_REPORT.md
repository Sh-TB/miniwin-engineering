# EXP-033: Dalvik VM Architecture Research Report

| Field | Value |
|---|---|
| **Experiment** | EXP-033 |
| **Title** | Dalvik VM Architecture Research — Foundation for MiniAndroid Compatibility Layer |
| **Date** | 2026-08-15 |
| **Type** | Research-only (no code produced) |
| **Status** | Complete |
| **Predecessor** | MiniAndroid DEX parser + proof-of-concept bytecode tracer |
| **Successor** | MiniAndroid register frame model implementation (EXP-034+) |

---

## 1. Objective

MiniAndroid can currently parse DEX files and execute a small number of Dalvik bytecodes in a trace mode, producing instruction logs and register state snapshots. However, when attempting to execute real Android APK bytecode, execution immediately fails at the first method invocation boundary. The failure mode is not a crash with a clear error message — it is silent incorrectness, where methods receive wrong arguments, return values vanish, and `this` references are lost. The root cause is unknown because the implementation does not follow Dalvik's architectural model.

This research was commissioned to eliminate that uncertainty. The goal is to build a comprehensive understanding of the AOSP Dalvik VM's architecture — from DEX file format internals to interpreter loop design, register frame layout, object model, method dispatch, exception handling, garbage collection, and the Zygote forking model — and then systematically compare MiniAndroid's current implementation against every major subsystem. The expected outcome is a definitive identification of the first execution blocker and an ordered roadmap of implementation tasks that will allow MiniAndroid to progress from a DEX reader into a functioning Dalvik-compatible runtime.

No code was written during this experiment. The deliverables are purely analytical: a comprehensive Dalvik architecture reference and a gap analysis document. These documents are intended to be the authoritative technical basis for all subsequent MiniAndroid implementation work.

---

## 2. Research Methodology

The research was conducted through systematic study of the following primary sources:

1. **AOSP Dalvik VM source code (Android 4.4, KitKat)** — the final and most mature version of the Dalvik VM before Google transitioned to ART. Key source trees examined include `dalvik/libdex/` (DEX parsing and structure definitions), `dalvik/vm/interp/` (portable interpreter and mterp templates), `dalvik/vm/oo/` (object model, `Object.h`, `Class.cpp`), `dalvik/vm/alloc/` (garbage collection and heap management), `dalvik/vm/Jni.cpp` (JNI bridge and native method dispatch), `dalvik/vm/Verify.cpp` (bytecode verification), `dalvik/vm/compiler/` (JIT compilation pipeline), `dalvik/vm/native/` (native method registrations including Zygote), and `dalvik/vm/Thread.h` (thread and interpreter stack structures).

2. **DEX file format specification** — the official `.dex` binary format documentation covering all header fields, section layouts, LEB128/MUTF-8 encodings, and the interdependency graph between DEX sections. The specification was cross-referenced against the AOSP `DexFile.h` header to verify struct layouts and offset calculations.

3. **Dalvik interpreter architecture** — detailed analysis of the portable interpreter (`InterpC-all.c`), the mterp (macro-templated interpreter) generated sources, and the frame allocation conventions. This included tracing the exact sequence of operations during `invoke-virtual` to understand how arguments are copied from caller registers into the callee's incoming frame area.

4. **MiniAndroid source code** — the existing implementation was examined to determine what DEX sections are parsed, which opcodes are implemented, how registers are managed, and where the architectural deviations from Dalvik occur.

5. **Secondary references** — Android developer documentation, community reverse-engineering resources, and academic papers on register-based virtual machine design were consulted where AOSP source comments were insufficient.

---

## 3. Key Findings Summary

The following table presents the ten most significant findings from this research, ordered by implementation impact:

| # | Area | Finding | Impact | Priority |
|---|---|---|---|---|
| 1 | **Register Frame Model** | Dalvik partitions each method frame into `ins[]`, `locals[]`, and `outs[]` using `code_item` fields. MiniAndroid uses a flat register array with no frame management. | **Blocks all method calls.** Arguments are not passed, return values are lost, `this` is clobbered, recursion is impossible. | **P0 — Critical** |
| 2 | **Bytecode Coverage** | MiniAndroid implements ~8 of ~218 Dalvik opcodes (3.7%). Missing: all arithmetic, all control flow, `move-result*`, `invoke-static`, `invoke-super`, `invoke-interface`, array operations. | Even a trivial `Hello World` Activity requires 30–40 additional opcodes. | **P0 — Critical** |
| 3 | **Object Model** | No `Object` or `ClassObject` runtime structures exist. `new-instance` cannot allocate objects with proper headers, field layouts, or class metadata. | `iget`/`iput` operate on non-existent memory. No virtual dispatch table exists. | **P1 — High** |
| 4 | **DEX class_data_item** | The `class_data_item` (LEB128-encoded field/method lists) is not parsed. This is the bridge between `class_defs` and runtime class structures. | Cannot enumerate methods, fields, or access flags per class. Blocks vtable construction and field offset computation. | **P1 — High** |
| 5 | **Method Resolution** | No virtual dispatch through class hierarchies. `invoke-virtual` cannot walk superclass chains. `invoke-super` is entirely absent. | `super.onCreate()` — the very first instruction in every Activity — fails immediately. | **P1 — High** |
| 6 | **Return Value Propagation** | No `move-result*` opcodes and no result register concept. Non-void method returns are silently discarded. | Any method that returns a value (nearly all of them) breaks the caller's logic. | **P0 — Critical** |
| 7 | **Exception Handling** | No `try_items` parsing, no `throw` opcode, no frame unwinding. Dalvik's `encoded_catch_handler` is entirely unprocessed. | Any exception (NPE, ClassCastException) crashes the interpreter rather than being caught. | **P2 — Medium** |
| 8 | **Garbage Collection** | No heap management, no object allocation with zero-initialization, no reachability tracking. | Memory leaks are guaranteed. Uninitialized fields cause type confusion and undefined behavior. | **P2 — Medium** |
| 9 | **proto_ids Section** | The `proto_ids` DEX section (method signatures: argument types + return type) is not parsed. | Cannot verify method compatibility, match overridden methods, or determine argument passing conventions for dispatch. | **P1 — High** |
| 10 | **Type System & Verification** | No register type tracking, no class loading pipeline, no class hierarchy modeling, no `check-cast`. | Type confusion manifests as memory corruption rather than controlled exceptions. Unsafe for arbitrary APK execution. | **P2 — Medium** |

---

## 4. Architecture Reference

The primary research document, `research/dalvik_architecture_notes.md` (997 lines), provides a comprehensive reference for the AOSP Dalvik VM architecture. It covers the following twelve topics, each structured with C struct definitions, memory layout diagrams, and AOSP source file references:

| # | Topic | Coverage |
|---|---|---|
| 1 | **DEX File Format** | Complete `DexHeader` layout (112 bytes, 19 fields), all DEX sections (`string_ids`, `type_ids`, `proto_ids`, `field_ids`, `method_ids`, `class_defs`, `code_item`, `class_data_item`, `map_list`), MUTF-8 encoding details, LEB128 encoding rules, and interdependencies between sections. |
| 2 | **Dalvik VM Register Model** | Register-based (vs stack-based) design rationale, `code_item` frame fields (`registers_size`, `ins_size`, `outs_size`), frame memory layout with `ins[]`/`locals[]`/`outs[]` partitioning, wide value (long/double) register pairing rules, and frame pointer (`fp`) usage in the interpreter. |
| 3 | **Method Invocation & Call Stack** | `invoke-*` instruction formats (35c, 3rc), argument passing via shared `outs[]`/`ins[]` memory, return value protocol (`rV` result register + `move-result*`), frame push/pop mechanics, and the interpreter's method dispatch loop. |
| 4 | **Object Representation** | `Object` struct (class pointer + lock word + instance fields), `ClassObject` struct (80+ bytes: descriptor, superclass, vtable, field layout, access flags, status), instance field offset computation, and object size calculation. |
| 5 | **Field Access** | `iget`/`iput`/`sget`/`sput` mechanics, instance field offset resolution through class hierarchy, static field storage per class, field type categories (1-slot vs 2-slot), and volatile/synchronized field semantics. |
| 6 | **Type System & Class Hierarchy** | Class loading pipeline, superclass resolution, interface implementation, vtable construction with override resolution, access flag semantics, class initialization (`<clinit>`) triggering, and primitive type categories. |
| 7 | **Exception Handling** | `try_items` and `encoded_catch_handler` structures, `throw` opcode mechanics, frame unwinding through the call stack, exception handler matching by exception type hierarchy, and `finally` block implementation via JSR-style tricks. |
| 8 | **Garbage Collection** | Mark-sweep GC algorithm, card table for write barriers, object allocation with zero-initialization, `Thread` local allocation buffers, GC trigger thresholds, and root set enumeration (stack roots, JNI global refs, static fields). |
| 9 | **Interpreter Loop Architecture** | Portable interpreter (`Interp.c`) dispatch loop, mterp (macro-templated) generated interpreter for ARM/x86, instruction format families (12x, 23x, 35c, 3rc, etc.), fetch-decode-execute cycle, and handler function pointer table design. |
| 10 | **JNI & Native Interface** | JNI method registration, native method bridge code, `JNIEnv` function table, parameter type marshalling between Dalvik registers and C calling conventions, and the `dalvik_system_Zygote` native methods. |
| 11 | **Optimization: JIT Compilation** | JIT compilation pipeline, trace-based compilation, code cache management, hot method detection, and JIT entry/exit stubs that transition between interpreted and compiled code. |
| 12 | **Zygote Forking Model** | Zygote pre-loading of boot classes and framework resources, `fork()`-based application process creation, COW (copy-on-write) page sharing, post-fork reinitialization requirements, and mmap-based DEX loading for page sharing. |

An appendix provides a table mapping each architectural component to its corresponding AOSP source file path in Android 4.4.

---

## 5. Gap Analysis Summary

The secondary research document, `research/miniandroid_vs_dalvik.md` (723 lines), performs a subsystem-by-subsystem comparison of MiniAndroid's current implementation against the full Dalvik VM. The key quantitative findings are:

### DEX Parsing Coverage

| Status | Count | Sections |
|---|---|---|
| **DONE** | 3/10 | `header`, `string_ids`, `type_ids`, `method_ids` (counted as done; note: method_ids also done) |
| **PARTIAL** | 3/10 | `field_ids` (referenced by instructions but not a standalone indexed structure), `class_defs` (parsed for `code_item` but not `class_data_off`), `code_item` (bytecodes extracted but frame fields unused, no `try_items`) |
| **MISSING** | 4/10 | `proto_ids` (method signatures), `map_list` (section directory), `class_data_item` (field/method enumeration), `encoded_catch_handler` (exception tables) |

The most critical missing section is `class_data_item` — it is the bridge between `class_defs` and the runtime class model, containing the encoded field and method lists that define object memory layout and dispatch tables.

### Bytecode Interpreter Coverage

- **Total Dalvik opcodes:** ~218 across 13 instruction format families
- **MiniAndroid implemented:** ~8 opcodes (`return-void`, `invoke-virtual`, `invoke-direct`, `const-string`, `iget`, `iput`, `sget`, `new-instance`)
- **Coverage:** approximately 3.7%
- **Estimated minimum for trivial execution:** 30–40 additional opcodes (arithmetic, control flow, move operations, invoke-super, invoke-static, move-result)

### Register Frame Model

**Status: BROKEN.** MiniAndroid uses a flat register array or dictionary keyed by register number. There is no frame stack, no partitioning into `ins[]`/`outs[]`/`locals[]`, and no frame push on invoke or pop on return. The `code_item` fields `registers_size`, `ins_size`, and `outs_size` are read from the DEX file but not used to construct execution frames. This is not a partial implementation — the concept does not exist in the current codebase.

### Object Model

**Status: ABSENT.** No `Object` or `ClassObject` runtime structures exist. The `new-instance` opcode produces a trace entry but cannot allocate heap memory with a proper object header, class pointer, or field layout. There is no virtual dispatch table, no instance field offset computation, and no way to distinguish between objects of different classes at runtime.

### Exception Handling

**Status: ABSENT.** The `try_items` and `encoded_catch_handler` structures in `code_item` are not parsed. The `throw` opcode is not implemented. There is no frame unwinding mechanism. Any exception during execution will crash the interpreter rather than being caught by a matching handler.

### Garbage Collection

**Status: ABSENT.** There is no heap, no object allocation, no reachability tracking, and no garbage collector. Memory management is entirely unimplemented. Even if objects could be allocated, there would be no mechanism to reclaim them.

---

## 6. First Execution Blocker

### The Register Frame Model

The single most critical blocker preventing MiniAndroid from executing real Android APK code is the **absence of a Dalvik-compatible register frame and calling convention model**. This is not one blocker among many — it is the foundational mechanism on which every other subsystem depends. Without correctly structured frames, no method call can succeed, no return value can propagate, and no object operation can function correctly.

### Why This Is the Blocker

Dalvik's execution model is built entirely around the frame. Every method invocation creates a new frame, and every register access in the interpreter goes through a frame pointer. The frame defines three regions: incoming arguments (`ins[]`), local variables (`locals[]`), and outgoing arguments (`outs[]`). These regions have specific offsets and semantics that the interpreter relies on for every operation.

When `invoke-virtual {vC, vD, vE, vF, vG}, meth@BBBB` is executed, Dalvik copies the caller's registers `vC..vG` into the callee's `ins[]` area (which physically overlaps with the caller's `outs[]`). The callee then accesses its arguments as `v[0]`, `v[1]`, etc. — the first `ins_size` registers of its own frame. For instance methods, `v[0]` is always `this`.

Without frames, this mechanism has nowhere to operate. The callee reads from whatever flat register state exists, which may contain garbage, stale values from a previous call, or values from the caller's frame at wrong offsets. Arguments are misrouted, `this` is lost, and the method executes with completely incorrect input.

Similarly, when a method returns, Dalvik stores the return value in a result register (`rV`) within the caller's frame. The caller then uses `move-result*` to copy this value into its own register. Without frames, there is no well-defined location for the result register. Return values are silently discarded, and the caller proceeds with uninitialized or stale data in the register that should hold the return value.

### Dependency Chain

The frame model is not just the first blocker — it is a prerequisite for nearly every other improvement:

- **Object model:** `iget`/`iput` need the correct `this` reference, which requires proper frame-based argument passing.
- **Method dispatch:** `invoke-super` needs the current method's class context, which is stored in the frame's saved method pointer.
- **Exception handling:** Unwinding walks the frame stack to find matching handlers.
- **GC:** Stack roots are enumerated by walking frames and scanning registers for object references.
- **Additional opcodes:** `move-result*`, `move`, `move-object` all operate within the frame model.

Adding any of these features without first implementing the frame model is futile — they will all operate on incorrect register state.

---

## 7. Recommended Next Steps

The following tasks are ordered by dependency. Each task assumes the preceding ones are complete:

1. **Implement proper register frame model (ins/outs/locals partitioning).** Design a `Frame` struct with `outs[outs_size]`, a saved method pointer, padding, and `locals[registers_size]`. Implement frame allocation on the interpreter stack, frame push on `invoke-*` (copying caller argument registers into callee's `ins[]`), frame pop on `return-*` (restoring the caller's frame pointer), and a result register for non-void returns. This is the architectural foundation that unblocks all subsequent work.

2. **Implement object representation (Object header + ClassObject).** Define a minimal `Object` struct with a class pointer and lock word. Define a minimal `ClassObject` struct with descriptor, superclass pointer, vtable, instance field layout table, and `objectSize`. This enables `new-instance` to allocate properly structured objects and `iget`/`iput` to access fields at computed offsets.

3. **Complete DEX class_data_item parsing.** Parse the LEB128-encoded `encoded_class_data_item` to enumerate static fields, instance fields, direct methods, and virtual methods for each class. This provides the data needed to construct `ClassObject` instances at runtime — field lists for offset computation, method lists for vtable construction, and access flags for visibility checks.

4. **Implement field resolution (instance + static fields).** Walk the class hierarchy to compute instance field offsets, assign static field storage per class, and resolve field references from `field_ids` to runtime field descriptors. This makes `iget`/`iput`/`sget`/`sput` functional rather than speculative.

5. **Expand invoke-* opcode coverage.** Implement `invoke-static`, `invoke-super`, `invoke-interface`, and the range variants (`3rc` format). Add `move-result`, `move-result-object`, `move-result-wide`. Implement basic virtual dispatch through the vtable and superclass chain resolution for `invoke-super`. This enables method chaining — the fundamental pattern of Android Activity lifecycle code.

6. **Implement basic exception handling (try/catch).** Parse `try_items` and `encoded_catch_handler` from `code_item`. Implement the `throw` opcode. Build frame unwinding that walks the frame stack searching for a matching catch handler by exception type hierarchy. This prevents unhandled exceptions from crashing the interpreter.

7. **Implement string/int object allocation.** Create a minimal heap with object allocation and zero-initialization. Implement `java.lang.String` representation (at minimum, a length + MUTF-8 data + `ClassObject` pointer). Implement boxed integer allocation for autoboxing. This allows method calls that pass strings or integers as arguments to function correctly.

8. **Add basic type verification.** Track register types through the interpreter (reference vs int vs wide). Enforce type category rules (category 1 vs category 2 for wide values). Implement `check-cast` and `instance-of` using the class hierarchy. This catches type confusion before it manifests as memory corruption.

---

## 8. Deliverables

The following three files were produced during this experiment:

| # | File | Lines | Description |
|---|---|---|---|
| 1 | `research/dalvik_architecture_notes.md` | 997 | Comprehensive Dalvik VM architecture reference covering 12 topics: DEX format, register model, method invocation, object representation, field access, type system, exception handling, GC, interpreter architecture, JNI, JIT, and Zygote. Includes C struct definitions, memory layout diagrams, and AOSP source file references. |
| 2 | `research/miniandroid_vs_dalvik.md` | 723 | Gap analysis comparing MiniAndroid's current implementation against full Dalvik across 10 subsystems. Includes DEX parsing coverage table, bytecode opcode inventory, register frame model comparison with diagrams, object model gap analysis, method resolution assessment, and a minimal architecture roadmap for executing a trivial Android app. |
| 3 | `docs/EXP033_RESEARCH_REPORT.md` | — | This report. Executive summary of research findings, blocker identification, and ordered implementation recommendations. |

---

## Appendix: Blocker Evidence — Register Frame Model

### A. Dalvik Frame Layout (from AOSP `dalvik/vm/interp/Interp.c`)

```
+====================+  <-- fp (frame pointer)
|   out[outs_size-1] |  \  outgoing arguments
|   ...              |   | (physically shared with callee's ins[])
|   out[0]           |  /
+--------------------+
|   saved method ptr |  Method* for current activation
+--------------------+
|   padding (0–3 w)  |  Align to 8-byte boundary
+--------------------+
|   v[reg_size-1]    |  \  local registers
|   ...              |   | v[0..ins_size-1] = incoming args
|   v[0]             |  /    (v[0] = this for non-static)
+--------------------+  <-- fp + (reg_size + padding/4 + 1)
```

The frame pointer (`fp`) is the most frequently referenced variable in the Dalvik interpreter loop. Every register access is expressed as `fp[vA]` where `vA` is the register number from the bytecode instruction. The `ins[]` area occupies `v[0]` through `v[ins_size-1]`. The `locals[]` area occupies `v[ins_size]` through `v[registers_size-1]`. The `outs[]` area occupies `fp[0]` through `fp[outs_size-1]` — note that `outs[]` is addressed relative to `fp` with *negative* conceptual offsets, while `v[]` is addressed with *positive* offsets from the bottom of the frame.

### B. How `invoke-virtual` Passes Arguments via Callee's `ins[]`

When the interpreter encounters `invoke-virtual {v2, v3, v4}, meth@0x00A5`, the following sequence executes:

1. **Resolve the method** using `method_ids[0x00A5]` to obtain a `Method*` pointer.
2. **Verify the argument count** matches the method's `ins_size` (from its `code_item`).
3. **Copy arguments** from the caller's registers into the caller's `outs[]` area: `fp[0] = caller_v2`, `fp[1] = caller_v3`, `fp[2] = caller_v4`. Because the callee's frame is pushed directly above the caller's frame, the callee's `v[0..2]` (its `ins[]`) physically overlaps with the caller's `fp[0..2]` (its `outs[]`). No actual memory copy is needed — it is a zero-cost argument passing convention.
4. **Push the callee's frame** by advancing the frame pointer: `fp -= (callee_registers_size + padding + 1)`.
5. **Set the saved method pointer** in the callee's frame to the resolved `Method*`.
6. **Begin executing** the callee's bytecodes. The callee accesses its arguments as `fp[0]` (this), `fp[1]` (arg1), `fp[2]` (arg2).

This shared-memory convention is why Dalvik requires that the caller's `outs_size >=` the callee's `ins_size`. The DEX verifier enforces this constraint.

### C. How `return-void` and `move-result` Propagate Return Values

When the callee executes `return-void` (opcode `0x0e`):

1. The interpreter pops the frame: `fp += (current_registers_size + padding + 1)`, restoring the caller's frame pointer.
2. Execution resumes at the instruction following the `invoke-*` in the caller.

When the callee executes `return vA` (opcode `0x0f` for int, `0x11` for object):

1. The interpreter stores the return value in the caller's **result register** (`retVal`), a dedicated slot associated with the caller's frame.
2. The frame is popped.
3. Execution resumes at the caller's next instruction.

The caller then executes `move-result vB` (opcode `0x0a`) or `move-result-object vB` (opcode `0x0c`):

1. The interpreter copies `retVal` into the caller's register `vB`.

This two-step return protocol (return stores to result register, then `move-result*` copies to a local register) exists because the Dalvik bytecode format does not encode the destination register in the `return` instruction — only in the `move-result*` instruction. This allows the compiler to schedule code between the invoke and the move-result.

### D. What Breaks in MiniAndroid Without the Frame Model

MiniAndroid's flat register model fails in the following specific ways when executing any non-trivial bytecode sequence:

1. **Argument misrouting.** Consider `invoke-virtual {v0, v1}, meth@A` where the method expects `this=v0, arg=v1`. Without frames, the callee has no distinct register namespace. It may read from the same `v0` and `v1` (if the flat array is shared) — but these may have been overwritten by any previous operation in the callee's own bytecode, or the callee may use completely different register numbers, making the arguments unreachable.

2. **`this` loss.** In a method chain like `Activity.onCreate()`: the method receives `this` in `v[0]`, then calls `super.onCreate(this, savedInstanceState)`. After the super call, `this` must still be in `v[0]` for the subsequent `setContentView(this, layoutId)`. With flat registers, the super call's execution overwrites `v0`, destroying `this`.

3. **Return value loss.** `String.length()` returns an int. The caller expects this value via `move-result v1`. Without a result register and without `move-result`, the return value disappears. The caller's `v1` retains whatever value it held before the call — typically zero or garbage.

4. **Recursive impossibility.** Any method that calls itself (even indirectly via a call chain that returns to it) requires separate register states per activation. A flat array merges all activations, so recursive calls overwrite the caller's registers, making correct return impossible.

5. **Outs/ins size mismatch.** Dalvik's verifier guarantees `caller.outs_size >= callee.ins_size`. Without the frame model, there is no `outs_size` concept. The interpreter cannot enforce this constraint, and arguments may be written beyond the bounds of any register array.

These are not edge cases — they are the normal, expected behavior of every Android application. Every Activity lifecycle method, every view operation, every string concatenation requires correct argument passing and return value propagation. The frame model is the mechanism that makes this possible, and its absence is why MiniAndroid cannot execute real bytecode.
