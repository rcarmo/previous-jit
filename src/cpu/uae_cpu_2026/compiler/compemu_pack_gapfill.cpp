/* Priority gap-fill handlers for PACK/UNPK helper-backed forms. */

/* PACK.L Dn,Dn */
void REGPARAM2 op_8140_0_comp_ff(uae_u32 opcode) /* PACK */
{
	uae_u32 srcreg = (opcode & 7);
	uae_u32 dstreg = (opcode >> 9) & 7;
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	int enc = scratchie++;
	mov_l_ri(enc, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	shll_l_ri(enc, 16);
	or_l_ri(enc, ((srcreg & 7) << 3) | (dstreg & 7));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	flush(1);
	call_helper((uintptr)jit_op_pack);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* PACK.L -(An),-(An) */
void REGPARAM2 op_8148_0_comp_ff(uae_u32 opcode) /* PACK */
{
	uae_u32 srcreg = (opcode & 7);
	uae_u32 dstreg = (opcode >> 9) & 7;
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	int enc = scratchie++;
	mov_l_ri(enc, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	shll_l_ri(enc, 16);
	or_l_ri(enc, (1 << 6) | ((srcreg & 7) << 3) | (dstreg & 7));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	flush(1);
	call_helper((uintptr)jit_op_pack);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* UNPK.L Dn,Dn */
void REGPARAM2 op_8180_0_comp_ff(uae_u32 opcode) /* UNPK */
{
	uae_u32 srcreg = (opcode & 7);
	uae_u32 dstreg = (opcode >> 9) & 7;
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	int enc = scratchie++;
	mov_l_ri(enc, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	shll_l_ri(enc, 16);
	or_l_ri(enc, ((srcreg & 7) << 3) | (dstreg & 7));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	flush(1);
	call_helper((uintptr)jit_op_unpk);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* UNPK.L -(An),-(An) */
void REGPARAM2 op_8188_0_comp_ff(uae_u32 opcode) /* UNPK */
{
	uae_u32 srcreg = (opcode & 7);
	uae_u32 dstreg = (opcode >> 9) & 7;
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	int enc = scratchie++;
	mov_l_ri(enc, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	shll_l_ri(enc, 16);
	or_l_ri(enc, (1 << 6) | ((srcreg & 7) << 3) | (dstreg & 7));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	flush(1);
	call_helper((uintptr)jit_op_unpk);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* PACK.L Dn,Dn */
void REGPARAM2 op_8140_0_comp_nf(uae_u32 opcode) /* PACK */
{
	uae_u32 srcreg = (opcode & 7);
	uae_u32 dstreg = (opcode >> 9) & 7;
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	int enc = scratchie++;
	mov_l_ri(enc, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	shll_l_ri(enc, 16);
	or_l_ri(enc, ((srcreg & 7) << 3) | (dstreg & 7));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	flush(1);
	call_helper((uintptr)jit_op_pack);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* PACK.L -(An),-(An) */
void REGPARAM2 op_8148_0_comp_nf(uae_u32 opcode) /* PACK */
{
	uae_u32 srcreg = (opcode & 7);
	uae_u32 dstreg = (opcode >> 9) & 7;
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	int enc = scratchie++;
	mov_l_ri(enc, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	shll_l_ri(enc, 16);
	or_l_ri(enc, (1 << 6) | ((srcreg & 7) << 3) | (dstreg & 7));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	flush(1);
	call_helper((uintptr)jit_op_pack);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* UNPK.L Dn,Dn */
void REGPARAM2 op_8180_0_comp_nf(uae_u32 opcode) /* UNPK */
{
	uae_u32 srcreg = (opcode & 7);
	uae_u32 dstreg = (opcode >> 9) & 7;
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	int enc = scratchie++;
	mov_l_ri(enc, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	shll_l_ri(enc, 16);
	or_l_ri(enc, ((srcreg & 7) << 3) | (dstreg & 7));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	flush(1);
	call_helper((uintptr)jit_op_unpk);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* UNPK.L -(An),-(An) */
void REGPARAM2 op_8188_0_comp_nf(uae_u32 opcode) /* UNPK */
{
	uae_u32 srcreg = (opcode & 7);
	uae_u32 dstreg = (opcode >> 9) & 7;
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	int enc = scratchie++;
	mov_l_ri(enc, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	shll_l_ri(enc, 16);
	or_l_ri(enc, (1 << 6) | ((srcreg & 7) << 3) | (dstreg & 7));
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, enc);
	flush(1);
	call_helper((uintptr)jit_op_unpk);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}
