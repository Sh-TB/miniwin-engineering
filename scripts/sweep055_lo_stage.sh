#!/bin/bash
# SWEEP-055 (SW-1273 train 2): stage the LibreOffice soffice guest from the
# fresh 19,427-file CAB extract (same recipe as SWEEP-051: soffice.bin +
# sal3 + mergedlo + the DLL import closure + VC runtime + renames), then
# verify the two byte-pins from the WORKLOG.
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
SRC=artifacts/sweep055/lo_extract
DST=artifacts/sweep055/guests/soffice
mkdir -p "$DST"

# every DLL + soffice.bin + program data at the extract root
cp "$SRC"/*.dll "$DST"/ 2>/dev/null
cp "$SRC"/soffice.bin "$DST"/
cp "$SRC"/*.ini "$DST"/ 2>/dev/null
cp "$SRC"/*.xcs "$SRC"/*.xcu "$DST"/ 2>/dev/null
# MSI underscore-vs-hyphen renames (SWEEP-051 recipe)
[ -f "$SRC/orcus_parser.dll" ] && cp "$SRC/orcus_parser.dll" "$DST/orcus-parser.dll"
[ -f "$SRC/libcrypto_3.dll" ] && cp "$SRC/libcrypto_3.dll" "$DST/libcrypto-3.dll"
[ -f "$SRC/libssl_3.dll" ] && cp "$SRC/libssl_3.dll" "$DST/libssl-3.dll"
[ -f "$SRC/libxmlsec_mscng.dll" ] && cp "$SRC/libxmlsec_mscng.dll" "$DST/libxmlsec-mscng.dll"

echo "staged files: $(ls "$DST" | wc -l)"
python3 - <<'EOF'
import hashlib
pins = {"artifacts/sweep055/guests/soffice/soffice.bin": "cb08cffd",
        "artifacts/sweep055/guests/soffice/mergedlo.dll": "017d0c21"}
ok = True
for f, want in pins.items():
    h = hashlib.sha256(open(f, "rb").read()).hexdigest()[:8]
    print(f"{f}: {h} pin {want} MATCH={h == want}")
    ok &= (h == want)
print("PINS", "OK" if ok else "MISMATCH")
EOF
