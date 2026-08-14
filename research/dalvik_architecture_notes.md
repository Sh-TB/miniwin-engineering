# Dalvik VM Architecture — Research Notes for EXP-033

> **Purpose:** Comprehensive reference for implementing a Dalvik-compatible runtime.
> **Scope:** Android 2.3–4.4 (Dalvik); pre-ART, 32-bit ARM/x86 focus.

---

## 1. DEX File Format

The `.dex` file is the byte-code container that the Dalvik VM consumes. It is a densely packed, index-heavy binary format designed for minimal memory footprint and fast class lookup. Every `.dex` begins with a fixed-size header (`DexHeader`) that encodes the file's geometry — byte offsets, sizes, and counts for every logical section that follows.

### Header (`DexHeader`)

The header is exactly 112 bytes and occupies the very beginning of the file:

```c
struct DexHeader {
    u1  magic[8];           // "dex\n035\0" or "dex\n037\0"
    u4  checksum;           // Adler32 of everything after magic
    u1  signature[20];      // SHA-1 hash of everything after signature
    u4  file_size;          // Total length of the .dex file
    u4  header_size;        // 0x70 (112 bytes)
    u4  endian_tag;         // 0x12345678 (little-endian)
    u4  link_size;
    u4  link_off;
    u4  map_off;            // Offset to DexMapList (section directory)
    u4  string_ids_size;    // Count of string_ids entries
    u4  string_ids_off;     // File offset to string_ids[]
    u4  type_ids_size;
    u4  type_ids_off;
    u4  proto_ids_size;
    u4  proto_ids_off;
    u4  field_ids_size;
    u4  field_ids_off;
    u4  method_ids_size;
    u4  method_ids_off;
    u4  class_defs_size;
    u4  class_defs_off;
    u4  data_size;          // Size of data section
    u4  data_off;           // File offset to data section
};
```

The header is critical because every section's location and count is encoded here. A correct parser reads the header first, then uses `*_off` and `*_size` to locate and bound every subsequent section. The `map_off` points to a `DexMapList` that provides an ordered, redundant directory of all sections — useful for validation and iteration.

### `string_ids[]`

An array of `DexStringId` structures, one per unique string literal in the file:

```c
struct DexStringId {
    u4 string_data_off;  // File offset to ULEB128-prefixed MUTF-8 data
};
```

The string data itself uses a **modified UTF-8** (MUTF-8) encoding with a ULEB128 length prefix. MUTF-8 differs from standard UTF-8 in two ways: the null character `U+0000` is encoded as the two-byte sequence `0xC0 0x80` (so that embedded nulls can exist), and supplementary characters (above `U+FFFF`) are encoded as **pairs** of three-byte sequences (a surrogate-like encoding) rather than using four-byte sequences. Strings are deduplicated across the entire `.dex` — class names, method names, field names, constant string values, and type descriptors all share the same `string_ids` pool.

### `type_ids[]`

```c
struct DexTypeId {
    u4 descriptor_idx;  // Index into string_ids[]
};
```

Each entry maps to a type descriptor string (e.g., `"Ljava/lang/String;"`, `"[I"`, `"V"`). The descriptor syntax is JVM-compatible: `L` marks the start of a class/reference type terminated by `;`, `[` prefixes an array dimension, and single characters denote primitives (`I`=int, `J`=long, `Z`=boolean, `B`=byte, `S`=short, `C`=char, `F`=float, `D`=double, `V`=void). `type_ids` are used pervasively — by `proto_ids`, `field_ids`, `method_ids`, `class_defs`, and the bytecode itself (via 8-bit or 16-bit type indices in opcodes like `check-cast`, `new-instance`, `new-array`, `filled-new-array`).

### `proto_ids[]`

```c
struct DexProtoId {
    u4 shorty_idx;      // Index into string_ids[] — return type + arg types (abbreviated)
    u4 return_type_idx; // Index into type_ids[]
    u4 parameters_off;  // File offset to DexTypeList (or 0 for no-arg methods)
};
```

The `shorty` string is a compact representation: one character for the return type followed by one character per argument type, using the single-letter primitive codes. Long and double are represented by their single-letter codes (`J`, `D`) even though they occupy two register slots — this is intentional, as `shorty` is used for stack frame sizing and JNI signature matching, not register counting. The `parameters_off` points to a `DexTypeList` containing a count and an array of `DexTypeItem` (each holding a `u2 type_idx`), listing the full types in declaration order.

### `field_ids[]`

```c
struct DexFieldId {
    u2 class_idx;       // Index into type_ids[] — defining class
    u2 type_idx;        // Index into type_ids[] — field type
    u4 name_idx;        // Index into string_ids[] — field name
};
```

Fields are identified by the triple (class, type, name). The `class_idx` typically points to the class that *declares* the field, though in the bytecode the field index is used to resolve the actual declaring class through the class hierarchy. Field IDs are shared across all classes — if `A.foo` and `B.foo` are distinct fields, they get distinct `field_ids` entries.

### `method_ids[]`

```c
struct DexMethodId {
    u2 class_idx;       // Index into type_ids[] — declaring class
    u2 proto_idx;       // Index into proto_ids[] — method signature
    u4 name_idx;        // Index into string_ids[] — method name
};

```

Methods are identified by (class, proto, name). The `class_idx` is the class that declares the method, `proto_idx` selects the full signature (return type + parameter types), and `name_idx` is the method's simple name. These indices are what the `invoke-*` opcodes encode directly (as 16-bit method indices). Method resolution at runtime uses this index to look up the `DexMethodId`, then resolves the actual target method through the class hierarchy (virtual dispatch) or directly (static/direct).

### `class_defs[]`

```c
struct DexClassDef {
    u4  class_idx;           // Index into type_ids[] — this class's type
    u4  access_flags;        // PUBLIC, FINAL, ABSTRACT, etc.
    u4  superclass_idx;      // Index into type_ids[] (NO_INDEX for Object)
    u4  interfaces_off;      // File offset to DexTypeList
    u4  source_file_idx;     // Index into string_ids[] (NO_INDEX if none)
    u4  annotations_off;     // File offset to annotations_directory_item
    u4  class_data_off;      // File offset to encoded class data
    u4  static_values_off;   // File offset to encoded_array_item
};
```

The `class_data_off` is crucial — it points to a **LEB128-encoded** stream of the class's contents: static fields, instance fields, direct methods, and virtual methods. Each group has a count followed by that many encoded items. Each field item encodes a `field_idx_diff` (differential index into `field_ids[]`) and `access_flags`. Each method item encodes a `method_idx_diff`, `access_flags`, and a `code_off` pointing to the method's `code_item` (or 0 for abstract/native methods).

The `static_values_off` points to an `encoded_array_item` containing the initial values for `static final` fields (constants). This is a size-prefixed array of `encoded_value` items.

### `code_item` Structure

Every non-abstract, non-native method has a `code_item`:

```c
struct DexCode {
    u2  registers_size;   // Total registers used by this method
    u2  ins_size;         // Number of argument registers (incoming)
    u2  outs_size;        // Number of outgoing arg registers for invoke calls
    u2  tries_size;       // Number of try_block entries
    u4  debug_info_off;   // File offset to debug info (or 0)
    u4  insns_size;       // Size of insns[] in 16-bit code units
    u2  insns[1];         // Actual bytecode, padded to 4-byte alignment
    // Followed by (if tries_size > 0):
    //   u2  padding;           (if insns_size is odd)
    //   DexTry tries[tries_size];
    //   u4  handler_off;  → points to encoded_catch_handler_list
};
```

