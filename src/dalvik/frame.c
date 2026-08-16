/*
 * EXP-034: Dalvik Register Frame Model Prototype — Implementation
 *
 * Memory layout per frame (flat array in regs[]):
 *
 *   regs[0 .. locals_size-1]          = locals  (v0 .. v[L-1])
 *   regs[locals_size .. locals_size+ins_size-1] = ins (arguments)
 *   regs[registers_size .. registers_size+outs_size-1] = outs (callee args)
 *
 * This matches Dalvik's actual layout where ins follow locals,
 * and outs occupy the highest register slots.
 */

#include "frame.h"
#include <stdio.h>
#include <string.h>

/* ============================================================
 * Frame Stack Lifecycle
 * ============================================================ */

void frame_stack_init(FrameStack* fs, int trace) {
    memset(fs, 0, sizeof(FrameStack));
    fs->trace_enabled = trace;
    fs->next_frame_id = 1;
}

Frame* frame_push(FrameStack* fs, const MethodMetadata* method) {
    if (fs->count >= FRAME_MAX_DEPTH) {
        if (fs->trace_enabled) {
            fprintf(stderr, "[FRAME ERROR] stack overflow at depth %u\n", fs->count);
        }
        return NULL;
    }

    Frame* f = &fs->frames[fs->count];
    memset(f, 0, sizeof(Frame));

    /* Copy method metadata */
    f->registers_size = method->registers_size;
    f->ins_size       = method->ins_size;
    f->outs_size      = method->outs_size;
    f->locals_size    = method->locals_size;
    f->method         = method;
    f->frame_id       = fs->next_frame_id++;

    /* Dalvik layout: [locals | ins | outs] */
    f->ins_offset  = f->locals_size;        /* ins start right after locals */
    f->outs_offset = f->registers_size;     /* outs start after all v-regs */

    /* Linkage */
    if (fs->count > 0) {
        f->prev  = &fs->frames[fs->count - 1];
        f->depth = f->prev->depth + 1;
    } else {
        f->prev  = NULL;
        f->depth = 0;
    }

    /* Mark all registers as uninitialized */
    for (uint16_t i = 0; i < FRAME_MAX_REGS; i++) {
        f->regs[i].type_tag = 0; /* uninit */
    }

    fs->count++;

    if (fs->trace_enabled) {
        frame_trace_state(f);
    }

    return f;
}

void frame_pop(FrameStack* fs, RegValue* out_retval) {
    if (fs->count == 0) {
        if (fs->trace_enabled) {
            fprintf(stderr, "[FRAME ERROR] pop on empty stack\n");
        }
        return;
    }

    Frame* f = &fs->frames[fs->count - 1];

    if (fs->trace_enabled) {
        frame_trace_return(f);
    }

    /* Copy return value to caller if requested */
    if (out_retval && f->has_retval) {
        *out_retval = f->retval;
    }

    /* Clear frame */
    memset(f, 0, sizeof(Frame));
    fs->count--;
}

Frame* frame_current(FrameStack* fs) {
    if (fs->count == 0) return NULL;
    return &fs->frames[fs->count - 1];
}

Frame* frame_caller(FrameStack* fs) {
    Frame* cur = frame_current(fs);
    if (!cur) return NULL;
    return cur->prev;
}

/* ============================================================
 * Register Access
 * ============================================================ */

RegValue frame_get_reg(Frame* f, uint16_t idx) {
    if (idx >= FRAME_MAX_REGS) {
        RegValue bad = { .value = 0, .type_tag = 0 };
        return bad;
    }
    return f->regs[idx];
}

void frame_set_reg(Frame* f, uint16_t idx, RegValue val) {
    if (idx >= FRAME_MAX_REGS) return;
    f->regs[idx] = val;
}

/* Incoming argument: ins[arg_idx] = regs[locals_size + arg_idx] */
RegValue frame_get_in(Frame* f, uint16_t arg_idx) {
    uint16_t idx = f->ins_offset + arg_idx;
    return frame_get_reg(f, idx);
}

void frame_set_in(Frame* f, uint16_t arg_idx, RegValue val) {
    uint16_t idx = f->ins_offset + arg_idx;
    frame_set_reg(f, idx, val);
}

/* Local: local[local_idx] = regs[local_idx] */
RegValue frame_get_local(Frame* f, uint16_t local_idx) {
    return frame_get_reg(f, local_idx);
}

void frame_set_local(Frame* f, uint16_t local_idx, RegValue val) {
    frame_set_reg(f, local_idx, val);
}

/* Outgoing: out[out_idx] = regs[registers_size + out_idx] */
RegValue frame_get_out(Frame* f, uint16_t out_idx) {
    uint16_t idx = f->outs_offset + out_idx;
    return frame_get_reg(f, idx);
}

void frame_set_out(Frame* f, uint16_t out_idx, RegValue val) {
    uint16_t idx = f->outs_offset + out_idx;
    frame_set_reg(f, idx, val);
}

/* ============================================================
 * Invoke Simulation
 * ============================================================ */

