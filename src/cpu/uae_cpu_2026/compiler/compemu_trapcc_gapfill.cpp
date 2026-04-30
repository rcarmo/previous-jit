/* Priority gap-fill handlers for TRAPcc helper-backed forms. */

static void comp_trapcc_gapfill(int cc, int ext_bytes)
{
	m68k_pc_offset += 2;
	if (ext_bytes == 2)
		(void)comp_get_iword((m68k_pc_offset += 2) - 2);
	else if (ext_bytes == 4)
		(void)comp_get_ilong((m68k_pc_offset += 4) - 4);
{	uae_u8 scratchie = S1;
	int enc = scratchie++;
	mov_l_ri(enc, cc & 15);
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	flush(1);
	call_helper((uintptr)jit_op_trapcc);
}
	if (m68k_pc_offset > SYNC_PC_OFFSET)
		sync_m68k_pc();
}

#define TRAPCC_GAP(CCHEX, CCVAL) \
void REGPARAM2 op_##CCHEX##fa_0_comp_ff(uae_u32 opcode) { comp_trapcc_gapfill(CCVAL, 2); } \
void REGPARAM2 op_##CCHEX##fb_0_comp_ff(uae_u32 opcode) { comp_trapcc_gapfill(CCVAL, 4); } \
void REGPARAM2 op_##CCHEX##fc_0_comp_ff(uae_u32 opcode) { comp_trapcc_gapfill(CCVAL, 0); } \
void REGPARAM2 op_##CCHEX##fa_0_comp_nf(uae_u32 opcode) { comp_trapcc_gapfill(CCVAL, 2); } \
void REGPARAM2 op_##CCHEX##fb_0_comp_nf(uae_u32 opcode) { comp_trapcc_gapfill(CCVAL, 4); } \
void REGPARAM2 op_##CCHEX##fc_0_comp_nf(uae_u32 opcode) { comp_trapcc_gapfill(CCVAL, 0); }

TRAPCC_GAP(50, 0)
TRAPCC_GAP(51, 1)
TRAPCC_GAP(52, 2)
TRAPCC_GAP(53, 3)
TRAPCC_GAP(54, 4)
TRAPCC_GAP(55, 5)
TRAPCC_GAP(56, 6)
TRAPCC_GAP(57, 7)
TRAPCC_GAP(58, 8)
TRAPCC_GAP(59, 9)
TRAPCC_GAP(5a, 10)
TRAPCC_GAP(5b, 11)
TRAPCC_GAP(5c, 12)
TRAPCC_GAP(5d, 13)
TRAPCC_GAP(5e, 14)
TRAPCC_GAP(5f, 15)

#undef TRAPCC_GAP