**`registers_size`** is the total number of 32-bit virtual registers the method needs. This includes both the method's local variables and the incoming arguments. For instance methods, register `v0` holds `this`, and the remaining arguments occupy `v1..v(ins_size-1)`. For static methods, arguments start at `v0`. Local variables begin at `v(ins_size)` and extend to `v(registers_size - 1)`.

**`ins_size`** counts the number of 32-bit register slots occupied by incoming arguments. Note that `long` and `double` consume **two** slots, so a method `(long, int)` has `ins_size = 3` (2 for long + 1 for int). For non-static methods, `this` is also counted in `ins_size`.

**`outs_size`** is the maximum number of argument registers needed for *any* `invoke-*` instruction within this method. This is used by the interpreter to allocate a "outgoing argument frame" — a contiguous block of registers (or a separate buffer) where arguments for callee methods are placed before invocation. The `outs_size` is computed by the dex compiler (dx) as the maximum across all call sites in the method.

**`tries_size`** and the associated `DexTry[]` array and `encoded_catch_handler_list` encode the exception handling tables. Each `DexTry` specifies a range of bytecode addresses (`start_addr`, `insn_count`) and an offset to the handler list.

**`debug_info_off`** points to an optional debug info stream used by the debugger. It contains line number mappings, local variable name/type information, and source file references — all encoded with LEB128 varints for compactness. The debug info is not needed at runtime for execution, only for debugging.

**`insns[]`** is the actual Dalvik bytecode, stored as an array of 16-bit code units. Instructions are variable-length: most are 2 code units (32 bits), but some extend to 3 or 4 code units. The first code unit's high byte always encodes the opcode (0x00–0xFF), and the remaining bits determine the register/constant/index encoding format.

---

## 2. Dalvik VM Register Model

Dalvik uses a **register-based** virtual machine, as opposed to the JVM's stack-based model. Every method operates on a fixed-size array of 32-bit virtual registers, numbered `v0` through `v(registers_size - 1)`. The maximum possible register count is 65536 (16-bit `registers_size` field), though in practice methods rarely exceed 256 registers.

### Register Numbering

Registers are identified by 4-bit or 8-bit indices embedded directly in the instruction encoding:

- **4-bit registers** (v0–v15): Used in "compact" instruction formats (e.g., `add-int vAA, vBB, vCC` where AA, BB, CC are each 4 bits packed into a single 16-bit code unit). These enable higher code density.
- **8-bit registers** (v0–v255): Used in "wide" instruction formats where a register index occupies a full byte (e.g., `add-int vAA, vBB, vCC` in a 32-bit instruction where each field is 8 bits).
- **16-bit registers** (v0–v65535): Used in `invoke-*/range` instructions that reference a contiguous range starting at `vCCCC`.

### Method Invocation Frame Layout

When a method is called, the Dalvik interpreter allocates a frame with the following layout in contiguous memory:

```
+-----------------------------------+
|  saved frame pointer (previous)   |  <-- fp[0]
+-----------------------------------+
|  return value (saved across call) |  <-- fp[1]  (or Method* for JNI)
+-----------------------------------+
|  method pointer (Method*)         |  <-- fp[2]
+-----------------------------------+
|  v(registers_size - 1)            |  <-- fp[3]
|  v(registers_size - 2)            |  <-- fp[4]
|  ...                              |
|  v(ins_size)  [first local]       |
|  v(ins_size - 1)  [last arg]      |
|  ...                              |
|  v0  [this / first arg / local]   |  <-- fp[3 + registers_size]
+-----------------------------------+
|  outgoing args (outs_size slots)  |  <-- above v0, grows upward
|  for callee invoke-*              |
+-----------------------------------+
```

The frame pointer (`fp`) points just past the end of the register array, so registers are accessed at negative offsets from `fp`. The exact layout has the saved previous frame pointer at `fp[0]`, the saved return value at `fp[1]`, the `Method*` at `fp[2]`, and then the virtual registers from `v(registers_size-1)` at `fp[3]` down to `v0` at `fp[3 + registers_size - 1]`. The outgoing argument area for `invoke-*` instructions is placed immediately above `v0` in the frame (i.e., at positive offsets from the end of the register array).

### Argument Mapping

Arguments to a method are pre-loaded into the first `ins_size` registers by the caller:

- **Instance (non-static) methods:** `v0` = `this` reference, `v1`..`v(ins_size-1)` = explicit arguments.
- **Static methods:** `v0`..`v(ins_size-1)` = explicit arguments, no `this`.
- **Wide types (long/double):** Occupy two consecutive register slots. For example, a method `(J, I)` called on an instance maps `this→v0`, the long's low word → `v1`, high word → `v2`, the int → `v3`. The `ins_size` would be 4. Conventionally, the low word is in the lower-numbered register.

### Return Values

The return value from a method is placed in a special **result register** — not in the callee's frame but in the caller's frame. Specifically, `invoke-*` instructions store the return value into the caller's `retval` slot (at `fp[1]` in the caller's frame, or in a dedicated return value register in the assembly interpreter). The `move-result*` family of opcodes (`move-result`, `move-result-wide`, `move-result-object`) copies this return value from the result register into a specified virtual register in the caller's frame. The return value register must be consumed by exactly one `move-result*` instruction immediately following the `invoke-*`; it is undefined behavior to have an `invoke-*` without a corresponding `move-result*` for non-void methods, or to interleave other instructions between the two.

### Register Pair Convention

For 64-bit values (long, double), Dalvik uses **register pairs** — two consecutive 32-bit registers treated as one logical 64-bit slot. The pair is always `(vN, vN+1)` where `vN` is even-aligned by convention in the `code_item`'s register allocation (the dx compiler ensures this). Instructions operating on wide values use the lower register number: `move-wide vAA, vBBBB` moves a 64-bit value from `{vBBBB, vBBBB+1}` to `{vAA, vAA+1}`. This convention must be respected by any runtime implementation to ensure correct 64-bit arithmetic on 32-bit architectures.

---

## 3. Method Invocation & Call Stack

Dalvik provides a rich set of `invoke-*` opcodes that differ in dispatch semantics (virtual, direct, static, interface, super), argument encoding (individual registers vs. contiguous range), and the size of the method index field. Understanding these is critical because the interpreter's behavior, the verifier's constraints, and the JNI bridging logic all depend on which invoke variant is used.

### Invoke Opcode Families

| Opcode | Index Bits | Dispatch | Description |
|--------|-----------|----------|-------------|
| `invoke-virtual` | 16-bit | Virtual (vtable) | Normal instance method, virtual dispatch |
| `invoke-super` | 16-bit | Super (skip this class) | Call superclass's implementation |
| `invoke-direct` | 16-bit | Direct | Constructors, private methods |
| `invoke-static` | 16-bit | Static | Static methods, no `this` |
| `invoke-interface` | 16-bit | Interface | Interface method dispatch |
| `invoke-virtual/range` | 16-bit | Virtual | Same as virtual, range arg encoding |
| `invoke-super/range` | 16-bit | Super | Same as super, range arg encoding |
| `invoke-direct/range` | 16-bit | Direct | Same as direct, range arg encoding |
| `invoke-static/range` | 16-bit | Static | Same as static, range arg encoding |
| `invoke-interface/range` | 16-bit | Interface | Same as interface, range arg encoding |

### Non-Range vs. Range Encoding

**Non-range** instructions encode up to 5 argument registers individually in the instruction's bit fields:

```
invoke-virtual {vC, vD, vE, vF, vG}, meth@BBBB
  A=opcode, B=method_idx (16-bit), G=arg5, C=arg1, D=arg2, E=arg3, F=arg4
```