Frame* frame_invoke(FrameStack* fs, const MethodMetadata* callee,
                     const RegValue* args, uint16_t nargs) {
    if (fs->trace_enabled) {
        frame_trace_invoke(callee, args, nargs);
    }

    /* Temporarily suppress trace during push (args not yet copied) */
    int saved_trace = fs->trace_enabled;
    fs->trace_enabled = 0;

    /* Push callee frame */
    Frame* callee_frame = frame_push(fs, callee);

    fs->trace_enabled = saved_trace;
    if (!callee_frame) return NULL;

    /* Copy arguments from args[] into callee's ins[] */
    uint16_t to_copy = nargs < callee_frame->ins_size ? nargs : callee_frame->ins_size;
    for (uint16_t i = 0; i < to_copy; i++) {
        frame_set_in(callee_frame, i, args[i]);
    }

    /* Mark unused ins as uninit */
    for (uint16_t i = to_copy; i < callee_frame->ins_size; i++) {
        callee_frame->regs[callee_frame->ins_offset + i] = make_reg_uninit();
    }

    /* Now trace the frame state with arguments populated */
    if (fs->trace_enabled) {
        frame_trace_state(callee_frame);
    }

    return callee_frame;
}

/* ============================================================
 * Return Simulation
 * ============================================================ */

void frame_set_return(Frame* f, RegValue val) {
    f->retval     = val;
    f->has_retval = 1;
}

/* ============================================================
 * Trace Output
 * ============================================================ */

void frame_trace_state(Frame* f) {
    fprintf(stdout, "FRAME CREATE  id=%u depth=%u\n", f->frame_id, f->depth);
    fprintf(stdout, "  method: %s->%s %s\n",
            f->method->class_name, f->method->method_name, f->method->signature);
    fprintf(stdout, "  register count: %u (locals=%u ins=%u outs=%u)\n",
            f->registers_size, f->locals_size, f->ins_size, f->outs_size);

    /* Dump ins */
    fprintf(stdout, "  incoming [");
    for (uint16_t i = 0; i < f->ins_size; i++) {
        RegValue v = frame_get_in(f, i);
        if (i > 0) fprintf(stdout, ", ");
        if (v.type_tag == 2)
            fprintf(stdout, "p%u=ref(0x%lx)", i, (unsigned long)v.value);
        else
            fprintf(stdout, "p%u=%ld", i, (long)v.value);
    }
    fprintf(stdout, "]\n");

    /* Dump locals */
    fprintf(stdout, "  locals [");
    int any_local = 0;
    for (uint16_t i = 0; i < f->locals_size; i++) {
        RegValue v = frame_get_local(f, i);
        if (v.type_tag != 0) { /* only show initialized */
            if (any_local) fprintf(stdout, ", ");
            fprintf(stdout, "v%u=%ld", i, (long)v.value);
            any_local = 1;
        }
    }
    if (!any_local) fprintf(stdout, "(empty)");
    fprintf(stdout, "]\n");

    /* Dump outs */
    fprintf(stdout, "  outgoing [");
    int any_out = 0;
    for (uint16_t i = 0; i < f->outs_size; i++) {
        RegValue v = frame_get_out(f, i);
        if (v.type_tag != 0) {
            if (any_out) fprintf(stdout, ", ");
            if (v.type_tag == 2)
                fprintf(stdout, "out%u=ref(0x%lx)", i, (unsigned long)v.value);
            else
                fprintf(stdout, "out%u=%ld", i, (long)v.value);
            any_out = 1;
        }
    }
    if (!any_out) fprintf(stdout, "(empty)");
    fprintf(stdout, "]\n");
}

void frame_trace_invoke(const MethodMetadata* callee,
                        const RegValue* args, uint16_t nargs) {
    (void)0; /* no caller reference available at this point */
    fprintf(stdout, "INVOKE  %s->%s %s\n",
            callee->class_name, callee->method_name, callee->signature);
    fprintf(stdout, "  args [");
    for (uint16_t i = 0; i < nargs; i++) {
        if (i > 0) fprintf(stdout, ", ");
        if (args[i].type_tag == 2)
            fprintf(stdout, "a%u=ref(0x%lx)", i, (unsigned long)args[i].value);
        else
            fprintf(stdout, "a%u=%ld", i, (long)args[i].value);
    }
    fprintf(stdout, "]\n");
}

void frame_trace_return(Frame* f) {
    fprintf(stdout, "RETURN  %s->%s",
            f->method->class_name, f->method->method_name);
    if (f->has_retval) {
        if (f->retval.type_tag == 2)
            fprintf(stdout, "  value=ref(0x%lx)", (unsigned long)f->retval.value);
        else
            fprintf(stdout, "  value=%ld", (long)f->retval.value);
    } else {
        fprintf(stdout, "  value=void");
    }
    fprintf(stdout, "\n");
}

/* ============================================================
 * Debug: Full Stack Dump
 * ============================================================ */

void frame_stack_dump(FrameStack* fs) {
    fprintf(stdout, "\n=== FRAME STACK DUMP (depth=%u) ===\n", fs->count);
    for (uint32_t i = 0; i < fs->count; i++) {
        Frame* f = &fs->frames[i];
        fprintf(stdout, "[%u] id=%u %s->%s  regs=%u  ins=%u  locals=%u  outs=%u\n",
                i, f->frame_id,
                f->method->class_name, f->method->method_name,
                f->registers_size, f->ins_size, f->locals_size, f->outs_size);
    }
    fprintf(stdout, "=== END DUMP ===\n\n");
}
