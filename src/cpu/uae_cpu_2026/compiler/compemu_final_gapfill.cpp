/* Final fallback opcode gap-fill handlers. */

static void comp_simple_final_helper(uae_u32 opcode, uintptr helper)
{
	m68k_pc_offset += 2;
{	uae_u8 scratchie = S1;
	int enc = scratchie++;
	mov_l_ri(enc, opcode & 0xffff);
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	sync_m68k_pc();
	flush(1);
	call_helper(helper);
}
}

static void comp_trap_gapfill(uae_u32 opcode) { comp_simple_final_helper(opcode, (uintptr)jit_op_trap); }
static void comp_bkpt_gapfill(uae_u32 opcode) { comp_simple_final_helper(opcode, (uintptr)jit_op_bkpt); }
static void comp_rtm_gapfill(uae_u32 opcode) { comp_simple_final_helper(opcode, (uintptr)jit_op_rtm); }
static void comp_cas2_gapfill(uae_u32 opcode)
{
	m68k_pc_offset += 2;
{	uae_u8 scratchie = S1;
	int enc = scratchie++;
	int extra = scratchie++;
	mov_l_ri(enc, opcode & 0xffff);
	mov_l_ri(extra, comp_get_ilong((m68k_pc_offset += 4) - 4));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	mov_l_mr((uintptr)&regs.scratchregs[0], extra);
	flush(1);
	call_helper((uintptr)jit_op_cas2);
}
	if (m68k_pc_offset > SYNC_PC_OFFSET)
		sync_m68k_pc();
}
static void comp_fsave_gapfill(uae_u32 opcode) { comp_simple_final_helper(opcode, (uintptr)jit_op_fsave); }
static void comp_ftrapcc_gapfill(uae_u32 opcode)
{
	m68k_pc_offset += 2;
	if ((opcode & 0xffff) == 0xf27a)
		(void)comp_get_iword((m68k_pc_offset += 2) - 2);
	else if ((opcode & 0xffff) == 0xf27b)
		(void)comp_get_ilong((m68k_pc_offset += 4) - 4);
{	uae_u8 scratchie = S1;
	int enc = scratchie++;
	mov_l_ri(enc, opcode & 0xffff);
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	sync_m68k_pc();
	flush(1);
	call_helper((uintptr)jit_op_ftrapcc);
}
}
static void comp_fdbcc_gapfill(uae_u32 opcode)
{
	m68k_pc_offset += 2;
	(void)comp_get_iword((m68k_pc_offset += 2) - 2); /* FDBcc extension word */
	(void)comp_get_iword((m68k_pc_offset += 2) - 2); /* displacement */
{	uae_u8 scratchie = S1;
	int enc = scratchie++;
	mov_l_ri(enc, opcode & 0xffff);
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	sync_m68k_pc();
	flush(1);
	call_helper((uintptr)jit_op_fdbcc);
}
}
static void comp_cache_line_gapfill(uae_u32 opcode) { comp_simple_final_helper(opcode, (uintptr)jit_op_cache_line); }
static void comp_mmu_final_gapfill(uae_u32 opcode) { comp_simple_final_helper(opcode, (uintptr)jit_op_mmu_final); }

static void comp_movep_gapfill(uae_u32 opcode, int op_kind)
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 areg = (opcode >> 8) & 7;
#else
	uae_u32 areg = opcode & 7;
#endif
	m68k_pc_offset += 2;
{	uae_u8 scratchie = S1;
	int enc = scratchie++;
	int addr = scratchie++;
	mov_l_ri(enc, ((op_kind & 3) << 16) | (opcode & 0xffff));
	mov_l_rr(addr, areg + 8);
	lea_l_brr(addr, addr, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset += 2) - 2));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	mov_l_mr((uintptr)&regs.scratchregs[0], addr);
	flush(1);
	call_helper((uintptr)jit_op_movep);
}
	if (m68k_pc_offset > SYNC_PC_OFFSET)
		sync_m68k_pc();
}

#define FINAL_SIMPLE(OPHEX, NAME) \
void REGPARAM2 op_##OPHEX##_0_comp_ff(uae_u32 opcode) { comp_##NAME##_gapfill(opcode); } \
void REGPARAM2 op_##OPHEX##_0_comp_nf(uae_u32 opcode) { comp_##NAME##_gapfill(opcode); }

/* MOVEP */
void REGPARAM2 op_108_0_comp_ff(uae_u32 opcode) { comp_movep_gapfill(opcode, 0); }
void REGPARAM2 op_108_0_comp_nf(uae_u32 opcode) { comp_movep_gapfill(opcode, 0); }
void REGPARAM2 op_148_0_comp_ff(uae_u32 opcode) { comp_movep_gapfill(opcode, 1); }
void REGPARAM2 op_148_0_comp_nf(uae_u32 opcode) { comp_movep_gapfill(opcode, 1); }
void REGPARAM2 op_188_0_comp_ff(uae_u32 opcode) { comp_movep_gapfill(opcode, 2); }
void REGPARAM2 op_188_0_comp_nf(uae_u32 opcode) { comp_movep_gapfill(opcode, 2); }
void REGPARAM2 op_1c8_0_comp_ff(uae_u32 opcode) { comp_movep_gapfill(opcode, 3); }
void REGPARAM2 op_1c8_0_comp_nf(uae_u32 opcode) { comp_movep_gapfill(opcode, 3); }

/* RTM/BKPT/TRAP */
FINAL_SIMPLE(6c0, rtm)
FINAL_SIMPLE(6c8, rtm)
FINAL_SIMPLE(4848, bkpt)
FINAL_SIMPLE(4e40, trap)

/* CAS2 */
FINAL_SIMPLE(cfc, cas2)
FINAL_SIMPLE(efc, cas2)

/* Remaining FPU flow/state instructions */
FINAL_SIMPLE(f248, fdbcc)
FINAL_SIMPLE(f27a, ftrapcc)
FINAL_SIMPLE(f27b, ftrapcc)
FINAL_SIMPLE(f27c, ftrapcc)
FINAL_SIMPLE(f310, fsave)
FINAL_SIMPLE(f320, fsave)
FINAL_SIMPLE(f328, fsave)
FINAL_SIMPLE(f330, fsave)
FINAL_SIMPLE(f338, fsave)
FINAL_SIMPLE(f339, fsave)

/* Remaining cache line/page instructions */
FINAL_SIMPLE(f408, cache_line)
FINAL_SIMPLE(f410, cache_line)
FINAL_SIMPLE(f428, cache_line)
FINAL_SIMPLE(f430, cache_line)

/* Remaining MMU opcodes */
FINAL_SIMPLE(f500, mmu_final)
FINAL_SIMPLE(f508, mmu_final)
FINAL_SIMPLE(f510, mmu_final)
FINAL_SIMPLE(f518, mmu_final)
FINAL_SIMPLE(f548, mmu_final)
FINAL_SIMPLE(f568, mmu_final)

#undef FINAL_SIMPLE
