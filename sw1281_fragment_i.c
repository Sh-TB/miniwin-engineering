
/* ---------------- shim installation + pump-side glue ---------------- */
static void mw32_gui_install_shims(void) {
    /* g_main_class_proc32 is captured in mw_gui_store_wndproc (edit in
     * place); nothing else to do — mw_dispatch_proc() branches on it. */
    MW_TRACE("[PE32] GUI shims armed (x86 wndproc re-entry via cpu32_call)");
}

/* DialogBox-family dlgproc + timer/enum callbacks: any host site that
 * would call a guest fn pointer natively goes through this generic
 * dispatcher when the x86 CPU is live. Host APIs receive the guest EIP
 * in the same field they'd receive an ms_abi pointer; the shim checks
 * the mode flag. */
__attribute__((ms_abi)) static long long mw32_generic_callback_shim(void* a0, uint32_t a1, uintptr_t a2, long long a3) {
    /* a0 = guest EIP stored by the caller-side wrapper */
    uint32_t eip = (uint32_t)(uintptr_t)a0;
    if (!eip || !g_cpu32_active) return 0;
    uint32_t args[3] = { a1, (uint32_t)a2, (uint32_t)a3 };
    uint32_t r = cpu32_call(eip, 3, args);
    return (long long)(int32_t)r;
}
