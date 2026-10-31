#!/usr/bin/env python3
"""SWEEP-035 splice: apply sweep035_impl.c into src/loader/loader.c.
Train 4 of 5 — every replacement must match EXACTLY ONCE."""
import sys, re

LOADER = "src/loader/loader.c"
IMPL = "scripts/sweep035_impl.c"

with open(LOADER) as f:
    src = f.read()
with open(IMPL) as f:
    impl = f.read()

def section(name):
    pat = r"/\* ==== SECTION: %s ==== \*/\n(.*?)(?=/\* ==== SECTION:|/\* ==== SWEEP-035 IMPL END ==== \*/)" % re.escape(name)
    m = re.search(pat, impl, re.S)
    if not m:
        raise SystemExit(f"impl section '{name}' not found")
    return m.group(1).rstrip() + "\n"

fails = []
def replace(old, new, label):
    global src
    n = src.count(old)
    if n != 1:
        fails.append(f"FAILED [{label}]: {n} occurrences (need exactly 1)")
        return
    src = src.replace(old, new)
    print(f"  ok: {label}")

print("== SWEEP-035 splice ==")

# ---- 1. prototypes + pre-create fire inside CreateWindowExW (W and A) ----
proto = """/* SW-1203: WH_CBT event delivery (defined after the hook machinery) */
int mw_hook_fire_create(uint32_t exstyle, uint32_t style, const void* winname,
        const void* clsname, int x, int y, int w, int h, void* parent,
        void* menu, void* inst, void* param);
void mw_hook_fire_destroy(void* hwnd);
void mw_hook_fire_activate(void* hwnd);

__attribute__((ms_abi)) void* mw_CreateWindowExW(uint32_t exstyle, const void* cls,
        const uint16_t* win, uint32_t style, int x, int y, int w, int h,
        void* parent, void* menu, void* inst, void* param) {
    int sc = mw_sysclass_w(cls);
    if (sc && parent) { /* SWEEP-013: child control — engine-internal wndproc */
        (void)exstyle; (void)x; (void)y; (void)style;
        return mw_child_register(sc, parent, menu, win, 1, NULL);
    }
    /* SW-1203: WH_CBT pre-create notification — nonzero CANCELS (real CBT) */
    if (mw_hook_fire_create(exstyle, style, win, cls, x, y, w, h, parent, menu, inst, param))
        return NULL;
    g_create_param = param; /* SW-802..806: forward lpParam into CREATESTRUCT */
    mw_gui_store_window((int)w, (int)h); /* SWEEP-005: allocate paint surface */
    mw_gui_send_create(); /* SWEEP-006: synchronous WM_CREATE */
    MW_TRACE("CreateWindowExW(ex=0x%x style=0x%x cls=%p %dx%d@%d,%d) -> HWND %p",
             exstyle, style, cls, w, h, x, y, MW_GUI_HWND);
    return MW_GUI_HWND;
}
__attribute__((ms_abi)) void* mw_CreateWindowExA(uint32_t exstyle, const void* cls,
        const char* win, uint32_t style, int x, int y, int w, int h,
        void* parent, void* menu, void* inst, void* param) {
    int sc = mw_sysclass_a(cls);
    if (sc && parent) { /* SWEEP-013: child control — engine-internal wndproc */
        (void)exstyle; (void)x; (void)y; (void)style;
        return mw_child_register(sc, parent, menu, win, 0, NULL);
    }
    /* SW-1203: WH_CBT pre-create notification — nonzero CANCELS (real CBT);
     * the A path passes the ANSI name reinterpreted — hooks read names
     * through the same pointer the caller would have used on Windows. */
    if (mw_hook_fire_create(exstyle, style, win, cls, x, y, w, h, parent, menu, inst, param))
        return NULL;
    g_create_param = param; /* SW-802..806: forward lpParam into CREATESTRUCT */
    mw_gui_store_window((int)w, (int)h); /* SWEEP-005: allocate paint surface */
    mw_gui_send_create(); /* SWEEP-006: synchronous WM_CREATE */
    MW_TRACE("CreateWindowExA(ex=0x%x style=0x%x cls=%p %dx%d@%d,%d) -> HWND %p",
             exstyle, style, cls, w, h, x, y, MW_GUI_HWND);
    return MW_GUI_HWND;
}"""

