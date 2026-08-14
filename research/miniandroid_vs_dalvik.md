# MiniAndroid vs AOSP Dalvik VM — Comprehensive Gap Analysis

> **Experiment:** EXP-033
> **Date:** 2026-07-30
> **Scope:** MiniAndroid current implementation vs full Android 2.3–4.4 Dalvik VM (pre-ART)
> **Reference:** `research/dalvik_architecture_notes.md`

---

## 1. Executive Summary

MiniAndroid represents an ambitious first step toward a standalone Dalvik-compatible runtime. It successfully parses the most critical DEX file sections—header, string table, type table, method table, class definitions, and code items—and can interpret a small but meaningful subset of Dalvik bytecodes, including `return-void`, `invoke-virtual`, `invoke-direct`, `const-string`, `iget`, `iput`, `sget`, and `new-instance`. It produces execution evidence in the form of instruction traces and register state snapshots, which is valuable for debugging and verification.

However, the implementation is fundamentally incomplete and cannot execute real Android APK code. The gaps span every major subsystem of the Dalvik VM: the DEX parser is missing several essential sections (proto_ids, field_ids, map_list, class_data_item), the bytecode interpreter covers only ~10 out of ~218 opcodes, there is no register frame model compatible with Dalvik's calling convention, no object model for allocating and representing Java objects on a heap, no method resolution capable of virtual dispatch through class hierarchies, no exception handling infrastructure, no garbage collection, and no type verification or class loading pipeline. The architecture is currently best described as a **DEX file reader with a proof-of-concept instruction tracer**, not a virtual machine.

The most critical single blocker preventing MiniAndroid from executing real APK code is the **absence of a Dalvik-compatible register frame and calling convention model**. Without correctly structured frames that map the `code_item`'s `registers_size`, `ins_size`, and `outs_size` fields to actual memory, the interpreter cannot pass arguments to methods, receive return values, manage `this` references, or chain method calls. Every invoked method will immediately encounter undefined register state, causing cascading failures. This must be the first architectural feature implemented before any real bytecode can execute.

---

## 2. DEX Parsing Comparison Table

| DEX Section | Dalvik VM (AOSP) | MiniAndroid Status | Evidence / Notes |
|---|---|---|---|
| **header** (`DexHeader`, 112 bytes) | Full: reads and validates all 19 fields including magic, checksum, SHA-1 signature, endian tag, section offsets/counts | **DONE** | Header is parsed; magic validation and endian tag verified. Checksum and SHA-1 validation status unknown (likely skipped). |
| **string_ids** | Full: array of `u4` offsets into `data[]`, each pointing to a ULEB128-length-prefixed MUTF-8 string | **DONE** | String table indexed and strings dereferenced. Required for `const-string` and method/field name resolution. |
| **type_ids** | Full: array of `u4` indices into `string_ids[]`, representing class/interface/primitive type descriptors like `"Landroid/app/Activity;"` | **DONE** | Type descriptors loaded and indexable. Required for class resolution in `new-instance`, field access, and invoke targets. |
| **proto_ids** | Full: 12-byte structs containing `shorty_idx` (into string_ids), `return_type_idx` (into type_ids), and `parameters_off` (into `type_list`) | **MISSING** | Proto_ids describe method signatures (argument types + return type). Without this, MiniAndroid cannot verify method compatibility, match overridden methods for virtual dispatch, or determine argument passing conventions. |
| **field_ids** | Full: 8-byte structs containing `class_idx` (into type_ids, declaring class), `type_idx` (into type_ids, field type), `name_idx` (into string_ids, field name) | **PARTIAL** | MiniAndroid implements `iget`, `iput`, and `sget` instructions that reference fields, but the `field_ids` table itself may not be parsed as a standalone indexed structure. Field resolution appears to happen through instruction-level references rather than a pre-built field table. This breaks for static field lookups via class hierarchies. |
| **method_ids** | Full: 8-byte structs containing `class_idx` (into type_ids, declaring class), `proto_idx` (into proto_ids, method signature), `name_idx` (into string_ids, method name) | **DONE** | Method table parsed and indexed. Required for all `invoke-*` instructions to resolve the target method by its `method_idx` operand. |
| **class_defs** | Full: 32-byte structs containing `class_idx`, `access_flags`, `superclass_idx`, `interfaces_off`, `source_file_idx`, `annotations_off`, `class_data_off`, `static_values_off` | **PARTIAL** | Class definitions are parsed to locate `code_item` bytecode, but `class_data_off` (which points to `encoded_class_data_item` containing instance/static field lists and direct/virtual method lists) is not fully traversed. `superclass_idx` and `interfaces_off` may be read but not used for hierarchy resolution. |
| **code_item** | Full: `registers_size`, `ins_size`, `outs_size`, `tries_size`, `debug_info_off`, `insns_size`, `insns[]`, optional `try_item[]` + encoded catch handlers | **PARTIAL** | `insns[]` bytecodes extracted and interpreted. `registers_size`, `ins_size`, and `outs_size` are likely read from the struct but not used to construct proper execution frames. `tries_size` and associated exception handler tables are not processed. |
| **map_list** | Full: section directory (`DexMapList`) mapping section types to offsets/sizes; used for validation and integrity checking | **MISSING** | Map list provides a complete directory of all sections in the DEX file. Its absence means MiniAndroid cannot perform integrity validation or dynamically discover section locations. This is tolerable for well-formed DEX files but will fail on obfuscated or multi-dex files. |
| **class_data_item** (`encoded_class_data_item`) | Full: LEB128-encoded structure containing `static_fields_size`, `instance_fields_size`, `direct_methods_size`, `virtual_methods_size`, followed by lists of `encoded_field` and `encoded_method` entries (each with `field_id_idx`/`method_id_idx` plus access flags and code/data offsets) | **MISSING** | This is the critical bridge between `class_defs` and the actual field/method runtime structures. Without parsing this, MiniAndroid cannot enumerate which methods a class declares, which fields it has, or what their access modifiers are. This directly blocks object instantiation, field storage layout, and method dispatch table construction. |

