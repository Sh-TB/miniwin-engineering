# EXP-034: Dalvik Register Frame Model Prototype — Report

| Field | Value |
|---|---|
| **Experiment** | EXP-034 |
| **Title** | Dalvik Register Frame Model Prototype |
| **Date** | 2026-08-15 |
| **Type** | Prototype (C implementation + synthetic tests) |
| **Status** | Complete |
| **Predecessor** | EXP-033 (Dalvik Architecture Research) |
| **Successor** | EXP-035 (Object Representation + Field Resolution) |
| **Tests** | 24/24 PASS, 0 FAIL |
| **Files** | `src/dalvik/frame.h`, `src/dalvik/frame.c`, `tests/dalvik_frame/test_frame.c` |

---

## 1. Objective

EXP-033 identified the **register frame model** as the single most critical execution blocker in MiniAndroid. The current implementation uses a flat register array with no frame partitioning, which means method arguments are not passed correctly, return values vanish, and nested calls corrupt each other's state. EXP-034 builds a minimal C prototype that proves the Dalvik-style register frame partitioning works correctly for static methods, instance methods, and nested call chains. No Android app execution is claimed — this is a synthetic-only validation of the frame model.

---

## 2. Architecture Decision

### Chosen Model: Flat Array with Computed Offsets

Each frame stores all registers in a single flat `RegValue regs[FRAME_MAX_REGS]` array, partitioned into three contiguous regions by computed offsets:

```
regs[0]                    regs[locals_size]           regs[registers_size]
  |---- locals[] ----|        |---- ins[] ----|           |---- outs[] ----|
  v0  v1  ... v[L-1]      p0  p1  ... p[N-1]          out0  out1  ...  

  L = locals_size            N = ins_size                outs_size
  ins_offset = L            (first arg index)            outs_offset = registers_size
```

This matches Dalvik's actual memory layout where `ins` follow `locals` in the virtual register space, and `outs` occupy the highest slots. The key offsets are:

- `ins_offset = locals_size` — first incoming argument register
- `outs_offset = registers_size` — first outgoing argument slot

### Why Not Separate Arrays?

An alternative design would use three separate arrays (`locals[]`, `ins[]`, `outs[]`). This was rejected because:

1. Dalvik opcodes reference registers by a single index (v0, v1, ...) that spans all three regions. A single flat array allows `frame_get_reg(f, vN)` to work without region branching.
2. The Dalvik verifier treats the register space as one contiguous range. Separate arrays would require translating between the verifier's single-index model and the implementation's multi-array model.
3. A single array has better cache locality for frame initialization and clearing.

### Return Value Storage

The return value (`rV` in Dalvik terminology) is stored **outside** the virtual register array in a dedicated `retval` field. This matches Dalvik's design where `move-result` reads from a separate result register, not from the callee's register space. A `has_retval` flag distinguishes void returns from value returns.

---

## 3. Memory Layout

### FrameStack (global)

```
FrameStack {
  frames[FRAME_MAX_DEPTH]   // 64 frames, stack-allocated
  count: uint32_t            // current depth
  next_frame_id: uint32_t    // auto-increment for trace
  trace_enabled: int         // 0=silent, 1=verbose
}
```

Size: 64 × sizeof(Frame) + 16 bytes. Each Frame is ~2.4 KB (256 × 12 bytes for regs + metadata). Total: ~154 KB. This is acceptable for a prototype; a production runtime would allocate frames from a heap-allocated interpreter stack.

### Frame (per method invocation)

```
Frame {
  regs[256]              // RegValue[256] = {uint64_t value, uint32_t type_tag}
  registers_size         // total v-registers (from DEX code_item)
  ins_size               // incoming argument count
  outs_size              // outgoing argument slot count
  locals_size            // = registers_size - ins_size
  ins_offset             // = locals_size
  outs_offset            // = registers_size
  retval                 // RegValue — return value
  has_retval             // int — 1 if set
  method                 // const MethodMetadata*
  prev                   // Frame* — caller frame
  depth                  // uint32_t — call depth
  frame_id               // uint32_t — trace ID
}
```

