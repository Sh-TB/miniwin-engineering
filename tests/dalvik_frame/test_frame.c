/*
 * EXP-034: Register Frame Model — Test Suite
 *
 * Test A: Static method  foo(int a, int b) -> int
 * Test B: Instance method  object.method(int x) — "this" preservation
 * Test C: Nested call  A -> B -> C — independent frame registers
 *
 * Build: gcc -o test_frame test_frame.c ../src/dalvik/frame.c -I../src/dalvik -Wall -O2
 * Run:   ./test_frame 2>&1 | tee test_evidence.log
 */

#include "frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT_INT_EQ(msg, actual, expected) do {                         \
    g_tests_run++;                                                        \
    if ((actual) == (expected)) {                                         \
        g_tests_passed++;                                                 \
        printf("  PASS: %s (%ld == %ld)\n", msg,                        \
               (long)(actual), (long)(expected));                         \
    } else {                                                              \
        g_tests_failed++;                                                 \
        printf("  FAIL: %s (%ld != %ld)\n", msg,                        \
               (long)(actual), (long)(expected));                         \
    }                                                                     \
} while(0)

/* ==============================================================
 * Test A: Static method  foo(int a, int b) -> int
 *
 * Dalvik: invoke-static {v0, v1}, LFoo;->foo(II)I
 *   v0 = 3 (first arg = a)
 *   v1 = 7 (second arg = b)
 *   callee has: registers_size=3, ins_size=2, outs_size=1, locals_size=1
 *
 * Verify:
 *   - arguments arrive correctly in callee's ins[]
 *   - return value is preserved across frame pop
 * ==============================================================*/

/* Method metadata for foo */
static const MethodMetadata foo_method = {
    .class_name     = "LFoo;",
    .method_name    = "foo",
    .signature      = "(II)I",
    .registers_size = 3,    /* v0..v2: 2 ins + 1 local */
    .ins_size       = 2,    /* a, b */
    .outs_size      = 1,    /* space for one outgoing call */
    .locals_size    = 1,    /* v0 = local temp */
    .flags          = METHOD_STATIC
};

/* Method metadata for a caller that calls foo */
static const MethodMetadata caller_a = {
    .class_name     = "LMain;",
    .method_name    = "main",
    .signature      = "()V",
    .registers_size = 4,
    .ins_size       = 0,    /* static main has no args in this simplified model */
    .outs_size      = 2,    /* needs 2 outs for calling foo */
    .locals_size    = 4,
    .flags          = METHOD_STATIC
};

static void test_a_static_method(void) {
    printf("\n========================================\n");
    printf("TEST A: Static method foo(int a, int b) -> int\n");
    printf("========================================\n\n");

    FrameStack fs;
    frame_stack_init(&fs, 1);  /* trace enabled */

    /* 1. Push caller frame (main) */
    Frame* caller = frame_push(&fs, &caller_a);

    /* 2. Simulate: const v0, 3  and  const v1, 7 */
    frame_set_reg(caller, 0, make_reg_i(3));  /* v0 = 3 */
    frame_set_reg(caller, 1, make_reg_i(7));  /* v1 = 7 */

    /* 3. Simulate: invoke-static {v0, v1}, LFoo;->foo(II)I
     *    In Dalvik, args are copied to caller's outs[] first,
     *    then outs[] is the source for callee's ins[]. */
    RegValue invoke_args[2];
    invoke_args[0] = frame_get_reg(caller, 0);  /* v0 -> arg0 */
    invoke_args[1] = frame_get_reg(caller, 1);  /* v1 -> arg1 */

    Frame* callee = frame_invoke(&fs, &foo_method, invoke_args, 2);

    /* 4. Verify arguments arrived correctly */
    RegValue a_val = frame_get_in(callee, 0);
    RegValue b_val = frame_get_in(callee, 1);

    ASSERT_INT_EQ("arg a == 3", (int32_t)a_val.value, 3);
    ASSERT_INT_EQ("arg b == 7", (int32_t)b_val.value, 7);

    /* 5. Simulate foo's body: add v2, v0, v1  (v2 = a + b)
     *    In Dalvik: v2 maps to local[0] (since ins start at v1 in this method)
     *    Actually: locals are v0, ins are v1,v2 (locals_size=1, ins_size=2)
     *    So local[0] = v0, ins[0] = v1 (arg a), ins[1] = v2 (arg b)
     *    We compute: local[0] = ins[0] + ins[1] */
    int32_t result = (int32_t)a_val.value + (int32_t)b_val.value;
    frame_set_local(callee, 0, make_reg_i(result));

    /* 6. Simulate: return v0 (local[0]) */
    frame_set_return(callee, frame_get_local(callee, 0));

    /* 7. Pop callee frame, get return value */
    RegValue ret_val;
    frame_pop(&fs, &ret_val);

    /* 8. Verify return value preserved */
    ASSERT_INT_EQ("return value == 10", (int32_t)ret_val.value, 10);

    /* 9. Verify caller's registers are UNAFFECTED by callee */
    RegValue caller_v0 = frame_get_reg(caller, 0);
    RegValue caller_v1 = frame_get_reg(caller, 1);
    ASSERT_INT_EQ("caller v0 preserved == 3", (int32_t)caller_v0.value, 3);
    ASSERT_INT_EQ("caller v1 preserved == 7", (int32_t)caller_v1.value, 7);

    /* Clean up caller */
    frame_pop(&fs, NULL);

    printf("\n");
}