**Range** instructions encode a starting register and count, allowing arbitrarily many arguments:

```
invoke-virtual/range {vCCCC .. vNNNN}, meth@BBBB
  A=opcode, B=method_idx, C=first register, AA=argument count
```

The `dx` compiler emits range variants when a method has more than 5 arguments. The range encoding uses 5 registers by default; for methods with ≤5 args, non-range is preferred for code density.

### Dispatch Semantics in Detail

**`invoke-virtual`**: Resolves the target method by performing virtual dispatch. The runtime looks up the method reference in `method_ids[]`, extracts the class and method name, then searches the object's actual class (from `this`, in `v0`) for a matching method — first in the object's class, then up the superclass chain, using the **vtable** for O(1) lookup when available (most non-abstract methods have vtable entries). The vtable is a flat array of `Method*` pointers stored in the `ClassObject` structure, indexed by the method's vtable index (assigned during class linking).

**`invoke-direct`**: Bypasses virtual dispatch entirely. The target method must be an instance method that is either `<init>` (constructor), `private`, or in the same class (package-private). Resolution is direct: look up the method in the specified class (from `method_ids[].class_idx`) without consulting the vtable or the receiver's actual runtime type. The verifier enforces that `invoke-direct` is only used with methods having the `ACC_DIRECT` flag.

**`invoke-static`**: Similar to `invoke-direct` but for static methods. No `this` argument is passed (arguments start at `v0`). Resolution looks in the class specified in the method reference, then up the superclass chain if not found directly.

**`invoke-super`**: Resolves against the *superclass* of the current method's class, not the receiver's actual class. This is essential for correctly calling overridden parent methods from within a child. The runtime uses the calling method's `ClassObject` to find its superclass, then resolves the method from there. This ensures that even if a subclass `C` overrides `m()`, a call to `super.m()` from `C.m()` dispatches to `B.m()` (where `B` is `C`'s superclass), not back to `C.m()`.

**`invoke-interface`**: The most complex dispatch path. Interface method calls cannot use a simple vtable because a class may implement multiple interfaces and the interface method's vtable slot depends on the full class hierarchy. Dalvik maintains an **iftable** (interface table) in `ClassObject` — an array of `IfTable` entries, each containing an interface class pointer and a vtable-like method array for that interface. The runtime first resolves the interface method reference to get the interface class and method name, then does a linear scan of the object's iftable to find the matching interface, then indexes into that interface's method array. In practice, this is O(n) in the number of interfaces implemented, which is usually small.

### Call Frame Creation

When the interpreter processes an `invoke-*` instruction:

1. The caller's outgoing argument area (or the range of registers `vC..vC+AA-1`) contains the arguments already placed by prior `move` instructions or `filled-new-array`.
2. The interpreter allocates a new frame: `sizeof(StackSaveArea) + callee_registers_size * 4 + callee_outs_size * 4`.
3. Arguments are copied from the caller's outgoing area into the callee's registers `v0..v(ins_size-1)`.
4. A `StackSaveArea` (prev frame, retval, Method*) is initialized.
5. The interpreter's program counter, frame pointer, and `self` (thread pointer) are saved and the new frame is activated.
6. Execution continues at the callee's first instruction.

For the assembly interpreter (mterp), much of this frame setup is inlined with hand-written assembly for maximum speed. The portable C interpreter uses the `dvmInterpret()` function with a while-loop and explicit frame management.

---

## 4. Object Representation

Every Java object in Dalvik is represented at runtime by an `Object` structure. The layout is designed to be compact, GC-friendly, and compatible with JNI's expectation that objects are pointer-sized.

### Object Header

```c
struct Object {
    ClassObject* clazz;    // 4 bytes (32-bit) — pointer to class object
    u4             lock;    // 4 bytes — lock word (thin lock / fat lock)
    // Instance fields follow immediately after
};
```

The header is exactly **8 bytes** on 32-bit Dalvik. This minimal two-word header is smaller than HotSpot's (which includes a mark word + class pointer + array length, often 12–16 bytes). The trade-off is that Dalvik's lock word must encode both the lock state *and* hash code in a single 32-bit word.

**`clazz`** is a pointer to the object's `ClassObject`, which is itself a special kind of object. The `ClassObject` contains the vtable, iftable, static field storage, reference instance field offsets (for GC card table scanning), and all class metadata.

**`lock`** is a multi-purpose 32-bit word:

```
Thin Lock Layout (32 bits):
+--------+------+------------------+
| hash   | count| owner (thread id)|
| 25 bits| 7 bit|  (variable)      |
+--------+------+------------------+

Fat Lock: lock word contains a pointer to a Monitor object on the heap
```

- **Unlocked state**: `lock == 0`
- **Thin-locked state**: The lock word encodes the owning thread ID (low bits) and a recursion count. No OS-level mutex is needed. Most locks in Java are uncontended and thin locks are extremely fast.
- **Fat-locked state**: If contention is detected (a second thread tries to acquire a thin-locked object), the runtime "inflates" the lock — allocates a `Monitor` structure (containing an OS mutex, condition variable, wait set, and owner thread pointer) and stores a pointer to it in the lock word. This is indicated by the low bit being set (since thread IDs are shifted).

The hash code for an object is lazily computed and stored in the upper bits of the lock word (or moved to the `Monitor` upon inflation). `System.identityHashCode()` reads from here.

### Instance Fields

Instance fields are stored **inline** immediately after the 8-byte object header, in the order they are declared in the class hierarchy (superclass fields first, then subclass fields). Reference fields and primitive fields are interleaved in declaration order, not segregated. However, the GC needs to know which offsets within an object are references — this information is stored in the `ClassObject` as an array of reference field offsets (`refOffsets`), packed as a bitmask or a list.

For a 64-bit field (long/double), the field occupies 8 bytes and is **naturally aligned** to an 8-byte boundary. This may introduce padding between fields. The `dx` compiler and the Dalvik linker both respect this alignment requirement.

```c
// Example: class A { int x; long y; Object z; }
// Memory layout (32-bit):
// offset 0:  ClassObject* clazz
// offset 4:  u4 lock
// offset 8:  int x          (4 bytes)
// offset 12: <padding>      (4 bytes, for 8-byte alignment of y)
// offset 16: long y         (8 bytes)
// offset 24: Object* z      (4 bytes)
// offset 28: <padding>      (4 bytes, for 8-byte object alignment)
// Total: 32 bytes
```

### Static Fields

Static fields are **not** stored inline in the object (there is no "object" for static fields). Instead, they are stored in a contiguous array referenced by the `ClassObject`:

```c
struct ClassObject {
    // ... class metadata ...
    u4  sfieldCount;
    InstField* sfields;       // Array of static field descriptors
    // ... 
    // Static field values stored in a separate allocation:
    // void* staticFieldStorage; // points to the actual values
};
```

The static field values are allocated as a single block of memory when the class is initialized. `sget`/`sput` instructions access this storage via offsets computed during class linking.

### Arrays

Arrays are objects with a special header that includes the array length:

```c
struct ArrayObject {
    Object    obj;         // Standard 8-byte object header (clazz + lock)
    u4        length;      // Number of elements
    // Elements follow immediately after
};
```

**Reference arrays** (`Object[]`) store pointers to `Object` in the element area. The GC treats each element as a potential root — during mark phase, every non-null element is traced.

**Primitive arrays** (`int[]`, `byte[]`, etc.) store raw values. The GC does **not** scan their contents (the elements are not pointers). The `clazz` pointer in the header distinguishes the element type — `int[]` has `clazz = [I`, `byte[]` has `clazz = [B`, etc.

