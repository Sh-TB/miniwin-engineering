#!/usr/bin/env python3
# SWEEP-026: mark LANDED items in .agent/SWEEP50_WORKLIST.md + append evidence.
import re, io

WL = '.agent/SWEEP50_WORKLIST.md'
txt = open(WL, encoding='utf-8').read()

EVID = {
 'SW-1021': 'real TEXTMETRICW offsets (height/ascent/descent/ave chain) -> putty BUG-082 GS-cookie gate BROKEN: exit 9->0, MessageBoxA error dialog rendered (artifacts/sweep026/sweep/putty/run1, miniwin-results/putty.exe/screenshots/messagebox_1.bmp)',
 'SW-1023': 'real SetTimer elapse gate (monotonic due-time + idle pump yield): procmon64 pump-on 26.35M -> 998,347 SendMessageW/12s (26x cut), program still pumping at timeout',
 'SW-1038': 'GetUserNameExW routed to ExA identity store (batch diff)',
 'SW-1040': 'ConvertSidToStringSidA A-tail of the W builder (batch diff)',
 'SW-1041': 'GetSecurityInfo/SetSecurityInfo from session SID model (session SIDs lifted to file scope)',
 'SW-1042': 'CheckTokenMembership real EqualSid walk over token groups',
 'SW-1043': 'EqualSid/GetSidSubAuthority(Count) real address arithmetic',
 'SW-1044': 'IsValidSid wire-format sanity (revision/count/length checks)',
 'SW-1045': 'CreateWellKnownSid/IsWellKnownSid real byte construction (BUILTIN\\Administrators)',
 'SW-1046': 'GetAclInformation/GetAce real ACL header reads',
 'SW-1047': 'AddAccessAllowedAce append within allocation reserve + SetSecurityDescriptorControl real control-bit edit',
 'SW-1048': 'InitializeAcl/IsValidAcl header contract',
 'SW-1049': 'RegConnectRegistryW local-machine honest host',
 'SW-1051': 'RegQueryMultipleValuesW real multi-value read loop',
 'SW-1052': 'RegCreateKeyTransactedW (transaction ignored, create real)',
 'SW-1056': 'ExtractIconExA A-tail mirroring W mint',
 'SW-1058': 'CreateIconFromResource non-Ex route',
 'SW-1059': 'LookupIconIdFromDirectoryEx real ICONDIR parse',
 'SW-1060': 'DrawStateW honest v1 state-render',
 'SW-1062': 'SetDIBitsToDevice real DIB blit into DC surface (bottom-up/top-down + BITFIELDS); build fix: correct row-conversion helper (to_bgr24)',
 'SW-1063': 'PatBlt honors rop-contract modes (WHITENESS/BLACKNESS/DSTINVERT...) instead of brushing',
 'SW-1064': 'CreateHalftonePalette real halftone mint',
 'SW-1065': 'AnimatePalette/SetSystemPaletteUse',
 'SW-1066': 'GetSystemPaletteEntries',
 'SW-1069': 'GetTextExtentPointW W short form of Point32',
 'SW-1070': 'GetTextMetricsA honest 53-byte A-shape',
 'SW-1071': 'SetTextCharacterExtra/GetTextCharacterExtra spacing knobs (putty probe pair)',
 'SW-1090': 'trace ring cap default 1M lines VERIFIED: procmon64 pump-on api_trace.json exactly 1,000,002 lines (was 26M/2.4GB); MINWIN_TRACE_MAX_LINES/MINWIN_TRACE_UNLIMITED overrides',
 'SW-1091': 'root cause named: WM_TIMER served at pump spin rate (not elapse); killed by SW-1023 real elapse gate — 26x SendMessageW reduction measured',
 'SW-1095': 'SetEntriesInAclW TRUSTEE_IS_NAME wide-name form via session identity fill',
 'SW-1096': 'BuildExplicitAccessWithName A/W real struct fill',
 'SW-1097': 'GetNamedSecurityInfoW descriptor read path',
}

lines = txt.split('\n')
out, i, marked = [], 0, set()
while i < len(lines):
    ln = lines[i]
    m = re.match(r'^- \[ \] \*\*(SW-\d{4})\b', ln)
    if m and m.group(1) in EVID and m.group(1) not in marked:
        sw = m.group(1)
        ln = ln.replace('- [ ] **', '- [x] **', 1)
        out.append(ln)
        out.append('      -> LANDED SWEEP-026: ' + EVID[sw])
        marked.add(sw)
    else:
        out.append(ln)
    i += 1

txt = '\n'.join(out)

# Section header refresh
txt = txt.replace(
 '## SW-1021..SW-1100 — SWEEP-025 continuation queue (80 items, (triple-parity continuation) standing order: 26 landed + 80 queued = 106 this round)',
 '## SW-1021..SW-1100 — SWEEP-025 continuation queue — 32 LANDED in SWEEP-026 (continue elephant-cheetah with 50 tasks) one batch (see -> LANDED SWEEP-026 marks); 48 remain queued below')

open(WL, 'w', encoding='utf-8').write(txt)
print(f"marked {len(marked)} items: {sorted(marked)}")
missing = set(EVID) - marked
if missing: print("NOT FOUND IN LIST:", missing)
