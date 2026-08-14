import sys
lines = open('/home/z/my-project/minwin/src/loader.c', 'rb').read().decode('utf-8', errors='replace')

# Find the block to replace
start = end = None
for i, line in enumerate(lines):
    stripped = line.strip()
    if 'direct_main = 0' in stripped and 'MINWIN_MAIN_RVA' in stripped:
        start = i
        break

if start is None:
    print('ERROR: block not found')
    sys.exit(1)

# Find end of block (matching brace at indentation level)
count = 0
for j in range(start, len(lines)):
    s = lines[j].strip()
    if s == '{':
        count += 1
    elif s == '}':
        count -= 1
    if count == 0 and j > start:
        end = j + 1
        break

print(f'Block: lines {start}-{end}')

old = lines[:start] + lines[end:]
new = '''    /* For UPX and similar apps, support direct main call via MINWIN_MAIN_RVA env var */
    const char* main_rva_str = getenv("MINWIN_MAIN_RVA");
    if (main_rva_str) {
        unsigned long rva = strtoul(main_rva_str, NULL, 0);
        if (rva > 0) {
            MW_TRACE("Calling main directly at RVA 0x%lx", rva);
            typedef int (__attribute__((ms_abi))) *main_t)(int, char**, char**);
            main_t mf = (main_t)(g_image_base + rva);
            int result = mf(g_argc, g_argv, NULL);
            printf("\\n---\\n[MiniWin] main returned %d\\n", result);
            fflush(stdout);
            if (g_trace_log) {
                fprintf(g_trace_log, "\\n  ]\\n}\\n");
                fclose(g_trace_log);
            }
            return result;
        }
    }

    /* Default: execute through CRT entry point */
    typedef void (ep_func_t)(void);
    ep_func_t ep = (ep_func_t)(g_image_base + g_entry_point);
    __asm__ __volatile__ (
        "call *%0\n"
        :
        "r"(ep)
        : "rcx", "rdx", "r8", "r9", "10", "11", "memory"
    );
    printf("\\n---\\n[MiniWin] Execution finished\\n");
    fflush(stdout);'''

old = lines[:start] + lines[end:]
with open('/home/z/my-project/minwin/src/loader.c', 'w', encoding='utf-8') as f:
    f.write('\n'.join(old))
print('OK')