cw_w = """__attribute__((ms_abi)) void* mw_CreateWindowExW(uint32_t exstyle, const void* cls,
        const uint16_t* win, uint32_t style, int x, int y, int w, int h,
        void* parent, void* menu, void* inst, void* param) {
    int sc = mw_sysclass_w(cls);
    if (sc && parent) { /* SWEEP-013: child control — engine-internal wndproc */
        (void)exstyle; (void)x; (void)y; (void)style;
        return mw_child_register(sc, parent, menu, win, 1, NULL);
    }
    g_create_param = param; /* SW-802..806: forward lpParam into CREATESTRUCT */
    mw_gui_store_window((int)w, (int)h); /* SWEEP-005: allocate paint surface */
    mw_gui_send_create(); /* SWEEP-006: synchronous WM_CREATE */
    MW_TRACE("CreateWindowExW(ex=0x%x style=0x%x cls=%p %dx%d@%d,%d) -> HWND %p",
             exstyle, style, cls, w, h, x, y, MW_GUI_HWND);
    return MW_GUI_HWND;
}
__attribute__((ms_abi)) void* mw_CreateWindowExA(uint32_t exstyle, const void* cls,
        const char* win, uint32_t style, int x, int y, int w, int h,
        void* parent, void* menu, void* inst, void* param) {
    int sc = mw_sysclass_a(cls);
    if (sc && parent) { /* SWEEP-013: child control — engine-internal wndproc */
        (void)exstyle; (void)x; (void)y; (void)style;
        return mw_child_register(sc, parent, menu, win, 0, NULL);
    }
    g_create_param = param; /* SW-802..806: forward lpParam into CREATESTRUCT */
    mw_gui_store_window((int)w, (int)h); /* SWEEP-005: allocate paint surface */
    mw_gui_send_create(); /* SWEEP-006: synchronous WM_CREATE */
    MW_TRACE("CreateWindowExA(ex=0x%x style=0x%x cls=%p %dx%d@%d,%d) -> HWND %p",
             exstyle, style, cls, w, h, x, y, MW_GUI_HWND);
    return MW_GUI_HWND;
}"""
replace(cw_w, proto, "CreateWindowEx CBT-fire")

# ---- 2. destroy fire in DestroyWindow (after child branch, main path) ----
dw_old = """    MW_TRACE("DestroyWindow(hwnd=%p)", hwnd);"""
dw_new = """    mw_hook_fire_destroy(hwnd); /* SW-1203: HCBT_DESTROYWND pre-destroy */
    MW_TRACE("DestroyWindow(hwnd=%p)", hwnd);"""
replace(dw_old, dw_new, "DestroyWindow CBT-fire")

# ---- 3. activate fire in ShowWindow ----
sw_old = """__attribute__((ms_abi)) int mw_ShowWindow(void* hwnd, int cmd) {
    MW_TRACE("ShowWindow(hwnd=%p cmd=%d) -> TRUE", hwnd, cmd);
    return 1;
}"""
sw_new = """__attribute__((ms_abi)) int mw_ShowWindow(void* hwnd, int cmd) {
    mw_hook_fire_activate(hwnd); /* SW-1203: HCBT_ACTIVATE on first show */
    MW_TRACE("ShowWindow(hwnd=%p cmd=%d) -> TRUE", hwnd, cmd);
    return 1;
}"""
replace(sw_old, sw_new, "ShowWindow CBT-fire")

# ---- 4. hookfire section after UnhookWindowsHookEx ----
unhook = """    MW_TRACE("UnhookWindowsHookEx(%p) -> FALSE (bad handle)", hhk);
    mw_SetLastError(1400); /* ERROR_INVALID_HANDLE */
    return 0;
}"""
replace(unhook, unhook + "\n" + section("hookfire").rstrip("\n"), "hookfire-block")

# ---- 5. SW-1204: AppPolicyGetThreadInitializationType (worker-thread unblock) ----
impl2 = open(IMPL).read()
m = re.search(r"/\* ==== SECTION: apppolicy ==== \*/\n(.*?)\n/\* ==== SWEEP-035 IMPL END ==== \*/", impl2, re.S)
assert m, "apppolicy section missing"
replace('    ENTRY("KERNEL32.DLL", "GetCurrentProcessorNumber", mw_GetCurrentProcessorNumber),',
"""    ENTRY("KERNEL32.DLL", "GetCurrentProcessorNumber", mw_GetCurrentProcessorNumber),
    /* SW-1204 (SWEEP-035): winmergeu worker-thread init unblock */
    ENTRY("api-ms-win-appmodel-runtime-l1-1-2.dll", "AppPolicyGetThreadInitializationType", mw_AppPolicyGetThreadInitializationType),""",
"ENTRY-apppolicy")
src += "\n" + m.group(1).rstrip() + "\n"
print("  ok: apppolicy-section-append")

if fails:
    print("\n".join(fails))
    raise SystemExit(1)

with open(LOADER, "w") as f:
    f.write(src)
print("SWEEP-035 splice complete.")