/* ==============================================================
 * Test B: Instance method  object.method(int x)
 *
 * Dalvik: invoke-virtual {v0, v1}, LBar;->doIt(I)V
 *   v0 = object reference ("this")
 *   v1 = 42 (int x)
 *   callee has: registers_size=4, ins_size=2, outs_size=2, locals_size=2
 *
 * Verify:
 *   - ins[0] contains the object reference ("this")
 *   - ins[1] contains the integer argument
 *   - "this" reference is preserved (not corrupted)
 * ==============================================================*/

/* Fake object pointer */
#define FAKE_OBJ_PTR  0xDEADBEEFCAFE0000ULL

static const MethodMetadata doIt_method = {
    .class_name     = "LBar;",
    .method_name    = "doIt",
    .signature      = "(I)V",
    .registers_size = 4,    /* v0..v3: 2 ins + 2 locals */
    .ins_size       = 2,    /* this, x */
    .outs_size      = 2,
    .locals_size    = 2,
    .flags          = METHOD_VIRTUAL
};

static const MethodMetadata caller_b = {
    .class_name     = "LMain;",
    .method_name    = "testInstance",
    .signature      = "()V",
    .registers_size = 4,
    .ins_size       = 0,
    .outs_size      = 2,    /* needs 2 outs: this + x */
    .locals_size    = 4,
    .flags          = METHOD_STATIC
};