**Summary:** 3 sections fully parsed, 3 partially parsed, 4 missing. The missing `proto_ids`, `class_data_item`, and `map_list` sections represent fundamental gaps in the type system, class model, and validation infrastructure respectively.

---

## 3. Bytecode Interpreter Comparison

Dalvik defines approximately **218 opcodes** across 13 instruction format families. MiniAndroid currently implements a small subset sufficient for tracing simple method bodies but far short of what is needed to execute real Android application code.

### Format Families Overview

| Format | Description | Dalvik Count | MiniAndroid Count |
|---|---|---|---|
| **12x** | Move operations, return, monitor | ~15 | 1 (`return-void`) |
| **11x** | Move result, throw, etc. | ~6 | 0 |
| **10t** | Unconditional goto | 1 | 0 |
| **23x** | Move-object, move-wide, etc. | ~10 | 0 |
| **32x** | if-test (2-register comparisons) | 6 | 0 |
| **31t** | Conditional branches (1-register + offset) | 6 | 0 |
| **30t** | goto/32, packed-switch, sparse-switch | 3 | 0 |
| **21c/22c** | const-string, const-class, check-cast, new-instance, sget, sput | ~10 | 3 (`const-string`, `new-instance`, `sget`) |
| **31c** | const-string/jumbo | 1 | 0 |
| **35c** | invoke-virtual, invoke-super, invoke-direct, invoke-static, invoke-interface | 5 | 2 (`invoke-virtual`, `invoke-direct`) |
| **3rc** | invoke-virtual/range, invoke-direct/range, etc. | 5 | 0 |
| **51l** | const (wide / 32-bit) | 1 | 0 |

### Implemented Opcodes (MiniAndroid)

| Opcode | Hex | Format | Purpose |
|---|---|---|---|
| `return-void` | `0x0e` | 12x | Return from void method |
| `invoke-virtual` | `0x6e` | 35c | Call virtual method on object |
| `invoke-direct` | `0x70` | 35c | Call constructor or private method |
| `const-string` | `0x1a` | 21c | Load string constant into register |
| `iget` | `0x52` | 22c | Read instance field from object |
| `iput` | `0x59` | 22c | Write instance field to object |
| `sget` | `0x63` | 21c | Read static field from class |
| `new-instance` | `0x22` | 22c | Allocate new object (no `<init>` call) |

**Estimated total implemented:** ~8 opcodes out of ~218 (approximately 3.7%)

### Missing Opcodes — Critical for Execution

The following opcodes are **essential** for executing even a trivial Android app and are completely absent from MiniAndroid:

#### Move / Load Operations
| Opcode | Hex | Why Critical |
|---|---|---|
| `move` | `0x01` | Fundamental register-to-register copy; used everywhere |
| `move-object` | `0x07` | Object reference passing; needed for `this` propagation |
| `move-result` | `0x0a` | Captures return values from `invoke-*`; without this, no method can return a value to its caller |
| `move-result-object` | `0x0c` | Captures object return values |
| `move-wide` | `0x05` | 64-bit value movement (long/double) |
| `const/4` | `0x12` | Load small integer constants (4-bit immediate, -8..7) |
| `const/16` | `0x13` | Load 16-bit integer constants |
| `const` | `0x14` | Load 32-bit integer constants |
| `const/high16` | `0x15` | Load high 16 bits of constant |

#### Control Flow
| Opcode | Hex | Why Critical |
|---|---|---|
| `goto` | `0x28` | Unconditional branch; basic loops and jumps |
| `if-eq`, `if-ne` | `0x32`, `0x33` | Equality comparisons; needed for null checks, string comparisons |
| `if-eqz`, `if-nez` | `0x38`, `0x39` | Null checks on object references; ubiquitous in Android code |
| `if-lt`, `if-ge`, `if-gt`, `if-le` | `0x34–0x37` | Numeric comparisons |
| `packed-switch` | `0x2b` | Switch statements compiled from Java switch blocks |
| `sparse-switch` | `0x2c` | Sparse switch tables |

#### Invoke Family (Missing)
| Opcode | Hex | Why Critical |
|---|---|---|
| `invoke-static` | `0x71` | Static method calls; needed for `Log.d()`, factory methods, utility calls |
| `invoke-super` | `0x6f` | Superclass method dispatch; essential in `Activity.onCreate()` calling `super.onCreate()` |
| `invoke-interface` | `0x72` | Interface method dispatch; used by View.OnClickListener, collections, etc. |
| `invoke-virtual/range` | `0x74` | Range variant for methods with >5 arguments |
| `invoke-direct/range` | `0x76` | Range variant for constructors with many args |

