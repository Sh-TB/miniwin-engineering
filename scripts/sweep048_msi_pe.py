#!/usr/bin/env python3
"""SWEEP-048: extract PE files from FreeOffice MSI streams, identify by version resource."""
import olefile, os, struct

MSI = "freeoffice.msi"
OUT = "msi_pe"
os.makedirs(OUT, exist_ok=True)
ole = olefile.OleFileIO(MSI)

pe_files = []
for s in ole.listdir():
    name = "/".join(s)
    try:
        sz = ole.get_size(name)
    except Exception:
        continue
    if sz < 200000:  # main exes are MB-scale; keep threshold
        continue
    data = ole.openstream(name).read()
    if data[:2] != b"MZ":
        continue
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew + 0x100 > len(data) or data[e_lfanew:e_lfanew+4] != b"PE\x00\x00":
        continue
    fn = f"{OUT}/{s[-1]}.bin".replace("/", "_")
    with open(fn, "wb") as f:
        f.write(data)
    pe_files.append((fn, len(data)))

print(f"{len(pe_files)} PE streams >=200KB saved")
# dump version info via pefile
import pefile
for fn, sz in pe_files:
    try:
        pe = pefile.PE(fn, fast_load=True)
        pe.parse_data_directories([pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
        vi = ""
        if hasattr(pe, "FileInfo"):
            for fi in pe.FileInfo:
                for e in fi:
                    if e.Key == b"StringFileInfo":
                        for st in e.StringTable:
                            for k, v in st.entries.items():
                                vi += f"{k.decode()}={v.decode(errors='replace')} "
        mach = pe.FILE_HEADER.Machine
        print(f"{fn} size={sz} machine={hex(mach)} :: {vi[:220]}")
        pe.close()
    except Exception as ex:
        print(f"{fn} size={sz} PE-ERR {ex}")