Multi-dimensional arrays are arrays-of-arrays: an `int[][]` is an `ArrayObject` of element type `[I`, where each element is itself an `ArrayObject*`.

---

## 5. Field Access

Field access in Dalvik bytecode is performed by the `iget`/`iput` (instance) and `sget`/`sput` (static) instruction families. Each family has variants for different field widths and for object references.

### Instance Field Access: `iget`/`iput`

| Opcode | Width | Operation |
|--------|-------|-----------|
| `iget` | 32-bit | `vA <- obj.field` (int, float, boolean, byte, char, short) |
| `iget-wide` | 64-bit | `vA:vA+1 <- obj.field` (long, double) |
| `iget-object` | 32-bit (ref) | `vA <- obj.field` (Object reference) |
| `iget-boolean` | 32-bit | `vA <- obj.field` (boolean, zero-extended) |
| `iget-byte` | 32-bit | `vA <- obj.field` (byte, sign-extended) |
| `iget-char` | 32-bit | `vA <- obj.field` (char, zero-extended) |
| `iget-short` | 32-bit | `vA <- obj.field` (short, sign-extended) |
| `iput` | 32-bit | `obj.field <- vA` |
| `iput-wide` | 64-bit | `obj.field <- vA:vA+1` |
| `iput-object` | 32-bit (ref) | `obj.field <- vA` (write barrier for GC) |
| `iput-boolean` | 32-bit | `obj.field <- vA` |
| `iput-byte` | 32-bit | `obj.field <- vA` |
| `iput-char` | 32-bit | `obj.field <- vA` |
| `iput-short` | 32-bit | `obj.field <- vA` |

The instruction format encodes: destination/source register `vAA`, object register `vBB`, and a 16-bit field index `CCCC` into `field_ids[]`.

The typed variants (`iget-boolean`, `iget-byte`, etc.) exist primarily for the verifier — they make the field type explicit in the bytecode so that the verifier can check type correctness without resolving the field. At runtime, all 32-bit `iget` variants perform the same operation (load a 32-bit word from the object); the sign/zero extension is handled by the instruction handler. The `iget-object` variant additionally triggers a read barrier in GC implementations that use a brooks pointer or forwarding pointer (though stock Dalvik mark-sweep does not require read barriers, this is important for future compatibility).

### Static Field Access: `sget`/`sput`

The same variants exist for static fields: `sget`, `sget-wide`, `sget-object`, `sget-boolean`, `sget-byte`, `sget-char`, `sget-short`, and their `sput` counterparts. The key difference is that there is no object register — the field is located via the class's static field storage.

### Field Resolution

Field resolution follows a well-defined chain:

1. **Index resolution:** The 16-bit field index in the instruction is used to look up `DexFieldId` in the DEX file's `field_ids[]` table, yielding (class_idx, type_idx, name_idx).

2. **Class resolution:** The `class_idx` is resolved to a `ClassObject*`. This may trigger class loading if the class hasn't been loaded yet.

3. **Field lookup:** The runtime searches for a field with the matching name and type in the resolved class:
   - For **instance fields** (`iget`/`iput`): The field is searched in the *object's actual class* first, then up the superclass chain to `java.lang.Object`. If the field is not found in the object's class hierarchy, a `NoSuchFieldError` is thrown. The `DexFieldId.class_idx` is the **referencing** class (used by the verifier), not necessarily the declaring class.
   - For **static fields** (`sget`/`sput`): The field is searched in the class specified in `field_ids[].class_idx` and then up its superclass chain. If not found, `NoSuchFieldError`.

4. **Access check:** The resolved field's `access_flags` are checked against the caller's class. Private fields are only accessible from the declaring class. Package-private fields require same package. Protected fields allow subclass access. If access is denied, `IllegalAccessError`.

5. **Offset caching:** Once a field is resolved, the byte offset from the object base (for instance fields) or from the static field storage base (for static fields) is cached in the `Field` structure's `byteOffset` field. Subsequent accesses to the same field index skip the full resolution and directly compute the address as `obj + offset`. This is critical for performance — field access is one of the hottest operations in typical Java code.

### 16-bit Field Indices

All field access instructions use a **16-bit** field index (`CCCC` in the instruction encoding), allowing up to 65535 unique field references per DEX file. This is sufficient for all but the most enormous applications. If a multi-dex application exceeds this limit, the Android build tools split the DEX into multiple files, each with its own field index space.

---

## 6. Type System & Class Hierarchy

### Primitive Types

Dalvik supports the same set of primitive types as the JVM, with specific encoding conventions in type descriptors and bytecode:

| Type | Descriptor | Size (registers) | Size (bytes) | Notes |
|------|-----------|-------------------|-------------|-------|
| `boolean` | `Z` | 1 | 1 | Stored as int (0/1) |
| `byte` | `B` | 1 | 1 | Sign-extended to int on load |
| `char` | `C` | 1 | 2 | Zero-extended to int on load |
| `short` | `S` | 1 | 2 | Sign-extended to int on load |
| `int` | `I` | 1 | 4 | Default integer type |
| `long` | `J` | **2** | 8 | Register pair (vN, vN+1) |
| `float` | `F` | 1 | 4 | IEEE 754 single |
| `double` | `D` | **2** | 8 | IEEE 754 double, register pair |
| `void` | `V` | 0 | 0 | Return type only |

Key detail for runtime implementers: **boolean, byte, char, and short are all stored and manipulated as 32-bit ints in registers**. The distinction only matters at store time (`iput-byte` truncates to 8 bits) and load time (`iget-byte` sign-extends). This simplifies the interpreter — most arithmetic instructions operate on 32-bit values regardless of the source type.

### Reference Types

- **Class types:** `Ljava/lang/String;` — any non-array, non-primitive type.
- **Array types:** `[I` (int[]), `[Ljava/lang/Object;` (Object[]), `[[I` (int[][]). Array dimension depth is unbounded.
- **Null:** A special reference value representing "no object." The `const/4 v0, 0` instruction can load a null reference into any register typed as an object reference.

### Class Loading Order

Class loading in Dalvik follows a lazy, on-demand model:

1. **Bootstrap classes** (java.lang.*, java.util.*, etc.) are loaded first from `bootclasspath` (typically `/system/framework/core.jar` or the preloaded classes cache in Zygote).
2. **Application classes** are loaded from the APK's `classes.dex` (or multidex files) via a `PathClassLoader` or `DexClassLoader`.
3. When a class is first referenced (by a `new-instance`, `invoke-*`, `iget`/`sget`, `check-cast`, or `const-class` instruction), the VM triggers class loading.
4. The class loader delegation model follows the JVM: first ask the parent class loader, then attempt local loading.
5. Dalvik's `DexFile` parser reads the `class_def` and `class_data` structures to construct an internal `ClassObject` representation.

### Class Verification

Dalvik's verifier (`dalvik/vm/Verify.cpp`) performs **pre-verification** at DEX load time (not at class load time like the JVM). This is a key design choice for performance on mobile devices:

- **Pre-verification** scans all bytecode and computes register type maps at every instruction. It proves type safety: no use of an int as an object reference, no branching from a try block into a catch block that expects a different exception type on the stack, etc.
- The result of pre-verification is a set of **register map** data structures that are stored alongside the DEX. At runtime, the interpreter can skip most type checks because the verifier has already proven correctness.
- A "quickened" bytecode optimization replaces some opcodes with variants that assume verification has passed (e.g., replacing `invoke-virtual` with `invoke-virtual-quick` that uses a pre-resolved vtable offset).
- Classes that fail verification cannot be instantiated or have their methods called — they are marked as `CLASS_ERROR` and subsequent attempts to use them throw `VerifyError`.