#### Field Operations (Missing)
| Opcode | Hex | Why Critical |
|---|---|---|
| `sput` | `0x64` | Write static field; needed to set static state |
| `iget-wide`, `iget-object` | `0x54`, `0x56` | Wide and object field reads |
| `iput-wide`, `iput-object` | `0x5d`, `0x5f` | Wide and object field writes |

#### Type / Casting
| Opcode | Hex | Why Critical |
|---|---|---|
| `check-cast` | `0x1c` | Runtime type check before casting; used throughout Android SDK |
| `instance-of` | `0x20` | Type checking for instanceof expressions |

#### Array Operations
| Opcode | Hex | Why Critical |
|---|---|---|
| `aget` | `0x44` | Read array element |
| `aput` | `0x4b` | Write array element |
| `aget-object` | `0x46` | Read object from array |
| `new-array` | `0x23` | Allocate array |
| `filled-new-array` | `0x24` | Allocate and initialize array |
| `array-length` | `0x21` | Get array length |

#### Arithmetic (Missing — all)
| Category | Opcodes | Why Critical |
|---|---|---|
| Add/Sub/Mul/Div/Rem (int) | `0x90–0x98` | Basic math in any program |
| Neg/Not/Not-int | `0x7b–0x7d` | Unary operations |
| And/Or/Xor | `0x99–0x9f` | Bitwise operations, flag manipulation |
| Shl/Shr/Ushr | `0xa0–0xa5` | Bit shifting |
| Wide variants | `0xdb–0xe3` | 64-bit arithmetic (long/double) |
| Cmp/Cmpl/Cmpg | `0x2d–0x2f` | Float/double comparisons |

#### Object Operations (Missing)
| Opcode | Hex | Why Critical |
|---|---|---|
| `instance-of` | `0x20` | Type checking |
| `throw` | `0x27` | Exception throwing |
| `monitor-enter` | `0x1d` | Synchronized block entry |
| `monitor-exit` | `0x1e` | Synchronized block exit |

**Total missing:** ~210 opcodes. Even for a minimal "Hello World" Android app, MiniAndroid would need at minimum an additional **30–40 opcodes** to trace through `Activity.onCreate()` → `setContentView()` → `TextView.setText()`.

---

## 4. Register Frame Model

This is the **most architecturally significant gap** in MiniAndroid and the most likely execution blocker (see Section 10).

### Dalvik VM Frame Layout

Dalvik uses a contiguous frame structure allocated on the interpreter stack. Each frame corresponds to one method activation. The frame layout is determined by three fields from the `code_item`:

```c
u2 registers_size;   // Total number of 32-bit registers used
u2 ins_size;          // Number of registers for incoming arguments
u2 outs_size;         // Number of registers for outgoing arguments (to callees)
```

The frame layout in memory (growing downward, lower addresses at top):

```
+====================+  <-- fp (frame pointer, points to start of this frame)
|   out[N-1]         |  \  outs_size registers
|   ...              |   | (shared with callee's ins)
|   out[0]           |  /
+--------------------+
|   saved method ptr |  Pointer to current Method* (or class/method metadata)
+--------------------+
|   padding (0–3)    |  Align to 8-byte boundary
+--------------------+
|   v[reg_size-1]    |  \  Local variable registers
|   ...              |   | (v[0]..v[ins_size-1] = incoming args for non-static)
|   v[0]             |  /
+--------------------+  <-- fp + (reg_size + padding/4 + 1)
```

Key details from the AOSP implementation:

1. **Incoming arguments map to the FIRST `ins_size` registers** (`v[0]` through `v[ins_size-1]`). For instance methods, `v[0]` = `this`. For static methods, `v[0]` = first argument.

2. **Outgoing arguments are placed at the BOTTOM of the frame** (`fp[0]` through `fp[outs_size-1]`). These physically overlap with the callee's incoming registers when the callee's frame is pushed directly above the current frame (no copy needed — Dalvik shares this memory).

3. **The saved method pointer** sits between the outs area and the locals. It identifies the currently executing method for debugging, exception unwinding, and `invoke-*` resolution.

4. **Wide values** (long, double) occupy two consecutive register slots `(vN, vN+1)`. The low 32 bits go in the lower-numbered register.

5. **Frame allocation:** When `invoke-*` is executed, the interpreter pushes a new frame above the current one. The `outs_size` of the caller must be >= the `ins_size` of the callee. Arguments are written into `fp[0]..fp[ins_size-1]` before the callee starts executing.

### MiniAndroid's Current Approach

MiniAndroid does **not implement Dalvik-compatible register frames**. Based on the available evidence:

- Registers are likely represented as a flat array or dictionary keyed by register number (`v0`, `v1`, etc.), decoupled from the `code_item`'s `registers_size`/`ins_size`/`outs_size` fields.
- There is no frame stack — each method call does not push a new frame with properly partitioned ins/outs/locals areas.
- The caller→callee argument passing mechanism is undefined. When `invoke-virtual` is executed, the registers containing `this` and arguments are read from the current flat register state, but there is no defined protocol for how those values appear in the callee's register namespace.
- There is no result register mechanism. Dalvik requires that `invoke-*` for non-void methods stores the return value in a result register that is subsequently consumed by `move-result*`. MiniAndroid has neither the result register nor the `move-result*` opcode.

**Impact:** Without a proper frame model, method chaining is impossible. Consider `Activity.onCreate()`:

```
invoke-super {v0, v1}          // this = v0, Bundle = v1
invoke-virtual {v0, v2}        // this = v0, layoutRes = v2
```

After `invoke-super` returns, the interpreter must:
1. Pop the super's frame
2. Restore `v0` (`this`) in the caller's frame
3. Place `v2` (a resource ID loaded by `const` earlier) correctly for the next call

With a flat register array and no frame stack, `v0` and `v2` may be clobbered or undefined after the super call returns.

### Comparison Diagram

```
DALVIK (CORRECT):
  Frame for onCreate()         Frame for super.onCreate()
  ┌──────────┐                 ┌──────────┐
  │ out[0]=v0│ ──shared──────>│ in[0]=v0 │ (this)
  │ out[1]=v1│ ──shared──────>│ in[1]=v1 │ (savedState)
  ├──────────┤                 ├──────────┤
  │ method*  │                 │ method*  │
  ├──────────┤                 ├──────────┤
  │ v[N-1]   │                 │ v[M-1]   │
  │ ...      │                 │ ...      │
  │ v2=layout│                 │ v1       │
  │ v1=saved │                 │ v0=this  │
  │ v0=this  │                 └──────────┘
  └──────────┘

MINIANDROID (BROKEN):
  ┌──────────────────────────────────┐
  │ Flat dict/array:                 │
  │   v0 = ??? (clobbered?)         │
  │   v1 = ???                       │
  │   v2 = ???                       │
  │ (no frame boundary, no outs)     │
  └──────────────────────────────────┘
```

---

## 5. Object Model Gap Analysis

### Dalvik VM Object Model

Every Java/Kotlin object in Dalvik is represented as an `Object` struct at runtime:

```c
struct Object {
    ClassObject* clazz;    // Pointer to the object's ClassObject (vtable, field layout)
    u4             lock;    // Thin lock or fat lock word (synchronization, GC bits)
    // Followed by instance fields, ordered per class hierarchy
};
```

The `ClassObject` is a complex, multi-field structure (approximately 80+ bytes in AOSP) containing:

```c
struct ClassObject {
    // Identity
    const char*     descriptor;        // "Lcom/example/MyActivity;"
    ClassObject*    superClass;        // Direct superclass
    ClassObject*    componentType;     // For arrays, the element type
    Object*         classLoader;       // Defining ClassLoader

    // Access
    u4              accessFlags;       // ACC_PUBLIC, ACC_FINAL, ACC_INTERFACE, etc.

    // Instance layout
    int             instanceDataOffset; // Byte offset of first instance field in Object
    u4              objectSize;         // Total size in bytes (including Object header)

    // Fields
    InstField*      instanceFields;    // Array of instance field descriptors
    StaticField*    staticFields;      // Array of static field descriptors
    int             fieldCount;         // Total fields

    // Methods
    Method**        virtualMethods;     // Virtual method table (vtable)
    Method**        directMethods;      // Direct methods (static, private, constructor)
    int             virtualMethodCount;
    int             directMethodCount;
    int             vtableCount;        // Size of vtable (may include super's methods)

    // State
    int             status;             // CLASS_INITIALIZED, CLASS_ERROR, etc.
    struct DvmDex* pDvmDex;           // Back-pointer to the DEX file

    // Interface dispatch
    int             ifTableCount;       // Number of interface entries
    IfTable*        ifTable;            // Interface dispatch table
};
```

The `Method` structure contains:
```c
struct Method {
    ClassObject*    clazz;             // Declaring class
    u4              accessFlags;
    u2              methodIndex;       // Index in method_ids[]
    u2              registersSize;     // From code_item
    const char*     name;              // Method name (e.g., "onCreate")
    DexProto        prototype;         // Return type + parameter types
    const u2*       insns;             // Bytecode pointer
    int             insSize;
    int             outsSize;
    // ... JNI, native flags, profiling, etc.
};
```

### MiniAndroid's Object Representation

MiniAndroid has **no object model** in the Dalvik sense. The evidence:

- `new-instance` allocates something, but there is no `Object` struct with a `clazz` pointer and lock word.
- `iget`/`iput` read and write fields, but without a field layout computed from the `ClassObject`'s `instanceFields` array, the byte offsets for each field are undefined.
- There is no `ClassObject` structure. Types are represented only as string descriptors (e.g., `"Landroid/app/Activity;"`) without any runtime metadata.
- There is no vtable or virtual method dispatch table. `invoke-virtual` likely resolves the target method by looking up the method name directly rather than performing virtual dispatch through a class hierarchy.
- Static fields (`sget`) are likely stored in a flat dictionary keyed by field name, not in the `StaticField` array associated with the declaring class.

### Impact on Execution

Without an object model, the following operations are impossible:

1. **`instanceof` checks** — requires comparing `clazz` pointers up the superclass chain.
2. **Virtual dispatch** — requires walking the vtable to find the correct override for the receiver's actual runtime type.
3. **Field layout** — `iget`/`iput` require knowing the byte offset of each field within the object, which depends on the field sizes and ordering across the entire class hierarchy (superclass fields come first, then subclass fields, aligned to 4 or 8 bytes).
4. **Synchronization** — the lock word in the Object header is required for `monitor-enter`/`monitor-exit`.
5. **`toString()` / `hashCode()`** — identity-based operations depend on the object's address or a stored identity hash code.

---

## 6. Method Resolution & Dispatch

### Dalvik VM Method Resolution