static void test_b_instance_method(void) {
    printf("\n========================================\n");
    printf("TEST B: Instance method object.method(int x)\n");
    printf("========================================\n\n");

    FrameStack fs;
    frame_stack_init(&fs, 1);

    /* 1. Push caller */
    Frame* caller = frame_push(&fs, &caller_b);

    /* 2. v0 = object reference, v1 = 42 */
    frame_set_reg(caller, 0, make_reg_ref(FAKE_OBJ_PTR));  /* v0 = this */
    frame_set_reg(caller, 1, make_reg_i(42));                /* v1 = x */

    /* 3. invoke-virtual {v0, v1}, LBar;->doIt(I)V */
    RegValue invoke_args[2];
    invoke_args[0] = frame_get_reg(caller, 0);  /* this */
    invoke_args[1] = frame_get_reg(caller, 1);  /* x */

    Frame* callee = frame_invoke(&fs, &doIt_method, invoke_args, 2);

    /* 4. Verify "this" reference in ins[0] */
    RegValue this_val = frame_get_in(callee, 0);
    ASSERT_INT_EQ("ins[0] is ref type", this_val.type_tag, 2);
    ASSERT_INT_EQ("ins[0] == FAKE_OBJ_PTR",
                   (this_val.value == FAKE_OBJ_PTR) ? 1 : 0, 1);

    /* 5. Verify argument x in ins[1] */
    RegValue x_val = frame_get_in(callee, 1);
    ASSERT_INT_EQ("ins[1] == 42", (int32_t)x_val.value, 42);
    ASSERT_INT_EQ("ins[1] is int type", x_val.type_tag, 1);

    /* 6. Simulate method body: use "this" and x */
    /*    e.g., this.field = x → store x into a local for evidence */
    frame_set_local(callee, 0, this_val);  /* save this to local[0] */
    frame_set_local(callee, 1, x_val);     /* save x to local[1] */

    /* Verify locals are independent from ins */
    RegValue local_this = frame_get_local(callee, 0);
    RegValue local_x    = frame_get_local(callee, 1);
    ASSERT_INT_EQ("local[0] still has this ref",
                   (local_this.value == FAKE_OBJ_PTR) ? 1 : 0, 1);
    ASSERT_INT_EQ("local[1] still has x", (int32_t)local_x.value, 42);

    /* 7. void return */
    frame_pop(&fs, NULL);

    /* 8. Verify caller's registers untouched */
    RegValue caller_v0 = frame_get_reg(caller, 0);
    RegValue caller_v1 = frame_get_reg(caller, 1);
    ASSERT_INT_EQ("caller v0 (this) preserved",
                   (caller_v0.value == FAKE_OBJ_PTR) ? 1 : 0, 1);
    ASSERT_INT_EQ("caller v1 (x) preserved", (int32_t)caller_v1.value, 42);

    frame_pop(&fs, NULL);
    printf("\n");
}

/* ==============================================================
 * Test C: Nested call  A -> B -> C
 *
 * Three methods calling each other:
 *   A.main() calls B.add(3, 5)
 *   B.add(3, 5) computes 3+5=8, then calls C.multiply(8, 2)
 *   C.multiply(8, 2) returns 16
 *   B.multiply returns 16 to A
 *
 * Verify:
 *   - each frame keeps independent registers
 *   - A's registers are not corrupted by B or C
 *   - B's registers are not corrupted by C
 *   - return value chains correctly: C -> B -> A
 * ==============================================================*/

static const MethodMetadata method_A = {
    .class_name     = "LA;",
    .method_name    = "main",
    .signature      = "()V",
    .registers_size = 4,
    .ins_size       = 0,
    .outs_size      = 2,
    .locals_size    = 4,
    .flags          = METHOD_STATIC
};

static const MethodMetadata method_B = {
    .class_name     = "LB;",
    .method_name    = "add",
    .signature      = "(II)I",
    .registers_size = 4,
    .ins_size       = 2,    /* a, b */
    .outs_size      = 2,    /* for calling C */
    .locals_size    = 2,
    .flags          = METHOD_STATIC
};

static const MethodMetadata method_C = {
    .class_name     = "LC;",
    .method_name    = "multiply",
    .signature      = "(II)I",
    .registers_size = 4,
    .ins_size       = 2,    /* a, b */
    .outs_size      = 1,
    .locals_size    = 2,
    .flags          = METHOD_STATIC
};