### Linker Resolution

After a class is loaded and verified, the linker performs resolution:

1. **Superclass resolution:** The superclass reference is resolved to a `ClassObject*`. If it hasn't been loaded yet, it is loaded recursively.
2. **Interface resolution:** All implemented interfaces are resolved.
3. **Field resolution:** All fields declared in this class are assigned their offsets within the instance layout (for instance fields) or static storage (for static fields). The offsets are computed by concatenating the superclass's field layout and adding this class's fields, respecting alignment.
4. **Method resolution:** Virtual methods are assigned vtable indices. The vtable is built by copying the superclass's vtable, then replacing entries for overridden methods and appending new entries for new virtual methods. Direct methods and static methods are placed in a method list.
5. **Interface table (iftable) construction:** For each interface implemented (including inherited), an iftable entry is created mapping interface methods to the class's concrete method implementations.
6. **Static initializer (`<clinit>`) scheduling:** If the class has a `<clinit>` method and hasn't been initialized yet, it is marked for lazy initialization (triggered on first active use).

---

## 7. Exception Handling

Dalvik's exception model closely follows the JVM specification with some implementation-specific details relevant to the DEX bytecode format.

### Throwing Exceptions

Exceptions are thrown using the `throw` instruction:

```
throw vAA    // Throw the Throwable reference in register vAA
```

The runtime semantics are:

1. Pop the current method's frame off the call stack.
2. Search the current method's `tries[]` table for a handler whose address range covers the program counter at the time of the throw.
3. If found, clear the operand stack (in Dalvik's register model, this means the registers' contents become undefined — only the exception object is valid), push the exception object, and transfer control to the handler's bytecode offset.
4. If not found in the current method, unwind to the caller and repeat from step 2.
5. If the exception reaches the top of the stack without being caught, the thread's `UncaughtExceptionHandler` is invoked (which typically terminates the thread with a stack trace).

### Try Blocks and Handler Tables

Exception information is encoded in the `code_item` after the `insns[]` array:

```c
struct DexTry {
    u4 start_addr;     // Bytecode offset (in 16-bit code units) of try block start
    u2 insn_count;     // Number of code units in the try block
    u2 handler_off;    // Offset to encoded_catch_handler_list (relative to
                       // the start of this DexTry, NOT from the start of code_item)
};
```

The `handler_off` is **relative to the address of this `DexTry` struct itself**, which is a somewhat unusual design — to compute the absolute file offset of the handler list, you take the file offset of the `DexTry` entry and add `handler_off`.

The handler list is LEB128-encoded:

```c
struct encoded_catch_handler_list {
    uleb128 size;              // Number of handlers
    encoded_catch_handler handlers[size];
};

struct encoded_catch_handler {
    sleb128  handler_addr;     // Bytecode offset of handler (0 = catch-all)
    uleb128  type_idx;         // Index into type_ids[] (NO_INDEX = catch-all)
    // If type_idx != NO_INDEX, followed by another encoded_catch_handler
    // (handlers are stored as a linked list within the struct)
};
```

Each handler specifies a type (which exception class it catches) and a handler address. A special `type_idx == NO_INDEX` (value 0xFFFFFFFF encoded as -1 in sleb128) represents a **catch-all** (`finally` block). Handlers are checked in order, and the first matching handler is used. Type matching follows the JVM rule: a handler catches exceptions of the specified type **and all its subclasses**. The check is done by walking the exception object's class hierarchy upward.

### `move-exception`

When execution transfers to a catch handler, the exception object is not placed in a regular register. Instead, the first instruction of every catch handler **must** be `move-exception`:

```
move-exception vAA    // vAA <- exception object
```

This is the only way to access the caught exception. The `move-exception` instruction reads from a special internal location (the thread's `exception` field in the `Thread` struct, or the interpreter's pending exception slot) and moves it into the specified register. After `move-exception`, the thread's pending exception is cleared. If a catch handler does not begin with `move-exception`, the behavior is undefined (the verifier rejects it).

### Exception Handling Performance Considerations

- The tries table is typically very small (0–3 entries per method), so linear scanning is fast.
- Unwinding through the call stack is O(depth), which is normally shallow for common exceptions.
- The pending exception is stored in the `Thread` structure (`self->exception`), not on the operand stack, which simplifies unwinding.
- The interpreter's main loop checks for pending exceptions at every `invoke-*` return and at backward branches (to catch asynchronous exceptions, though these are rare in practice).

---

## 8. Garbage Collection

Dalvik's garbage collector evolved significantly across Android versions, but the core architecture remained a **non-generational, non-moving, mark-sweep** collector throughout the Dalvik era.

### Mark-Sweep Collector (Android 1.0–2.2)

The earliest Dalvik GC was a simple stop-the-world mark-sweep:

1. **Mark phase:** Traverse all reachable objects starting from GC roots, setting a "marked" bit in each object's header (or in a separate bitmap). Reachability is defined as: an object is reachable if it can be reached by following object references from any root.
2. **Sweep phase:** Scan the entire heap linearly. For each allocated object, if it is not marked, free it (add to the free list). If it is marked, clear the mark bit for the next cycle.

This was simple but had long pause times proportional to heap size.

### Card Table

To support efficient reference tracking, Dalvik uses a **card table** — a byte array with one byte per 512 bytes (configurable) of the heap. Each byte is a "card." When a reference field in an object is written (via `iput-object`), the runtime generates a **write barrier** that marks the card containing the modified object as "dirty."

During GC, instead of scanning every object in the heap to find references to the young generation (if there were one) or to handle inter-region references, the GC can scan only the dirty cards. In Dalvik's non-generational design, the card table is primarily used for **concurrent mark** — the GC can concurrently scan objects while the mutator runs, and the card table tells the GC which objects' reference fields may have changed since the last scan.

```c
// Write barrier (simplified):
#define CARD_SHIFT 9          // 512-byte cards
#define CARD_SIZE (1 << CARD_SHIFT)

static inline void dvmWriteBarrier(Object* obj) {
    u1* card = gDvm.cardTableBase + ((uintptr_t)obj >> CARD_SHIFT);
    *card = GC_CARD_DIRTY;
}
```

### GC Roots

GC roots are the starting points for mark-phase traversal:

| Root Type | Description |
|-----------|-------------|
| **Stack roots** | Every reference in every thread's stack frame (registers + outgoing args) |
| **JNI local/global refs** | References held by native code via JNI |
| **Static fields** | Every reference-type static field in every loaded class |
| **Intern table** | `String.intern()` entries |
| **Monitor objects** | Objects used as lock monitors |
| **VM internal** | Thread objects, class loader objects, exception objects in-flight |

For stack roots, the interpreter must cooperate with the GC. When a GC is triggered, the runtime walks each thread's stack, interprets the register maps (produced by the verifier) to determine which registers in each frame hold reference values, and reports those as roots. This is why the verifier's register type information is critical — without it, the GC would have to conservatively treat every register as a potential reference.

### Concurrent vs. Stop-the-World

In later Dalvik versions (Android 2.3+, with the "CMS" — Concurrent Mark Sweep collector):

- **Initial mark:** Brief stop-the-world pause to mark direct roots.
- **Concurrent mark:** The GC thread traces the object graph concurrently with the mutator. The card table tracks mutations so the GC can re-scan dirty cards.
- **Remark:** Short stop-the-world pause to finalize marking (re-scan dirty cards and roots that changed during concurrent mark).
- **Concurrent sweep:** The GC thread frees unmarked objects while the mutator continues. Freed memory is returned to the allocation pool.