Dalvik defines four invoke instruction types, each with distinct resolution semantics:

| Invoke Type | Opcode | Resolution | When Used |
|---|---|---|---|
| **invoke-virtual** | `0x6e` | **Virtual dispatch:** Resolve at runtime based on the actual type of the receiver object (`this`). Walks the vtable of `this.clazz` to find the method. | Instance methods (non-private, non-constructor). Most common. |
| **invoke-super** | `0x6f` | **Superclass dispatch:** Resolve based on the *declaring class* of the current method (not the receiver's type). Used to call the superclass implementation when a subclass overrides it. | `super.method()` calls in Java. Essential in `Activity.onCreate()`. |
| **invoke-direct** | `0x70` | **Direct (non-virtual):** Resolve based on the declaring class in the method_ids entry. No virtual dispatch. | Private methods, constructors (`<init>`, `<clinit>`), static methods (historically). |
| **invoke-static** | `0x71` | **Static dispatch:** Resolve based on declaring class. No receiver object. | Static methods. |
| **invoke-interface** | `0x72` | **Interface dispatch:** Resolve by searching the receiver's `ifTable` (interface dispatch table) for the matching interface method. Slower than virtual dispatch. | Interface method calls. Collections, listeners, callbacks. |

### Resolution Process in Detail

For `invoke-virtual`, the AOSP Dalvik interpreter performs these steps:

1. Read `method_idx` from the instruction's `@BBBB` operand.
2. Look up `method_ids[method_idx]` to get the class index, proto index, and name index.
3. Resolve the class to a `ClassObject*`.
4. Determine the actual class of the receiver: `this->clazz`.
5. Search the vtable of `this->clazz` for a method matching the name and prototype.
6. If not found in the current class, walk up the superclass chain.
7. If the method is abstract, throw `AbstractMethodError`.
8. If the method is native, call through JNI.
9. Otherwise, set up the frame and begin interpreting the method's bytecodes.

### MiniAndroid's Method Resolution

MiniAndroid implements `invoke-virtual` and `invoke-direct` but the resolution mechanism is almost certainly simplified:

- **No vtable lookup:** `invoke-virtual` likely resolves the target by matching the method name from `method_ids` directly, without considering the receiver's actual runtime type. This means all `invoke-virtual` calls behave as if they were `invoke-direct` — no polymorphism.
- **No `invoke-super`:** The `super.onCreate(bundle)` call at the top of every `Activity.onCreate()` method **cannot be executed**. This is a hard blocker for any Android Activity code.
- **No `invoke-static`:** Static method calls are impossible. This blocks `Log.d()`, `LayoutInflater.from()`, factory methods, and utility class calls.
- **No `invoke-interface`:** Interface dispatch through the `ifTable` is not implemented. This blocks callback patterns (`View.OnClickListener`), collection framework calls, and any interface-based API.
- **No native method support:** Dalvik methods with `ACC_NATIVE` flag are resolved through JNI. MiniAndroid has no JNI bridge, so any call to a native framework method (which includes most Android API calls like `setContentView()`, `getWindow()`, etc.) will fail.

### Dispatch Chain for `Activity.onCreate()`

```
MyActivity.onCreate(savedInstanceState: Bundle)
  │
  ├── invoke-super {v0, v1}       // super.onCreate(bundle)  ← BLOCKED: no invoke-super
  │     └── Activity.onCreate(bundle)
  │           └── FragmentActivity.onCreate(bundle)   // if using support lib
  │
  ├── const v2, 0x7f0b0001        // R.layout.activity_main
  │
  ├── invoke-virtual {v0, v2}     // this.setContentView(layout)  ← BLOCKED: setContentView is NATIVE
  │     └── AppCompatActivity.setContentView(int)
  │           └── JNI → android.app.Activity.setContentVew()  ← BLOCKED: no JNI
  │
  ├── const-string v3, "Hello"    // "Hello World"
  │
  └── invoke-virtual {v0, v3}     // textView.setText("Hello")  ← BLOCKED: native
```

**Every single method call in this chain is blocked** in MiniAndroid's current state.

---

## 7. Exception Handling

### Dalvik VM Exception Handling

Dalvik implements Java exception handling through a multi-layered system:

#### 1. Structured Exception Tables in DEX

Exception information is embedded in the `code_item` after the `insns[]` array:

```c
struct DexTry {
    u4 start_addr;     // Bytecode offset of try block start (in 16-bit code units)
    u2 insn_count;     // Number of code units in try block
    u2 handler_off;    // Offset to encoded_catch_handler_list (relative to this DexTry)
};
```

Each `try_item` maps a range of bytecode addresses to a list of catch handlers. The handlers are LEB128-encoded and support both typed catches (`catch (Exception e)`) and catch-all handlers (`catch (Throwable t)` or `finally` blocks).

#### 2. Interpreter-Level Exception Propagation

When an exception is thrown (via the `throw` opcode, or by the runtime when an error occurs):

1. An `Object*` representing the exception is created (a `java.lang.Throwable` subclass).
2. The interpreter walks the current method's `try_items` to find a matching handler.
3. If found, control transfers to the handler's bytecode offset, and the exception reference is placed in the target register specified by the `encoded_catch_handler`.
4. If no handler matches in the current frame, the interpreter **unwinds to the caller's frame** and repeats the search.
5. This continues up the call stack until either a handler is found or the thread's `UncaughtExceptionHandler` is invoked.

#### 3. Pending Exception State

Dalvik maintains a per-thread "pending exception" pointer. Between the throw and the handler, the interpreter sets `dvmExceptionOccurred()` to return the exception object. Method prologues check for pending exceptions and short-circuit to handler lookup rather than executing the method body.

#### 4. `finally` Blocks

`finally` blocks are compiled as catch-all handlers (`type_idx = NO_INDEX`) that re-throw the exception after executing their body. If the `try` block exits normally (not via exception), a synthetic `goto` skips the catch-all handler. If it exits via exception, the catch-all runs and then jumps to the re-throw.

### MiniAndroid's Exception Support

MiniAndroid has **no exception handling** in any form:

- The `throw` opcode (0x27) is not implemented.
- `try_items` in `code_item` are not parsed.
- LEB128-encoded catch handler lists are not decoded.
- There is no pending exception mechanism.
- There is no frame unwinding for exception propagation.
- There is no `finally` block support.

**Impact:** Any bytecode that throws an exception (null pointer dereference, class cast failure, array index out of bounds) will cause undefined behavior or a crash rather than controlled propagation to a handler. More critically, many Android framework methods use try/catch internally (e.g., `Parcel.readException()`), so even if native methods were implemented, their exception flow would not work correctly.

---

## 8. Memory Management

### Dalvik VM Memory Management

Dalvik manages memory through a full garbage-collected runtime:

#### Heap Structure
- **Java heap:** Managed by the garbage collector, grows and shrinks as needed. Divided into two spaces:
  - **Zygote space:** Objects created during Zygote startup (pre-forked, shared across processes via copy-on-write).
  - **Alloc space:** Per-process heap for objects allocated after fork.
- **Native heap:** Unmanaged, used for JNI allocations and internal VM structures.

#### Object Allocation
- `new-instance` allocates from the Java heap: `dvmAllocObject(clazz, flags)`.
- Allocation walks the class hierarchy to compute total object size (`clazz->objectSize`).
- The allocator updates the GC's allocation bitmap, checks for heap growth needs, and may trigger concurrent GC.
- Objects are zero-initialized (fields default to null/0/false) per the JLS.

#### Garbage Collection
- Dalvik uses a **mark-sweep-compact** GC (pre-4.0) or a **concurrent mark-sweep** GC (4.0+, "Garbage First" style).
- The GC maintains a card table (for generational write barriers) and a live bitmap.
- Every `Object` header contains GC bits used for mark phase.
- `GC_FOR_ALLOC` is triggered when the allocator cannot satisfy a request.
- `GC_CONCURRENT` runs concurrently with mutator threads.
- `GC_EXPLICIT` is triggered by `System.gc()`.

#### References
- Strong references (normal Java references).
- Weak references (`java.lang.ref.WeakReference`), soft references, phantom references.
- JNI local/global/weak global references.

### MiniAndroid's Memory Management

MiniAndroid has **no memory management subsystem**:

- There is no Java heap. Object allocation (`new-instance`) likely allocates from the host process's native heap (e.g., `malloc`/`Box::new()` in Rust), not from a managed heap.
- There is no garbage collector. Any allocated objects are never freed (memory leak) unless manually managed.
- There is no zero-initialization of object fields. Fields may contain garbage data after allocation, violating the JLS requirement that all fields default to null/0/false.
- There is no object size computation based on class hierarchy. `new-instance` cannot determine how much memory to allocate without walking the class's field list.
- There is no card table, bitmap, or any GC bookkeeping.
- JNI references are not relevant since there is no JNI bridge.

**Impact:** Memory leaks are guaranteed. More critically, the lack of zero-initialization means that `iget` on a newly allocated object may read uninitialized garbage, causing type confusion or crashes downstream.

---

## 9. Type System & Verification

### Dalvik VM Type System & Verification

#### Class Loading
Dalvik loads classes lazily (on first use) or eagerly (during pre-verification):
1. The class loader receives a class descriptor (e.g., `"Landroid/app/Activity;"`).
2. It searches the DEX file's `class_defs` table for a matching entry.
3. If found, it parses the `class_data_item` to enumerate fields and methods.
4. It resolves the superclass and interfaces by recursively loading them.
5. It links virtual methods into the vtable, performing override resolution.
6. It runs the class initializer (`<clinit>`) if present.

#### Verification
Dalvik performs bytecode verification to ensure safety:
- **Pre-verification (at DEX creation / install time):** The verifier assigns register types to each instruction in a dataflow analysis pass. It checks:
  - Type consistency: registers hold the correct types for their uses.
  - Null tracking: objects are checked for null before method calls.
  - Stack depth: the operand stack never underflows.
  - Branch targets: all branch targets have consistent merged register types.
- **Runtime verification (at class load time):** For classes that cannot be pre-verified (e.g., dynamically generated), the verifier runs at class load time using a full abstract interpretation.

#### Primitive Widening/Narrowing
Dalvik's bytecode uses explicit widening/narrowing opcodes:
- **Widening:** `int-to-long` (0x85), `int-to-float` (0x82), `int-to-double` (0x86), `float-to-double` (0x8b)
- **Narrowing:** `long-to-int` (0x88), `float-to-int` (0x8d), `double-to-int` (0x8e), `double-to-long` (0x8a)
- Some implicit widening is allowed: `iadd` can take a byte operand that is sign-extended to int.

#### Type Categories
Dalvik classifies types into categories (important for method dispatch and register allocation):
- **Category 1:** int, float, byte, char, short, boolean, references (1 register slot)
- **Category 2:** long, double (2 register slots)

### MiniAndroid's Type System

MiniAndroid has **no type system**:

- There is no class loading pipeline. Classes are not loaded, linked, or initialized.
- There is no verification. Bytecodes are executed directly without type checking.
- Register types are not tracked. A register holding an `int` in one instruction can be used as an object reference in the next without any detection.
- Primitive widening/narrowing opcodes are not implemented (part of the missing ~210 opcodes).
- Type categories (1 vs 2) are not enforced. Wide values may corrupt adjacent registers.
- Class hierarchy relationships (superclass, interfaces) are not modeled.
- The `check-cast` opcode is not implemented, so unsafe downcasts will not be caught.

**Impact:** Without verification and type tracking, the interpreter is unsafe. Type confusion bugs will manifest as memory corruption or segfaults rather than controlled `ClassCastException` errors. This is acceptable for a research prototype but makes execution of arbitrary APK code unreliable.

---

## 10. First Execution Blocker Identification

### Analysis

Having examined all nine subsystem gaps, the question is: **what is the SINGLE most likely blocker preventing MiniAndroid from executing real APK code?**

The candidates are:
1. Missing register frame model
2. Missing opcodes (~210 out of 218)
3. Missing object model
4. Missing native method/JNI bridge
5. Missing `invoke-super` (blocks every Activity lifecycle method)
6. Missing `invoke-static`
7. Missing exception handling
8. Missing GC/allocation

### Verdict: The Missing Register Frame Model

**The missing register frame model is the single most critical blocker.**

### Reasoning

The register frame model is foundational — it is the mechanism by which **data flows between methods**. Without it, nothing else works, because:

1. **Method arguments cannot be passed correctly.** When `invoke-direct` or `invoke-virtual` is executed, the callee must receive its arguments in the first `ins_size` registers of its own frame. Without frames, the callee reads from undefined register state. The interpreter may read garbage or values from the caller's frame that don't correspond to the expected argument positions.

2. **Return values cannot be propagated.** Dalvik's `invoke-*` stores return values in the caller's result register, which is then consumed by `move-result*`. MiniAndroid doesn't implement `move-result*` and has no result register concept. Without a frame boundary, there is no well-defined location to place return values.

3. **`this` references are lost across calls.** In an instance method chain like `this.foo()` → `this.bar()`, the `this` reference in `v0` must survive across the call to `foo()` and be available when `bar()` is invoked. With flat registers and no frame push/pop, `v0` will be clobbered by `foo()`'s own register usage.

4. **Recursive calls are impossible.** Any method that calls itself (even indirectly) requires separate register states for each activation. A flat register model collapses all activations into a single namespace, making recursion fundamentally broken.

5. **It blocks the interpreter at the very first method call.** Even a trivial test program:
   ```
   method_1: const-string v0, "test"
             invoke-virtual {v0}  // String.length()
             move-result v1
             return v1
   ```
   requires: (a) `v0` to be passed as the receiver in the callee's frame, (b) `String.length()` to execute in its own register namespace, (c) the return value to be placed in the caller's result register, (d) `move-result` to copy it to `v1`. Every step requires the frame model.

### Evidence

- The `code_item` struct contains `registers_size`, `ins_size`, and `outs_size` precisely because the frame model is the foundation of Dalvik's execution semantics. MiniAndroid reads the `code_item` but does not use these fields to construct frames.
- The AOSP interpreter (`dvmInterpret()` in `interp/InterpC-all.c`) allocates a frame before every method invocation: `curFrame = (u4*) alloca(...)` or equivalent. The frame pointer (`fp`) is the most frequently referenced variable in the interpreter loop — every register access goes through `fp[vA]`.
- The `invoke-*` instructions in Dalvik have the format `invoke-kind {vC, vD, vE, vF, vG}, meth@BBBB` where the registers `{vC..vG}` are the caller's registers that contain the arguments, and these arguments are written into the callee's incoming register area (`fp[0..ins_size-1]`). Without frames, this write has nowhere to go.

### Fix Priority

1. **Implement frame allocation** — a stack of frame structures, each containing:
   - `outs: [u32; outs_size]` — argument passing area
   - `locals: [u32; registers_size]` — local variables
   - Method metadata pointer
2. **Frame push on invoke** — copy caller's argument registers into callee's `outs`, push frame, set callee's `ins` = shared with `outs`
3. **Frame pop on return** — pop callee frame, write return value to caller's result slot
4. **Implement `move-result*`** — read from the result slot into caller's register

This is the **minimum viable fix** that unblocks all other subsystem work. Without frames, adding more opcodes, an object model, or a GC is futile — none of them can function correctly in a flat register namespace.

---

## 11. Recommended Minimal Architecture

To execute a minimal Android "Hello World" app (`Activity.onCreate()` → `setContentView()` → `TextView.setText()`), MiniAndroid needs the following minimum feature set, listed in implementation order:

### Phase 1: Execution Foundation (Must-Have, Blocks Everything Else)

| Feature | Description | Effort |
|---|---|---|
| **Register Frame Model** | Stack-allocated frames with `outs_size`/`ins_size`/`registers_size` partitioning. Frame push on invoke, pop on return. | High — architectural core |
| **`move-result*`** | Copy return value from callee's result to caller's register. | Low — depends on frames |
| **`move`/`move-object`** | Register-to-register copy. | Low |
| **`const/4`, `const/16`, `const`** | Load integer constants. | Low |
| **`invoke-super`** | Superclass dispatch (required for `super.onCreate()`). | Medium — requires class hierarchy |
| **`invoke-static`** | Static method dispatch. | Medium — requires class resolution |
| **`goto`, `if-eqz`, `if-nez`** | Basic control flow. | Low |

### Phase 2: Object Model (Required for Instance Operations)

| Feature | Description | Effort |
|---|---|---|
| **Object struct** | `{ clazz: *ClassObject, lock: u32, fields... }` | Medium |
| **ClassObject struct** | Minimal: descriptor, superclass ptr, vtable, instance field layout, objectSize | High |
| **Field offset computation** | Walk class hierarchy, assign offsets to instance fields based on type size | Medium |
| **Object allocation with zero-init** | `new-instance` allocates `ClassObject.objectSize` bytes, zeros all fields | Medium |
| **`iget-object`, `iput-object`** | Object-typed field access (needed for `this.textView`) | Low — depends on field offsets |
| **`check-cast`** | Runtime type check using `clazz` pointer and superclass chain | Medium |

### Phase 3: Android Framework Bridge (Required to Call Android APIs)

| Feature | Description | Effort |
|---|---|---|
| **Native method stub framework** | Register native method implementations for Android framework methods. | High |
| **`Activity.onCreate()` stub** | No-op native implementation that records the call. | Low |
| **`setContentView(int)` stub** | Record the layout resource ID. | Low |
| **`TextView.setText(String)` stub** | Record the string argument. | Low |
| **`LayoutInflater` / `View` construction** | At minimum, return a non-null object for `findViewById()`. | Medium |
| **String object model** | `java.lang.String` representation (or pass-through as host strings). | Low |

### Phase 4: Reliability (Required for Real APKs)

| Feature | Description | Effort |
|---|---|---|
| **Exception handling** | Parse `try_items`, implement `throw`, frame unwinding. | High |
| **Basic GC** | Even a simple stop-the-world mark-sweep would prevent memory exhaustion. | High |
| **Class loading pipeline** | Parse `class_data_item`, resolve superclass chains, build vtables. | High |
| **Additional opcodes** | ~30–40 more opcodes for arithmetic, array operations, comparisons. | Medium (each opcode is small, but there are many) |
| **Type tracking** | Basic register type tracking to prevent type confusion in the interpreter. | Medium |

### Execution Path for Minimal Hello World

```
[DEX Parser]
  Load classes.dex
  Parse class_defs for MyActivity
  Find MyActivity.onCreate code_item
  Extract bytecodes
       ↓
[Register Frame Model]
  Push frame for MyActivity.onCreate(registers=4, ins=2, outs=2)
  Map: v0=this (MyActivity), v1=savedInstanceState (Bundle)
       ↓
[Bytecode Interpreter]
  const/4 v2, 0x7f0b0001        // R.layout.activity_main  ← NEED: const opcodes
  invoke-super {v0, v1}          // super.onCreate()       ← NEED: invoke-super + frames
    → Push frame for Activity.onCreate()
    → Execute (no-op stub or simple code)
    → Pop frame, return void
       ↓
  invoke-virtual {v0, v2}        // setContentView(layout)  ← NEED: native stub
    → Native stub: record layoutResId
       ↓
  const-string v3, "Hello World" // Load string             ← DONE (already works)
  invoke-virtual {v0, v3}        // textView.setText()     ← NEED: native stub
    → Native stub: record string
       ↓
  return-void                                             ← DONE (already works)
```

**Minimum to get here:** Phase 1 (7 features) + Phase 2 (4 features) + Phase 3 (3 stubs) = **14 features** beyond what exists today. The register frame model alone unlocks 6 of the 14.

---

## Appendix: DEX Section Interdependency Graph

```
header ─────────────────────────────────────────────────────┐
  ├── string_ids ──────────────────────────────────────────┐ │
  ├── type_ids ─────┐ (indices into string_ids)          │ │
  ├── proto_ids ────┤ (type_ids + string_ids)             │ │
  │   └── type_list ┘                                      │ │
  ├── field_ids ─────┘ (type_ids + string_ids)             │ │
  ├── method_ids ────┘ (type_ids + proto_ids + string_ids) │ │
  ├── class_defs ──────────────────────────────────────────┤ │
  │   └── class_data_item ──┬── encoded_field              │ │
  │                          └── encoded_method             │ │
  ├── code_item ────────────┤ (referenced by class_data)   │ │
  │   ├── insns[]           │                              │ │
  │   ├── try_items         │                              │ │
  │   └── encoded_catch_handler │                          │ │
  └── map_list ────────────────────────────────────────────┘ │
                                                              ↓
                      [Interpreter consumes all of the above]
```

Sections marked as **MISSING** in MiniAndroid (`proto_ids`, `map_list`, `class_data_item`) are highlighted in bold above. Note that `class_data_item` is the critical bridge — it connects `class_defs` to `code_item` through `encoded_method` entries, and it also declares the instance and static fields that define the object memory layout.

---

*End of document. This gap analysis should be used to prioritize implementation work for EXP-033. The register frame model is the single highest-priority item.*
