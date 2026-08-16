/*
 * EXP-034: Dalvik Register Frame Model Prototype
 * Minimal C implementation of Dalvik-style register frames.
 *
 * Dalvik partitions each method's virtual registers into three regions:
 *   - locals[]  : v0 .. v(locals_size-1)        — method-local temps
 *   - ins[]     : vN .. v(N+ins_size-1)          — incoming arguments
 *   - outs[]    : vM .. v(M+outs_size-1)         — callee argument slots
 *     where N = registers_size - ins_size
 *           M = registers_size
 *
 * The frame also holds:
 *   - retval     : return value register (rV)
 *   - method_ref : pointer to method metadata
 *   - prev       : link to caller's frame
 */

#ifndef DALVIK_FRAME_H
#define DALVIK_FRAME_H

#include <stdint.h>
#include <stddef.h>

/* Maximum frame depth for nested calls */
#define FRAME_MAX_DEPTH 64

/* Maximum registers per frame (Dalvik max is 65535) */
#define FRAME_MAX_REGS 256

/* Maximum outgoing args (Dalvik max is 5 for non-range, but we support more) */
#define FRAME_MAX_OUTS 16

/* ---- Method Metadata ---- */

typedef enum {
    METHOD_STATIC  = 0x0001,
    METHOD_DIRECT  = 0x0002,
    METHOD_VIRTUAL = 0x0004,
    METHOD_INTERFACE = 0x0008
} MethodFlags;

typedef struct {
    const char* class_name;      /* e.g. "LFoo;" */
    const char* method_name;     /* e.g. "foo" */
    const char* signature;       /* e.g. "(II)I" */
    uint16_t    registers_size;  /* total virtual registers for this method */
    uint16_t    ins_size;        /* number of incoming argument registers */
    uint16_t    outs_size;       /* number of outgoing argument slots */
    uint16_t    locals_size;     /* registers_size - ins_size */
    MethodFlags flags;
} MethodMetadata;

/* ---- Register Value ---- */

typedef struct {
    uint64_t value;    /* raw 64-bit register value */
    uint32_t type_tag; /* 0=uninit, 1=int, 2=ref, 3=float, 8=byte, 9=short, 10=char */
} RegValue;

/* ---- Frame ---- */

typedef struct Frame {
    /* Layout mirrors Dalvik: locals[] then ins[] then outs[] */
    RegValue    regs[FRAME_MAX_REGS]; /* flat array: [locals | ins | outs] */

    uint16_t    registers_size;  /* total v-registers (locals + ins) */
    uint16_t    ins_size;        /* incoming argument count */
    uint16_t    outs_size;       /* outgoing argument slot count */
    uint16_t    locals_size;     /* = registers_size - ins_size */

    /* Offsets into regs[] */
    uint16_t    ins_offset;      /* first incoming register index */
    uint16_t    outs_offset;     /* first outgoing register index */

    /* Return value (rV) — stored outside the v-register array */
    RegValue    retval;
    int         has_retval;      /* 1 if return value was set */

    /* Method reference */
    const MethodMetadata* method;

    /* Frame linkage */
    struct Frame* prev;          /* caller's frame (NULL for bottom) */
    uint32_t    depth;           /* call depth (0 = bottom) */

    /* Trace ID for logging */
    uint32_t    frame_id;
} Frame;

/* ---- Frame Stack ---- */

typedef struct {
    Frame       frames[FRAME_MAX_DEPTH];
    uint32_t    count;           /* current number of active frames */
    uint32_t    next_frame_id;   /* auto-incrementing ID for trace */
    int         trace_enabled;   /* 1 = print trace, 0 = silent */
} FrameStack;

/* ---- Frame Lifecycle ---- */

/* Initialize a frame stack */
void frame_stack_init(FrameStack* fs, int trace);

/* Push a new frame for a method call.
 * Returns pointer to the new frame, or NULL if stack overflow. */
Frame* frame_push(FrameStack* fs, const MethodMetadata* method);

/* Pop the top frame, returning to caller.
 * Copies retval from callee to caller's outgoing slot or retval. */
void frame_pop(FrameStack* fs, RegValue* out_retval);

/* Get current (top) frame */
Frame* frame_current(FrameStack* fs);

/* Get caller (previous) frame */
Frame* frame_caller(FrameStack* fs);

/* ---- Register Access ---- */

/* Get/set a virtual register by index (v0, v1, ...) */
RegValue frame_get_reg(Frame* f, uint16_t idx);
void     frame_set_reg(Frame* f, uint16_t idx, RegValue val);

/* Access incoming argument registers (p0 = ins[0], p1 = ins[1], ...) */
RegValue frame_get_in(Frame* f, uint16_t arg_idx);
void     frame_set_in(Frame* f, uint16_t arg_idx, RegValue val);

/* Access local registers (local[0], local[1], ...) */
RegValue frame_get_local(Frame* f, uint16_t local_idx);
void     frame_set_local(Frame* f, uint16_t local_idx, RegValue val);

/* Access outgoing registers (out[0], out[1], ...) */
RegValue frame_get_out(Frame* f, uint16_t out_idx);
void     frame_set_out(Frame* f, uint16_t out_idx, RegValue val);

/* ---- Invoke Simulation ---- */

/* Simulate copying args from caller's outgoing area into callee's ins[].
 * This is the core of Dalvik's invoke-* mechanism.
 *
 * For non-range invokes: args come from caller registers vA, vB, vC, vD, vE
 * For range invokes:      args come from caller registers {vC .. vC+N-1}
 *
 * This function handles both by accepting an array of RegValue and count.
 * It also pushes the callee frame and fills its ins[].
 */
Frame* frame_invoke(FrameStack* fs, const MethodMetadata* callee,
                     const RegValue* args, uint16_t nargs);

/* ---- Return Simulation ---- */

/* Set return value in current frame */
void frame_set_return(Frame* f, RegValue val);

/* ---- Trace Output ---- */

/* Print frame state */
void frame_trace_state(Frame* f);

/* Print invoke trace */
void frame_trace_invoke(const MethodMetadata* callee,
                        const RegValue* args, uint16_t nargs);

/* Print return trace */
void frame_trace_return(Frame* f);

/* ---- Debug ---- */

/* Dump entire frame stack */
void frame_stack_dump(FrameStack* fs);

/* Make a tagged register value */
static inline RegValue make_reg_i(int32_t v) {
    RegValue r = { .value = (uint64_t)(int64_t)v, .type_tag = 1 };
    return r;
}
static inline RegValue make_reg_ref(uintptr_t v) {
    RegValue r = { .value = (uint64_t)v, .type_tag = 2 };
    return r;
}
static inline RegValue make_reg_uninit(void) {
    RegValue r = { .value = 0, .type_tag = 0 };
    return r;
}

#endif /* DALVIK_FRAME_H */