### MethodMetadata

```
MethodMetadata {
  class_name       // const char* — e.g. "LFoo;"
  method_name      // const char* — e.g. "foo"
  signature        // const char* — e.g. "(II)I"
  registers_size   // uint16_t — from DEX code_item
  ins_size         // uint16_t — from DEX code_item
  outs_size        // uint16_t — from DEX code_item
  locals_size      // uint16_t — computed: registers_size - ins_size
  flags            // MethodFlags (STATIC/DIRECT/VIRTUAL/INTERFACE)
}
```

### RegValue

```
RegValue {
  value     // uint64_t — raw 64-bit register content
  type_tag  // uint32_t — 0=uninit, 1=int, 2=ref, 3=float, 8=byte, 9=short, 10=char
}
```

---

## 4. Evidence Logs

### Test A: Static Method `foo(int a, int b) -> int`

**Scenario**: Caller pushes v0=3, v1=7, invokes static method `LFoo;->foo(II)I`.

**Key evidence**:
```
INVOKE  LFoo;->foo (II)I
  args [a0=3, a1=7]
FRAME CREATE  id=2 depth=1
  method: LFoo;->foo (II)I
  register count: 3 (locals=1 ins=2 outs=1)
  incoming [p0=3, p1=7]
  PASS: arg a == 3 (3 == 3)
  PASS: arg b == 7 (7 == 7)
RETURN  LFoo;->foo  value=10
  PASS: return value == 10 (10 == 10)
  PASS: caller v0 preserved == 3 (3 == 3)
  PASS: caller v1 preserved == 7 (7 == 7)
```

**Verified**: Arguments 3 and 7 arrive in callee's `ins[0]` and `ins[1]`. Return value 10 (3+7) propagates back to caller. Caller's v0, v1 unchanged.

---

### Test B: Instance Method `object.method(int x)`

**Scenario**: Caller pushes v0=0xDEADBEEFCAFE0000 (object ref), v1=42, invokes virtual `LBar;->doIt(I)V`.

**Key evidence**:
```
INVOKE  LBar;->doIt (I)V
  args [a0=ref(0xdeadbeefcafe0000), a1=42]
FRAME CREATE  id=2 depth=1
  incoming [p0=ref(0xdeadbeefcafe0000), p1=42]
  PASS: ins[0] is ref type (2 == 2)
  PASS: ins[0] == FAKE_OBJ_PTR (1 == 1)
  PASS: ins[1] == 42 (42 == 42)
  PASS: local[0] still has this ref (1 == 1)
  PASS: local[1] still has x (42 == 42)
RETURN  LBar;->doIt  value=void
  PASS: caller v0 (this) preserved (1 == 1)
  PASS: caller v1 (x) preserved (42 == 42)
```

**Verified**: `this` reference (0xDEADBEEFCAFE0000) arrives in `ins[0]` with type_tag=2 (ref). Integer argument 42 arrives in `ins[1]`. Both preserved when copied to locals. Caller's registers unaffected.

---

### Test C: Nested Call A → B → C

**Scenario**: A.main() calls B.add(3,5) which calls C.multiply(8,2). Three frames active simultaneously.

**Key evidence**:
```
INVOKE  LB;->add (II)I
  args [a0=3, a1=5]
  incoming [p0=3, p1=5]
  INVOKE  LC;->multiply (II)I
  args [a0=8, a1=2]
  incoming [p0=8, p1=2]
  RETURN  LC;->multiply  value=16
  PASS: C return value == 16 (16 == 16)
  PASS: B: local[0] (sum) still == 8 after C call (8 == 8)
  RETURN  LB;->add  value=16
  PASS: B return value == 16 (16 == 16)
  PASS: A: v0 == 3 (uncorrupted)
  PASS: A: v1 == 5 (uncorrupted)
  PASS: A: v2 == 999 (uncorrupted)   ← sentinel value
  PASS: A: v3 == 16 (return value)
```

**Verified**: Three independent frames coexist without register corruption. A's v2=999 sentinel proves B and C do not overwrite A's locals. Return value chains correctly: C(16) → B(16) → A.v3(16). B's local[0] (sum=8) survives C's invocation.

