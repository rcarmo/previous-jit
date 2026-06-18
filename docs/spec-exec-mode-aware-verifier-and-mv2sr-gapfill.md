# ONE-SHOT SPEC: exec-mode-aware verifier + first privileged-move gap-fill (MV2SR.W)

Supersedes the unconditional Fix B+A in docs/oracle-fix-implement-ready.md
(reverted in e82b263 — unsound on exact-exec blocks). Anchors HEAD-accurate at
write time (re-verify before editing). File: compemu_support_arm.cpp unless noted.

## Why exec-mode-aware

The verify "JIT run" is native (nzcv) ONLY when the block fully compiled. If any
op falls back (JIT_EXACT_EXEC) or optlev==0, the block ends on the interpreter and
`actual` is cznv (same layout as `expected`). The robust per-block signal is the
compile loop's `forced_interpreter_barrier` (set at EVERY exact-exec site:
6318/6385/6486/6701/6818/6831) plus `optlev==0` (whole-block exec_nostats at 6226).

Ordering subtlety RESOLVED: run compile_block FIRST (it only GENERATES code; it
does NOT consume the live entry regflags — the :6256 reload reads them at runtime
during pushall), read the signal, THEN apply Fix B only for native blocks, THEN
pushall. This avoids the e82b263 corruption (Fix B applied before knowing mode).

## PART 1 — exec-mode-aware verifier

### Edit 1a (global) — after line 637 (`...compile_pc = 0xffffffffu;`)
    static bool jit_block_verify_actual_exact_exec = false;

### Edit 1b (compile_block hook) — after line 7092 (`set_dhtu(bi, bi->handler);`)
Insert right after the closing brace of the
`if (forced_interpreter_barrier || !was_comp || ...) { set_dhtu(...); }` block:

    if (jit_block_verify_compile_active && block_m68k_pc == jit_block_verify_compile_pc)
        jit_block_verify_actual_exact_exec = (optlev == 0) || forced_interpreter_barrier;

(`optlev` and `forced_interpreter_barrier` are both in scope at 7092.)

### Edit 1c (jit_block_verify_run) — restructure JIT-run section (lines 790-806)
Reset the signal before compile_block; compute actual_is_nzcv AFTER; apply Fix B
only if native; pass the bool to the compare:

    jit_block_verify_snapshot_restore(&jit_block_verify_entry_state);
    regs.spcflags = 0;
    InterruptFlags = 0;
    jit_block_verify_actual_exact_exec = true;   /* safe default: cznv until proven native */
    jit_block_verify_compile_active = true;
    jit_block_verify_compile_pc = block_pc;
    compile_block(pc_hist, blocklen, total_cycles);   /* sets the signal via Edit 1b */
    jit_block_verify_compile_active = false;
    jit_block_verify_compile_pc = 0xffffffffu;

    const bool actual_is_nzcv = !jit_block_verify_actual_exact_exec;
    if (actual_is_nzcv) {
        /* Fix B (native blocks only): entry snapshot is interpreter cznv(bit14);
           the compiled block's :6256 reload reads word0 as nzcv(bit30). Convert. */
        uae_u32 cz = regflags.nzcv, xw = regflags.x;
        uae_u32 N=(cz>>15)&1, Z=(cz>>14)&1, C=(cz>>8)&1, V=(cz>>0)&1, X=(xw>>8)&1;
        regflags.nzcv = (N<<31)|(Z<<30)|(C<<29)|(V<<28);
        regflags.x    = (X<<29);
    }

    countdown = -1;
    jit_block_verify_reentrant = true;
    ((jit_compiled_handler)pushall_call_handler)();
    jit_block_verify_reentrant = false;

    if (jit_block_verify_snapshot_capture(&native)) {
        jit_block_verify_compare(&interp, &native, block_pc, blocklen, actual_is_nzcv);
        jit_block_verify_snapshot_free(&native);
    }

