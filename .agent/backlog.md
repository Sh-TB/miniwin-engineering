# Engineering Backlog

## CRITICAL
- [ ] M8: Fix RtlUnwindEx context restoration (nonvolatile registers)
  - Evidence: crash at 0x401561, RAX=0 after landing pad reached
  - Approach: Walk frames in RtlUnwindEx before longjmp

## HIGH
- [ ] M9: Verify UPX --version produces correct output
- [ ] Fix format warnings (-Wformat for %lx with uint32_t)
- [ ] Fix realloc use-after-free warning in MW_TRACE

## MEDIUM
- [ ] Implement GCC LSDA interpretation (__C_specific_handler equivalent)
- [ ] Exception object cleanup (_Unwind_DeleteException)
- [ ] Real heap with free-list allocator

## LOW
- [ ] Module split (loader.c → multiple files)
- [ ] File I/O implementation (CreateFileA → open())
- [ ] Thread support (CreateThread → pthread_create)
- [ ] DLL loading (LoadLibrary / GetProcAddress)

## FUTURE
- [ ] GUI support
- [ ] Stage 2 target: simple C console app
- [ ] Stage 3 target: CLI tools (dir, echo, type)