---

## 5. Test Summary

| Test | Scenario | Assertions | Result |
|------|----------|------------|--------|
| A | Static `foo(int,int)->int` | 5 | 5 PASS |
| B | Instance `object.method(int)` | 8 | 8 PASS |
| C | Nested A→B→C | 11 | 11 PASS |
| **Total** | | **24** | **24 PASS** |

Full evidence log: `tests/dalvik_frame/test_evidence.log` (118 lines).

---

## 6. Known Limitations

### 6.1 Static Frame Array

Frames are stored in a fixed-size `Frame frames[64]` array on the stack. This limits call depth to 64 and wastes memory for shallow call stacks. A production runtime would use a heap-allocated growable stack or a Dalvik-style interpreted stack with a pointer-based frame chain.

### 6.2 No 64-bit Register Pairs

Dalvik uses pairs of 32-bit registers for `long` and `double` values (e.g., v0/v1 hold one `long`). This prototype stores each register as 64 bits (`uint64_t`), which is wider than Dalvik's 32-bit registers. When integrating with real DEX bytecode, the interpreter must handle wide register pairs correctly — reading v0 as the low word and v1 as the high word, and ensuring both are allocated atomically.

### 6.3 No Actual Bytecode Execution

`frame_invoke` copies arguments and pushes frames, but there is no bytecode interpreter loop. The test code manually simulates method bodies by calling `frame_set_local` and `frame_set_return`. The next experiment (EXP-035+) will integrate this frame model into MiniAndroid's bytecode execution loop.

### 6.4 No Range vs Non-Range Invoke Distinction

In Dalvik, `invoke-virtual {vA, vB, vC}, method` packs up to 5 args from arbitrary registers, while `invoke-virtual/range {vC .. vC+N-1}, method` takes a contiguous range. This prototype accepts a `RegValue[]` array for both cases. The distinction matters when the interpreter copies from caller registers to outgoing slots — non-range invoke reads 5 arbitrary registers, while range invoke reads a contiguous block. The framework supports both; the caller just needs to build the args array differently.

### 6.5 No Synchronized/Monitor Support

Dalvik methods can be `synchronized`, requiring monitor enter/exit around the method body. This prototype has no lock word in the frame. Not needed until object monitors are implemented.

### 6.6 Type Tag Is Ad-Hoc

The `type_tag` field (0=uninit, 1=int, 2=ref, etc.) is a simplified stand-in for Dalvik's full verification type system. A real implementation would use the verifier's type tracking, which distinguishes between concrete class types, array types, uninitialized objects, and constant types.

### 6.7 No DEX Integration

MethodMetadata is created manually in the test code. In production, these values come from `code_item.registers_size`, `code_item.ins_size`, `code_item.outs_size` in the DEX file, plus the method's `access_flags` from `class_def`. The prototype proves the layout works; integration with DEX parsing is the next step.

---

## 7. Deliverables

| File | Lines | Purpose |
|------|-------|---------|
| `src/dalvik/frame.h` | 155 | Frame, FrameStack, MethodMetadata, RegValue declarations + API |
| `src/dalvik/frame.c` | 300 | Frame lifecycle, register access, invoke simulation, trace output |
| `tests/dalvik_frame/test_frame.c` | 330 | 3 test cases (24 assertions), synthetic method bodies |
| `tests/dalvik_frame/test_evidence.log` | 118 | Full execution trace with FRAME CREATE / INVOKE / RETURN |
| `docs/EXP034_REGISTER_FRAME_REPORT.md` | this file | Architecture decision, memory layout, evidence, limitations |

---

## 8. Conclusion

EXP-034 validates that Dalvik's register frame partitioning model (locals | ins | outs) works correctly for the three critical invocation patterns: static methods, instance methods with `this` preservation, and nested call chains with independent register isolation. The 24/24 test pass rate provides evidence that this frame model is a sound foundation for MiniAndroid's interpreter. The known limitations are documented and none block integration into the existing codebase — they are scope boundaries for future experiments, not architectural flaws.