#!/usr/bin/env python3
# probe: why does getFunctionContaining miss FUN_100001bc0?
import pyghidra

GHIDRA = "/home/z/my-project/tools/ghidra_12.2_PUBLIC"
pyghidra.start()
with pyghidra.open_program(
        "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep061/guests/sol/sol.exe",
        project_location="/home/z/my-project/tmp/ghidraproj",
        project_name="sweep063", analyze=False) as flat:
    prog = flat.getCurrentProgram()
    fm = prog.getFunctionManager()
    base = prog.getImageBase()
    print("image base:", base, "space:", base.getAddressSpace().getName())
    af = prog.getAddressFactory()
    print("factory spaces:", [s.getName() for s in af.getAllAddressSpaces()])
    sp = af.getDefaultAddressSpace()
    for v in (0x100001bc0, 0x100007cf0):
        try:
            a = sp.getAddress(v)
            fn = fm.getFunctionContaining(a)
            fn2 = fm.getFunctionAt(a)
            print(hex(v), "addr", a, "containing:", fn, "at:", fn2)
        except Exception as e:
            print(hex(v), "EXC", type(e).__name__, e)
    # direct entry lookup from function manager
    for fn in list(fm.getFunctions(True))[:3]:
        print("sample fn:", fn.getName(), fn.getEntryPoint(), "space",
              fn.getEntryPoint().getAddressSpace().getName())
