# Hypotheses — current registry

## H-026-1 (RESOLVED — verified): g_cap_rip operand defect is the sole cause of the current abort(134)
Evidence: probe (F8), trace F2-F4, single-operand diff vs exp_next2_backup. No competing hypothesis survives.

## H-001 (historical, REJECTED as stated): "landing-pad NV registers not restored"
Mechanism analysis: all 16 registers ARE restored mechanically (dispatcher copy L1350-1367; stub reload L1722-1740). The historical landing-pad crash is explained by the RSP window (H-002 lineage): landing pad ran at establisher RSP instead of body RSP (establisher − 0x68), so `[rsp+0x30]` read caller-frame garbage → P=NULL → fault at 0x401561. Note: state.md's landing-pad listing was also a mis-transcription (`mov rbx,rdi` — actual `mov %rdx,%rbx`).

## H-002 (historical, CONFIRMED-lineage): wrong RSP window / stack contents at landing pad
Superseded in phrasing by H-035-1 (below).

## H-035-1 (OPEN — to verify live in EXP-037): EXP-035 post_prologue_rsp = target_frame − alloc_total is correct for the landing-pad function
Static support: RF[76] prolog = 0x68 (ALLOC_SMALL 88 + RBX + R12), landing pad and normal call site both use `lea 0x30(%rsp)` ⇒ landing pad must run at body RSP. alloc_total algorithm agrees (0x68) for this function. Open: (a) live confirmation; (b) alloc_total is wrong for ALLOC_LARGE functions (N-1) — irrelevant for this target but generalization-relevant.

## H-038-1 (OPEN — EXP-038): adopting RtlUnwindEx's ignored 5th argument (OriginalContext at [RSP+0x28]) yields the correct landing-pad state including the RDX selector
Basis: real ABI mapping (forensics §3.5); MinGW personality prepares the context. Predicted: fixes selector-dependent landing pads (`mov %rdx,%rbx; sub $1,%rbx; jne`) without re-deriving state.

## H-VEH-1 (OPEN — low priority): UPX's AddVectoredExceptionHandler registration does not persist to dispatch time (trace shows 0 handlers)
Worth one diagnostic after the acceptance path works; not on the critical path.

## H-SEG-1 (REJECTED, historical): "crash is an ASLR problem"
PE maps at preferred base 0x400000 (trace); relocation absent but unused; crash addresses explained by §2/§3.