### Edit 1d (Fix A helpers) — insert above line 698 (jit_block_verify_compare)
    static inline uae_u16 ccr_from_cznv(uae_u32 cz, uae_u32 xw){return (uae_u16)((((xw>>8)&1)<<4)|(((cz>>15)&1)<<3)|(((cz>>14)&1)<<2)|(((cz>>0)&1)<<1)|((cz>>8)&1));}
    static inline uae_u16 ccr_from_nzcv(uae_u32 nz, uae_u32 xw){return (uae_u16)((((xw>>29)&1)<<4)|(((nz>>31)&1)<<3)|(((nz>>30)&1)<<2)|(((nz>>28)&1)<<1)|((nz>>29)&1));}

### Edit 1e (compare signature + Fix A) — line 698 signature, line 706 body
Signature (698): add a trailing `, bool actual_is_nzcv` parameter.
Body (706): replace `bool flags_mismatch = expected->flags.nzcv != ... ;` with:

    uae_u16 exp_ccr = ccr_from_cznv(expected->flags.nzcv, expected->flags.x);
    uae_u16 act_ccr = actual_is_nzcv ? ccr_from_nzcv(actual->flags.nzcv, actual->flags.x)
                                     : ccr_from_cznv(actual->flags.nzcv, actual->flags.x);
    bool flags_mismatch = exp_ccr != act_ccr;

Regs/ctrl/mem compares unchanged: native blocks get correct entry via Fix B;
exact-exec blocks share the interpreter path so already match.

### Verify (Part 1)
tools/fg-verify-window.sh 0x0409ec00-0x0409ed00 (log level 5; window ~10min in).
EXPECT: 0409ecbe/ec70 (native MVSR2) -> mismatch=0 via Fix B; 0409ecda/ec6c/ecd4
+ 0409ec68 (exact-exec 46fc/46c6/48e7) -> mismatch=0 via cznv-vs-cznv (no Fix B,
no false ccr_from_nzcv decode). Window total mismatch=1 -> 0. COMMIT Part 1.

## PART 2 — first gap-fill: MV2SR.W (46fc + 46c6) compiles

Rationale (docs/gapfill-priority-privileged-moves.md): freq 10 (46fc 8 + 46c6 2);
handler op_fullsr_mv2sr_w_comp_ff already exists; it is the window's contaminator.

### Edit 2 — env-gate the force-barrier at line 1210 (default: compile)
Replace:
    if (jit_allow_ram_dispatch_env() && table68k[op].mnemo == i_MV2SR && table68k[op].size == sz_word)
        return true;
with:
    if (jit_allow_ram_dispatch_env() && table68k[op].mnemo == i_MV2SR && table68k[op].size == sz_word
        && jit_keep_mv2sr_exact_env())   /* default false -> compile via op_fullsr_mv2sr_w_comp_ff */
        return true;
Helper (near other env helpers):
    static inline bool jit_keep_mv2sr_exact_env(void){ static int v=-1; if(v<0){const char*e=getenv("B2_JIT_KEEP_MV2SR_EXACT"); v=(e&&*e&&strcmp(e,"0"))?1:0;} return v!=0; }

### Verify (Part 2) — BOTH oracle AND boot
1. fg-verify-window 0x0409ec00-0x0409ed00: 46fc/46c6 blocks now native -> mismatch=0.
   If any mismatch=1: B2_JIT_KEEP_MV2SR_EXACT=1 (restore old behavior) + fix handler.
2. Boot-progress (THE risk from line-1210's comment): pure JIT must still pass the
   0x04387 SCSI poll and NOT vector early into the kernel exception handler
   (the NeXT RTC/SCSI misbranch). If boot regresses, keep exact + fix handler first.
3. Re-sweep 0x04090000-0x040b0000: EXPECT ~10 fewer JIT_EXACT_EXEC (46fc+46c6),
   ~4 fewer contaminated verdicts, zero new mismatch=1. COMMIT Part 2.

## Expected delta
- Part 1: oracle trustworthy on mixed blocks (0 false mismatch in window).
- Part 2: -~10 exact-exec fallbacks/window-pass + de-contaminate ~4 verdicts.
  MOVEM 48e7 (~2, MMU-restart) is the next, lower-priority gap-fill.