This design dramatically reduces pause times but introduces complexity: the mutator must execute write barriers, the GC thread must synchronize with class loading (newly loaded classes may add static field roots), and object allocation must be thread-safe.

### Allocation

Objects are allocated from a **free list** (not a bump-pointer arena, since Dalvik doesn't move objects). The allocator maintains bins by size for fast allocation of common sizes. For very small allocations or when the free list is empty, the allocator falls back to the system `malloc`. When the heap grows beyond the target size, a GC cycle is triggered. If the GC doesn't free enough memory, the heap is grown (up to the maximum heap size, which is device-dependent, typically 16–64 MB in the Dalvik era).

---

## 9. Interpreter Loop Architecture

The Dalvik interpreter is the heart of the VM — it fetches, decodes, and executes every Dalvik bytecode instruction. Dalvik provides **two** interpreter implementations: a **portable C interpreter** and a set of **assembly (mterp) interpreters** optimized for specific architectures.

### Instruction Fetch-Decode-Execute

Each Dalvik instruction occupies 2, 3, or 4 consecutive 16-bit code units. The first code unit always contains the opcode in its high byte (bits 15–8). The interpreter's core loop:

```c
// Simplified portable interpreter loop
void dvmInterpret(Thread* self) {
    DvmDex* methodDex = self->interpSave.method->clazz->pDvmDex;
    const u2* pc = self->interpSave.pc;
    u4* fp = self->interpSave.fp;

    #define FETCH()       (opcode = *pc++)
    #define INST_A(_inst) (u2)(((_inst) >> 8) & 0xFF)
    #define INST_B(_inst) (u2)(((_inst) >> 0) & 0xFF)

    while (true) {
        u2 inst = *pc++;
        u1 opcode = inst >> 8;

        switch (opcode) {
            case OP_NOP:        break;
            case OP_MOVE:       fp[INST_A(inst)] = fp[INST_B(inst)]; break;
            case OP_RETURN_VOID: goto return_from_method;
            case OP_INVOKE_VIRTUAL: {
                // decode B (method_idx), {C,D,E,F,G} (regs)
                // resolve method, setup frame, continue in callee
                break;
            }
            // ... ~250 more opcodes ...
        }
    }
}
```

### Handler Table vs. Switch vs. Computed Goto

The portable interpreter uses a C `switch` statement. The compiler may optimize this into:

- A **jump table** (if the opcode range is dense and the compiler chooses to): O(1) dispatch.
- A **binary search tree** (if the compiler decides the switch is too sparse): O(log n) dispatch.

For better performance, GCC supports **computed goto** (`goto *table[opcode]`), which provides guaranteed O(1) dispatch with lower overhead than a switch:

```c
static void* handler_table[256] = {
    [OP_NOP]            &&handle_nop,
    [OP_MOVE]           &&handle_move,
    [OP_INVOKE_VIRTUAL] &&handle_invoke_virtual,
    // ...
};

// In the loop:
goto *handler_table[opcode];
```

The computed goto approach avoids the switch's overhead of bounds checking and table index arithmetic, and allows each handler to fall through or jump to the next instruction fetch directly. Some Dalvik builds use this optimization when compiled with GCC.

### Portable Interpreter (`dalvik/vm/interp/Interp.c`)

The portable interpreter is a single C file with a large `switch` statement. It is correct, maintainable, and architecture-independent, but not the fastest. It is used:
- As a fallback when no assembly interpreter is available for the target architecture.
- During debugging (GDB can step through C code).
- As the reference implementation for verifying correctness of the assembly interpreters.

### Assembly Interpreter — mterp (`dalvik/vm/mterp/`)

The mterp ("method interpreter") is the performance-critical interpreter, implemented in assembly for each supported architecture (ARM, x86, MIPS, ARMv7 with VFP). Key characteristics:

- **Template-based code generation:** The interpreter is written in a hybrid of assembly and C-like macros. A Python script (`gen-mterp.py`) processes architecture-specific templates and generates the final `.S` assembly file.
- **Frequently-used opcodes are hand-optimized in assembly:** `iget`/`iput`, `invoke-virtual`, `move`, `return`, `if-test`, arithmetic ops — these are the hottest instructions in real workloads.
- **Rare opcodes fall back to C:** Opcodes like `monitor-enter`, `throw`, and packed-switch are dispatched to the portable C interpreter via a function call.
- **Inline frame management:** `invoke-*` and `return-*` handlers manipulate the frame pointer, program counter, and saved registers directly in assembly, avoiding function call overhead.
- **Register allocation:** The assembly interpreter keeps frequently-accessed VM state in physical registers (e.g., `rFP` = frame pointer, `rPC` = program counter, `rSELF` = thread pointer, `rINST` = current instruction word).

```arm
// Simplified ARM mterp handler for OP_MOVE
    .global dvmMterpOpMove
dvmMterpOpMove:
    FETCH_INST_FROM_REGISTER  // rINST = *rPC++
    mov     r0, rINST, lsr #8  @ r0 = A (dest register)
    and     r1, rINST, #0xFF  @ r1 = B (src register)
    ldr     r2, [rFP, r1, lsl #2]  @ r2 = fp[B]
    str     r2, [rFP, r0, lsl #2]  @ fp[A] = r2
    FETCH_ADVANCE_INST 1      @ advance PC by 1 code unit
    GOTO_NEXT_INSTRUCTION     @ jump to next handler
```

The mterp interpreter achieves roughly 2–5× better performance than the portable C interpreter on ARM, depending on the workload. This is critical for devices with limited CPU resources.

---

## 10. JNI & Native Interface

The Java Native Interface (JNI) allows Java code to call native (C/C++) functions and vice versa. Dalvik's JNI implementation is in `dalvik/vm/Jni.cpp` and must conform to the JNI specification while integrating with Dalvik's internal data structures.

### JNIEnv Function Table

Every native method receives a `JNIEnv*` as its first argument. The `JNIEnv` is a pointer to a function table (an array of function pointers) containing all JNI operations:

```c
struct JNINativeInterface_ {
    void*       reserved0;
    void*       reserved1;
    void*       reserved2;
    void*       reserved3;
    jint        (*GetVersion)(JNIEnv *);
    jclass      (*DefineClass)(JNIEnv*, const char*, jobject, const jbyte*, jsize);
    jclass      (*FindClass)(JNIEnv*, const char*);
    jmethodID   (*FromReflectedMethod)(JNIEnv*, jobject);
    jfieldID    (*FromReflectedField)(JNIEnv*, jobject);
    jobject     (*NewGlobalRef)(JNIEnv*, jobject);
    void        (*DeleteGlobalRef)(JNIEnv*, jobject);
    void        (*DeleteLocalRef)(JNIEnv*, jobject);
    jboolean    (*IsSameObject)(JNIEnv*, jobject, jobject);
    jobject     (*NewLocalRef)(JNIEnv*, jobject);
    jint        (*EnsureLocalCapacity)(JNIEnv*, jint);
    jobject     (*AllocObject)(JNIEnv*, jclass);
    jobject     (*NewObject)(JNIEnv*, jclass, jmethodID, ...);
    jfieldID    (*GetFieldID)(JNIEnv*, jclass, const char*, const char*);
    jobject     (*GetObjectField)(JNIEnv*, jobject, jfieldID);
    jint        (*GetIntField)(JNIEnv*, jobject, jfieldID);
    void        (*SetIntField)(JNIEnv*, jobject, jfieldID, jint);
    // ... ~230 total function pointers ...
    jint        (*RegisterNatives)(JNIEnv*, jclass, const JNINativeMethod*, jint);
    jint        (*UnregisterNatives)(JNIEnv*, jclass);
    // ...
};
```

The `JNIEnv` is thread-local: each thread has its own `JNIEnv` (actually, each thread has a `JavaVM*` and derives `JNIEnv*` from it). In Dalvik, the `JNIEnv` is stored in the thread's `Thread` structure (`self->jniEnv`).

### RegisterNatives

Native methods can be registered in two ways:

1. **Dynamic registration** via `JNI_OnLoad()` + `RegisterNatives()`: When a native library is loaded (`System.loadLibrary()`), the VM calls `JNI_OnLoad()`. The library typically registers all its native methods:

```c
static JNINativeMethod methods[] = {
    {"nativeMethod", "(ILjava/lang/String;)V", (void*)myNativeMethod},
};

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    jclass cls = (*env)->FindClass(env, "com/example/MyClass");
    (*env)->RegisterNatives(env, cls, methods, 1);
    return JNI_VERSION_1_6;
}
```

2. **Static binding** via name convention: If no dynamic registration is done, the VM looks for a function named `Java_com_example_MyClass_nativeMethod` in the loaded library using `dlsym()`. This is slower and not recommended for production.

When `RegisterNatives()` is called, Dalvik stores the function pointer in the `Method` structure's `nativeFunc` field. Subsequent `invoke-native` dispatches jump directly to this function pointer without any name lookup.

### Transition Between Managed and Native Code

Transitioning between Dalvik managed code and native code is one of the most expensive operations in the VM, because the VM state must be carefully managed:

```c
// Simplified dvmCallJNIMethod (from vm/Jni.cpp)
void dvmCallJNIMethod(const u4* args, JValue* pResult,
                      const Method* method, Thread* self)
{
    // 1. Transition thread state from RUNNING to NATIVE
    //    This tells the GC that this thread's stack may not have
    //    valid register maps (native frames aren't scanned the same way)
    dvmChangeStatus(self, THREAD_NATIVE);

    // 2. Push a JNI transition frame
    //    This frame has a reference to the Method* so the GC can
    //    find the calling method's class (for static field roots)

    // 3. Convert Dalvik calling convention to C calling convention
    //    Dalvik args are in an array: args[0]=this, args[1..N]=params
    //    These must be placed in the C function's argument registers/stack

    // 4. Call the native function
    //    method->nativeFunc(env, clazz_or_this, args...)

    // 5. Convert return value from C to JValue

    // 6. Transition thread state back to RUNNING
    dvmChangeStatus(self, THREAD_RUNNING);

    // 7. Check for pending exceptions (native code may have called
    //    ThrowNew or similar)
    if (dvmCheckException(self)) {
        // Exception will be thrown when execution returns to
        // // the interpreter
    }
}
```

### JNI Reference Management

JNI uses explicit reference types to prevent GC from collecting objects that are only referenced by native code:

- **Local references:** Valid for the duration of the native method call. Automatically freed when the method returns. Stored in a per-thread local reference table. If many local references are created in a loop, `DeleteLocalRef()` must be called explicitly to avoid overflowing the table (default capacity is 512).
- **Global references:** Valid until explicitly deleted with `DeleteGlobalRef()`. These are GC roots.
- **Weak global references:** Do not prevent GC but can be upgraded to a strong reference if the object hasn't been collected. Useful for caches.

In Dalvik, local references are implemented as a stack of `Object*` pointers in the `Thread` structure's `jniLocalRefTable`. Global references are stored in a VM-global hash table protected by a mutex.

---

## 11. Optimization: JIT Compilation

Starting with Android 2.2 (Froyo), Dalvik includes a **trace-based JIT compiler** that compiles frequently-executed bytecode sequences into native machine code. This is distinct from ART's ahead-of-time (AOT) approach — Dalvik's JIT compiles at runtime, while the application is running.

### Trace JIT Architecture

Dalvik's JIT is **trace-based**, not method-based. A "trace" is a linear sequence of bytecode instructions that includes one or more hot loop back-edges. The key insight is that most execution time is spent in loops, and compiling just the loop body (and the code leading into it) gives most of the benefit of full method compilation at a fraction of the cost.

**Hot trace detection:** The interpreter profiles execution by counting back-edge jumps. When a loop's back-edge count exceeds a threshold (configurable, typically ~10,000 iterations per JIT compilation unit, though the exact value depends on the device profile and runtime settings), the JIT compiler is invoked. The interpreter maintains a small profiling buffer per method (or per loop header) to track hotness.

### Code Cache

Compiled native code is stored in a **code cache** — a contiguous region of memory allocated with `mmap()` and `PROT_EXEC | PROT_WRITE` permissions (later changed to `PROT_EXEC | PROT_READ` after patching). The code cache has a fixed maximum size (typically 1–8 MB, device-dependent). When the code cache fills up, the oldest or least-frequently-used traces are evicted.

The code cache stores not just the native instructions but also **metadata**: a mapping from (method, bytecode PC) to (native code address), which the interpreter uses to check if compiled code exists before interpreting. When the interpreter encounters a hot loop, it looks up the code cache; if a compiled trace exists, it jumps directly to the native code instead of continuing to interpret.

```c
struct JitEntry {
    const Method* method;      // Which method
    const u2*     dPC;         // Dalvik PC (bytecode offset)
    void*         codeAddr;    // Native code entry point
    u4            instructionCount; // Number of Dalvik instructions compiled
};
```

### Compilation Pipeline

When a hot trace is detected, the JIT compiler:

1. **Trace formation:** Starting from the loop back-edge, the compiler walks backward and forward through the bytecode to form a linear trace. The trace includes the loop body plus the entry path from the method start (or from a branch target). Trace formation terminates at method calls (trace doesn't inline), throws, returns, or unconditional branches to code outside the trace.

2. **Type specialization:** The JIT uses runtime type information to generate more efficient code. For example, if a virtual call site always resolves to the same target class, the JIT can emit a direct call with a guard (class check + fallback to interpreter). This is called **class hierarchy analysis** (CHA) or **profile-guided inlining**.

3. **Register allocation:** Dalvik's virtual registers are mapped to native CPU registers or stack slots. The JIT performs linear-scan register allocation (faster than graph coloring, suitable for JIT compilation time constraints).

4. **Code generation:** The JIT emits native instructions for the target architecture. It supports ARM (ARMv5TE and ARMv7-A with Thumb-2), x86, and MIPS. The code generator handles all Dalvik opcodes, including complex ones like `packed-switch`, `sparse-switch`, `filled-new-array`, and all `invoke-*` variants.

5. **Patching:** The interpreter's dispatch loop is patched to jump to the compiled code at the appropriate bytecode PC. This is done by modifying the code cache lookup or by patching the interpreter's entry point.

### On-Stack Replacement (OSR)

On-Stack Replacement allows the VM to switch from interpreted execution to JIT-compiled execution (or vice versa) while a method is active on the stack — without waiting for the method to return and be re-entered. This is critical for long-running loops that become hot after the method has already been called.

In Dalvik's JIT, OSR works as follows:

1. A loop back-edge is detected as hot during interpretation.
2. The JIT compiles the trace starting at the loop header.
3. The VM modifies the current interpreter frame: it saves all virtual register values from the interpreter's register array, transitions to the compiled code entry point, and the compiled code reads the register values from the saved frame state.
4. When the compiled trace exits (e.g., the loop condition becomes false), execution returns to the interpreter at the bytecode PC following the loop.

OSR is one of the trickiest parts of a JIT implementation because the compiled code and the interpreter must agree on the exact layout and meaning of every register, and the transition must be atomic with respect to GC (no objects should be lost between the two execution modes).

### JIT Limitations

- **No inlining:** Dalvik's JIT does not inline callee methods into the trace. Each `invoke-*` still results in a call (possibly to another JIT-compiled trace, but not inlined).
- **No escape analysis:** Objects are always allocated on the heap, even if they don't escape the method.
- **Trace length limits:** Traces are limited to a few hundred instructions to keep compilation time low.
- **No deoptimization:** Once compiled code is entered, it cannot fall back to the interpreter except at trace boundaries. This means speculative optimizations must use guards that bail out to the interpreter at safe points.

---

## 12. Zygote Forking Model

The Zygote is one of the most distinctive and impactful design decisions in Android's runtime architecture. It is the process from which every Android application process is forked, and it pre-initializes a complete Dalvik VM instance that is shared across all app processes via copy-on-write (COW) semantics.

### Startup Sequence

1. **`init` process** launches `app_process` (the Zygote process).
2. Zygote creates a Dalvik VM instance (`dvmStartup()`).
3. Zygote pre-loads a large set of **framework classes** and **resources** into the VM:
   - All classes in `bootclasspath` (`framework.jar`, `ext.jar`, `core.jar`, etc.)
   - Pre-loaded classes list from `/system/etc/preloaded-classes` (typically 1000–2000+ classes including `android.app.Activity`, `android.view.View`, `java.lang.String`, etc.)
   - Drawables, layouts, and other resources from framework-res.apk
   - System fonts and color state lists
4. Zygote calls `ZygoteInit.main()`, which:
   - Starts a **socket server** (`ZygoteServer`) listening on `ANDROID_SOCKET_zygote` for connection requests from the Activity Manager Service (AMS).
   - Pre-loads the **SystemServer** (the first application forked from Zygote, which runs all system services).
5. Zygote enters a **select() loop**, waiting for AMS to request a new application process.

### Fork and Specialize

When AMS needs to start a new application:

1. AMS sends a command to Zygote's socket specifying the application's package name, UID, GID, nice priority, and other parameters.
2. Zygote calls `fork()` — the Linux kernel creates a child process that is an exact copy of the parent (Zygote).
3. **Copy-on-write (COW):** The child process shares all of Zygote's **read-only pages** — including the compiled DEX bytecode (mmap'd from the APK), the preloaded class `ClassObject` structures, the intern string table, and most of the heap. Only pages that are written to by either process are copied. This means that the shared memory (often 10–20 MB of preloaded classes, strings, and resources) is physically shared across all app processes, saving significant RAM.
4. In the child process (post-fork), Zygote performs **specialization**:
   - Closes the Zygote server socket (only the parent keeps listening).
   - Sets the process UID/GID to the application's assigned IDs (using `setuid()`/`setgid()`).
   - Applies seccomp filters and SELinux context.
   - Opens the application's specific DEX file and adds it to the class loader.
   - Calls the application's `ActivityThread.main()` to start the application.

```c
// Simplified Zygote fork flow (from frameworks/base/core/java/com/android/internal/os/ZygoteInit.java)
// Native side (dalvik/vm/native/dalvik_system_Zygote.cpp):
static void Dalvik_dalvik_system_Zygote_forkAndSpecialize(...) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        setuid(uid);
        setgid(gid);
        // Set capabilities, seccomp, etc.
        // Initialize app-specific class loaders
        // Call RuntimeInit.applicationInit()
    }
    // Parent returns pid to AMS
}
```

### Memory Sharing Benefits

The COW sharing is the primary reason Android can run many apps simultaneously on memory-constrained devices. Typical memory savings:

| Shared Resource | Typical Size | Shared Via |
|----------------|-------------|------------|
| Preloaded classes (ClassObject structs) | 2–5 MB | COW (mmap)
| DEX bytecode (boot classpath) | 5–10 MB | COW (mmap)
| Intern strings | 1–3 MB | COW (heap)
| Framework resources | 1–2 MB | COW (mmap)
| JIT code cache | 0–2 MB | COW (mmap, if JIT active) |

In total, Zygote sharing saves roughly **10–20 MB per app process** on a typical Android 4.x device. On a device with 512 MB RAM running 10 apps, this can save 100–200 MB.

### Pre-Initialized VM State

After `dvmStartup()`, the Zygote VM contains:

- A fully initialized heap with pre-allocated objects (intern strings, class objects, preloaded resource objects).
- All boot classpath classes loaded, verified, linked, and (for those with `<clinit>`) initialized.
- A working JNI environment.
- The main thread's interpreter stack and thread-local data.
- String intern table, class loader hierarchy, and security manager.

All of this state is inherited by the forked child. The child doesn't need to re-verify or re-link boot classes — they're already ready to use. This dramatically reduces application startup time (often from seconds to hundreds of milliseconds).

### GC Considerations in Forked Processes

After forking, each app process has its own GC that manages its own heap. However, the COW-shared pages from Zygote complicate GC:

- The initial heap contains many pre-allocated objects (from preloaded classes). These objects are in COW pages.
- When the app's GC runs its first sweep, it must be careful not to "free" objects that are still referenced by preloaded class static fields.
- In practice, Dalvik handles this by having each forked process maintain its own allocation tracking. Objects allocated after the fork are in process-private pages; objects from before the fork are shared and never freed by the child's GC (they remain reachable via static fields and class references).
- If a child process needs to modify a shared class's static fields, the kernel triggers a page fault and copies the page, breaking the sharing for that specific page. This is acceptable because static field modifications are relatively rare.

### Zygote's Impact on Runtime Implementation

For anyone implementing a Dalvik-compatible runtime, the Zygote model imposes specific requirements:

1. **Mmap-based DEX loading:** DEX files must be mmap'd (not read into malloc'd buffers) so that the kernel can share the pages across forked processes.
2. **Deterministic class initialization:** Classes initialized in Zygote must produce identical state in all children. Non-deterministic initialization (e.g., using `System.currentTimeMillis()` or random numbers in `<clinit>`) can cause subtle bugs.
3. **No daemon threads before fork:** Zygote must not have background threads running at fork time (they would not be duplicated correctly). The Zygote process is effectively single-threaded at the point of fork.
4. **Safe post-fork behavior:** After `fork()`, the child must not call `malloc()` or any non-async-signal-safe function before `exec()` (though Android's Zygote doesn't `exec()` — it specializes in-process, so it must be more careful about reinitializing locks and thread state).

---

## Appendix: Key Source File References

| Component | AOSP Path (Android 4.4) |
|-----------|------------------------|
| DEX header/structures | `dalvik/libdex/DexFile.h` |
| DEX parsing | `dalvik/libdex/DexFile.cpp` |
| Class loading | `dalvik/vm/oo/Class.cpp` |
| Verification | `dalvik/vm/Verify.cpp` |
| Interpreter (portable) | `dalvik/vm/interp/Interp.c` |
| Interpreter (mterp) | `dalvik/vm/mterp/` (templates + generated) |
| JIT compiler | `dalvik/vm/compiler/` |
| GC | `dalvik/vm/alloc/Alloc.cpp`, `Heap.cpp` |
| JNI | `dalvik/vm/Jni.cpp` |
| Native method bridge | `dalvik/vm/Native.cpp` |
| Object/ClassObject | `dalvik/vm/oo/Object.h` |
| Thread/Stack | `dalvik/vm/Thread.h` |
| Zygote | `dalvik/vm/native/dalvik_system_Zygote.cpp` |