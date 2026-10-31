#!/bin/bash
# SWEEP-070 wave-2: corrected URLs for the 7 misses + SuperTux (famous GitHub game)
set -u
cd /home/z/my-project/workspace/miniwin-engineering || exit 1
DL=artifacts/sweep070/dl
EV=evidence/sweep070

fetch() {
    IFS='|' read -r n u <<< "$1"
    local out="$DL/$n.bin" code size magic kind
    code=$(curl -L --max-time 240 --retry 1 -sS -o "$out" -w '%{http_code}' "$u" 2>/dev/null) || code="FAIL"
    size=$(stat -c%s "$out" 2>/dev/null || echo 0)
    magic=$(head -c2 "$out" 2>/dev/null)
    if [ "$magic" = "MZ" ]; then kind="PE"
    elif [ "$magic" = "PK" ]; then kind="ZIP"
    else kind="BAD"
    fi
    [ "$size" -lt 10000 ] && kind="BAD"
    echo -e "$n\t$code\t$size\t$kind" >> "$EV/download_wave2.tsv"
}
export -f fetch; export DL EV

echo -e "name\thttp\tbytes\tkind" > "$EV/download_wave2.tsv"
cat << 'EOF' | xargs -P 6 -I{} bash -c 'fetch "{}"'
openttd2|https://github.com/OpenTTD/OpenTTD/releases/download/14.1/openttd-14.1-windows-win64.zip
sumatrapdf2|https://github.com/sumatrapdfreader/sumatrapdf/releases/download/v3.5.2/SumatraPDF-3.5.2-64.zip
autohotkey2|https://www.autohotkey.com/download/1.1/AutoHotkey_1.1.37.01.zip
irfanview2|https://www.irfanview.net/files/iview468_x64_setup.exe
treesize2|https://downloads.jam-software.de/treesize/Free/TreeSizeFree-Setup.exe
processhacker2|https://github.com/ProcessHacker/processhacker/releases/download/v2.39/processhacker-2.39.0-setup.exe
supertux|https://github.com/SuperTux/supertux/releases/download/v0.6.3/supertux-0.6.3-win64.zip
EOF
cat "$EV/download_wave2.tsv"

echo "=== UNZIP phase ==="
cd "$DL"
for z in steamcmd sharex ripgrep bat nssm stockfish supertux openttd2 sumatrapdf2 autohotkey2; do
    [ -f "$z.bin" ] && head -c2 "$z.bin" | grep -q PK && mkdir -p "x_$z" && unzip -oq "$z.bin" -d "x_$z" 2>/dev/null && echo "$z unzipped: $(find "x_$z" -name "*.exe" | wc -l) exes"
done
echo "=== total exes staged:"; find x_* -name "*.exe" 2>/dev/null | wc -l
