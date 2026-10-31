#!/usr/bin/env python3
"""sweep111_seed.py -- SW-1546 registry-seed probe (issue #26 discriminating
experiment, continued from SWEEP-110 round 1/2).

Question: the installed WinRAR 7.12 GUI decodes all 21 toolbar PNG glyphs but
never sends TB_ADDBUTTONSW -- it aborts after 'HKCU\\Software\\WinRAR\\General\\
Toolbar\\Layout' / '\\Buttons' both miss (ERROR_PATH_NOT_FOUND), then parks in
the WM_TIMER id=100 loop. On a real Windows box the installer/first-run writes
the default button set. The guest binary strings give the exact value-name
model: short ids {add, exto, test, view, del, find, prn, wiz, cvt, info, exit,
rep, extr, vir, cmt, prot, lock, SFX, rpt, bnc} x templates
'f.%s' / 'f.%s.sep' / 'a.%s' (+ 'Band%d_%d' for Layout).

Probe: seed Buttons+Layout with f.<id>=1 (REG_DWORD) for every id, re-run, and
diff the toolbar message flow (TB_ADDBUTTONSW 0x444 / TB_ADDSTRING 0x41C).

Usage: sweep111_seed.py <snapshot_in> <snapshot_out>
"""
import sys

sys.path.insert(0, "scripts")
import regseed_extend as R  # noqa: E402

BTN_IDS = [
    "add", "exto", "test", "view", "del", "find", "prn", "wiz", "cvt",
    "info", "exit", "rep", "extr", "vir", "cmt", "prot", "lock", "SFX",
    "rpt", "bnc",
]

BTN_KEY = "HKCU\\Software\\WinRAR\\General\\Toolbar\\Buttons"
LAY_KEY = "HKCU\\Software\\WinRAR\\General\\Toolbar\\Layout"


def main(src, dst):
    slots = R.parse(src)
    live_before = sum(1 for s in slots if s)
    vals = {f"f.{b}": R.val_dw(1) for b in BTN_IDS}
    # a.<id>=1 as well: 'a' may be the archive-toolbar mirror of the flag set
    avals = dict(vals)
    avals.update({f"a.{b}": R.val_dw(1) for b in BTN_IDS})
    # SW-1554: real first-run values (any.run sandbox traces of real
    # WinRAR.exe runs, cross-checked with the guest disassembly cap 0x64):
    #   Layout\Band76_0..3 = 100 (the 0x14586d normalize loop caps at 100
    #   and the 0x1458c9 'first slot == 100' shortcut takes the completed
    #   layout branch; the old all-miss defaults (1,2,3 + 12) never reached
    #   the button-add phase)
    #   Buttons\View* / LargeButtons / ButtonsText = the size-flag family
    #   named by the guest binary strings (read through 0x14780c).
    b76 = {f"Band76_{i}": R.val_dw(100) for i in range(5)}
    avals.update({
        "Lock": R.val_dw(0),
        "ViewMain": R.val_dw(1),
        "ViewSmall": R.val_dw(0),
        "ViewAddress": R.val_dw(1),
        "LargeButtons": R.val_dw(0),
        "ButtonsText": R.val_dw(0),
    })
    R.upsert(slots, BTN_KEY, avals)
    R.upsert(slots, LAY_KEY, b76)
    blob = R.serialize(slots)
    live_after = sum(1 for s in slots if s)
    with open(dst, "wb") as f:
        f.write(blob)
    print(f"seeded {dst}: live keys {live_before} -> {live_after} "
          f"({len(avals)} values on Buttons, {len(b76)} Band76_x on Layout)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1], sys.argv[2])