static void test_c_nested_calls(void) {
    printf("\n========================================\n");
    printf("TEST C: Nested call A -> B -> C\n");
    printf("========================================\n\n");

    FrameStack fs;
    frame_stack_init(&fs, 1);

    /* ---- Frame A: main ---- */
    Frame* frameA = frame_push(&fs, &method_A);

    /* A sets up args for B: v0=3, v1=5 */
    frame_set_reg(frameA, 0, make_reg_i(3));
    frame_set_reg(frameA, 1, make_reg_i(5));
    /* A also sets v2=999 to prove it's not clobbered */
    frame_set_reg(frameA, 2, make_reg_i(999));

    printf("\n--- A calls B.add(3, 5) ---\n");
    RegValue args_b[2] = {
        frame_get_reg(frameA, 0),
        frame_get_reg(frameA, 1)
    };
    Frame* frameB = frame_invoke(&fs, &method_B, args_b, 2);

    /* Verify B received correct args */
    RegValue b_a = frame_get_in(frameB, 0);
    RegValue b_b = frame_get_in(frameB, 1);
    ASSERT_INT_EQ("B: arg a == 3", (int32_t)b_a.value, 3);
    ASSERT_INT_EQ("B: arg b == 5", (int32_t)b_b.value, 5);

    /* ---- B's body: compute a+b, then call C.multiply(sum, 2) ---- */
    int32_t sum = (int32_t)b_a.value + (int32_t)b_b.value;  /* 3+5=8 */
    frame_set_local(frameB, 0, make_reg_i(sum));  /* local[0] = 8 */

    printf("\n--- B calls C.multiply(8, 2) ---\n");
    RegValue args_c[2] = {
        frame_get_local(frameB, 0),  /* 8 */
        make_reg_i(2)
    };
    Frame* frameC = frame_invoke(&fs, &method_C, args_c, 2);

    /* Verify C received correct args */
    RegValue c_a = frame_get_in(frameC, 0);
    RegValue c_b = frame_get_in(frameC, 1);
    ASSERT_INT_EQ("C: arg a == 8", (int32_t)c_a.value, 8);
    ASSERT_INT_EQ("C: arg b == 2", (int32_t)c_b.value, 2);

    /* ---- C's body: return a * b = 8 * 2 = 16 ---- */
    int32_t product = (int32_t)c_a.value * (int32_t)c_b.value;
    frame_set_return(frameC, make_reg_i(product));

    /* Pop C, return to B */
    RegValue ret_c;
    printf("\n--- C returns to B ---\n");
    frame_pop(&fs, &ret_c);

    ASSERT_INT_EQ("C return value == 16", (int32_t)ret_c.value, 16);

    /* ---- B receives C's return, now B returns it to A ---- */
    /*    In Dalvik: move-result v0  → stores to local register */
    frame_set_local(frameB, 1, ret_c);  /* save C's result in B's local[1] */
    frame_set_return(frameB, ret_c);    /* B returns what C returned */

    /* Verify B's own local[0] (sum=8) was NOT corrupted by C */
    RegValue b_local0 = frame_get_local(frameB, 0);
    ASSERT_INT_EQ("B: local[0] (sum) still == 8 after C call",
                   (int32_t)b_local0.value, 8);

    /* Pop B, return to A */
    RegValue ret_b;
    printf("\n--- B returns to A ---\n");
    frame_pop(&fs, &ret_b);

    ASSERT_INT_EQ("B return value == 16", (int32_t)ret_b.value, 16);

    /* ---- A receives B's return ---- */
    /*    In Dalvik: move-result v3 */
    frame_set_reg(frameA, 3, ret_b);

    /* Verify A's registers were NOT corrupted by B or C */
    RegValue a_v0 = frame_get_reg(frameA, 0);
    RegValue a_v1 = frame_get_reg(frameA, 1);
    RegValue a_v2 = frame_get_reg(frameA, 2);
    RegValue a_v3 = frame_get_reg(frameA, 3);
    ASSERT_INT_EQ("A: v0 == 3 (uncorrupted)", (int32_t)a_v0.value, 3);
    ASSERT_INT_EQ("A: v1 == 5 (uncorrupted)", (int32_t)a_v1.value, 5);
    ASSERT_INT_EQ("A: v2 == 999 (uncorrupted)", (int32_t)a_v2.value, 999);
    ASSERT_INT_EQ("A: v3 == 16 (return value)", (int32_t)a_v3.value, 16);

    /* Full stack dump at the end */
    frame_stack_dump(&fs);

    frame_pop(&fs, NULL);
    printf("\n");
}

/* ==============================================================
 * Main
 * ==============================================================*/

int main(void) {
    printf("EXP-034: Dalvik Register Frame Model — Test Suite\n");
    printf("=================================================\n");

    test_a_static_method();
    test_b_instance_method();
    test_c_nested_calls();

    printf("\n========================================\n");
    printf("RESULTS: %d passed, %d failed, %d total\n",
           g_tests_passed, g_tests_failed, g_tests_run);
    printf("========================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}
