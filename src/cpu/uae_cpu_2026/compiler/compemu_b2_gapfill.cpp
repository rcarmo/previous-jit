/* Gap-fill handlers for opcodes covered by BasiliskII compstbl_arm.cpp but not present in the generated compemu.cpp shipped in src/Unix. */

/* Scc.B Dn */
void REGPARAM2 op_58c0_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int src = srcreg;
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	if(srcreg != val)
		mov_b_rr(srcreg, val);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (An) */
void REGPARAM2 op_58d0_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = dodgy ? scratchie++ : srcreg + 8;
	if (dodgy)
		mov_l_rr(srca, srcreg + 8);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (An)+ */
void REGPARAM2 op_58d8_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_rr(srca, srcreg + 8);
	lea_l_brr(srcreg + 8,srcreg + 8, areg_byteinc[srcreg]);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B -(An) */
void REGPARAM2 op_58e0_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{{	int srca = dodgy ? scratchie++ : srcreg + 8;
	lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-areg_byteinc[srcreg]);
	if (dodgy)
		mov_l_rr(srca, 8 + srcreg);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (d16,An) */
void REGPARAM2 op_58e8_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_rr(srca, 8 + srcreg);
	lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (d8,An,Xn) */
void REGPARAM2 op_58f0_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (xxx).W */
void REGPARAM2 op_58f8_0_comp_ff(uae_u32 opcode) /* Scc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (xxx).L */
void REGPARAM2 op_58f9_0_comp_ff(uae_u32 opcode) /* Scc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4)); /* absl */
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* DBcc.W Dn,#<data>.W */
void REGPARAM2 op_58c8_0_comp_ff(uae_u32 opcode) /* DBcc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int src = srcreg;
{	int offs = scratchie++;
	mov_l_ri(offs, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	sign_extend_16_rr(offs,offs);
	sub_l_ri(offs,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(offs,(uintptr)comp_pc_p);
	add_l_ri(offs,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
{	int nsrc=scratchie++;
	make_flags_live();
	mov_l_rr(nsrc,src);
	lea_l_brr(scratchie,src,(uae_s32)-1);
	mov_w_rr(src,scratchie);
	cmov_l_rr(offs,PC_P,0);
	cmov_l_rr(src,nsrc,0);
	dbcc_cond_move_ne_w(PC_P, offs, nsrc);
	save_and_discard_flags_in_nzcv();
	if(srcreg != src)
		mov_w_rr(srcreg, src);
	discard_flags_in_nzcv();
}}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B Dn */
void REGPARAM2 op_59c0_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int src = srcreg;
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	if(srcreg != val)
		mov_b_rr(srcreg, val);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (An) */
void REGPARAM2 op_59d0_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = dodgy ? scratchie++ : srcreg + 8;
	if (dodgy)
		mov_l_rr(srca, srcreg + 8);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (An)+ */
void REGPARAM2 op_59d8_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_rr(srca, srcreg + 8);
	lea_l_brr(srcreg + 8,srcreg + 8, areg_byteinc[srcreg]);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B -(An) */
void REGPARAM2 op_59e0_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{{	int srca = dodgy ? scratchie++ : srcreg + 8;
	lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-areg_byteinc[srcreg]);
	if (dodgy)
		mov_l_rr(srca, 8 + srcreg);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (d16,An) */
void REGPARAM2 op_59e8_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_rr(srca, 8 + srcreg);
	lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (d8,An,Xn) */
void REGPARAM2 op_59f0_0_comp_ff(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (xxx).W */
void REGPARAM2 op_59f8_0_comp_ff(uae_u32 opcode) /* Scc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (xxx).L */
void REGPARAM2 op_59f9_0_comp_ff(uae_u32 opcode) /* Scc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4)); /* absl */
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* DBcc.W Dn,#<data>.W */
void REGPARAM2 op_59c8_0_comp_ff(uae_u32 opcode) /* DBcc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int src = srcreg;
{	int offs = scratchie++;
	mov_l_ri(offs, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	sign_extend_16_rr(offs,offs);
	sub_l_ri(offs,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(offs,(uintptr)comp_pc_p);
	add_l_ri(offs,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
{	int nsrc=scratchie++;
	make_flags_live();
	mov_l_rr(nsrc,src);
	lea_l_brr(scratchie,src,(uae_s32)-1);
	mov_w_rr(src,scratchie);
	cmov_l_rr(offs,PC_P,1);
	cmov_l_rr(src,nsrc,1);
	dbcc_cond_move_ne_w(PC_P, offs, nsrc);
	save_and_discard_flags_in_nzcv();
	if(srcreg != src)
		mov_w_rr(srcreg, src);
	discard_flags_in_nzcv();
}}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Bcc.W #<data>.W */
void REGPARAM2 op_6800_0_comp_ff(uae_u32 opcode) /* Bcc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	sign_extend_16_rr(src,src);
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,1);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* BccQ.B #<data> */
void REGPARAM2 op_6801_0_comp_ff(uae_u32 opcode) /* Bcc */
{
	uae_s32 srcreg = (uae_s32)(uae_s8)(opcode & 255);
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, srcreg);
	sign_extend_8_rr(src,src);
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,1);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Bcc.L #<data>.L */
void REGPARAM2 op_68ff_0_comp_ff(uae_u32 opcode) /* Bcc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, comp_get_ilong((m68k_pc_offset+=4)-4));
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,1);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Bcc.W #<data>.W */
void REGPARAM2 op_6900_0_comp_ff(uae_u32 opcode) /* Bcc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	sign_extend_16_rr(src,src);
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,0);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* BccQ.B #<data> */
void REGPARAM2 op_6901_0_comp_ff(uae_u32 opcode) /* Bcc */
{
	uae_s32 srcreg = (uae_s32)(uae_s8)(opcode & 255);
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, srcreg);
	sign_extend_8_rr(src,src);
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,0);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Bcc.L #<data>.L */
void REGPARAM2 op_69ff_0_comp_ff(uae_u32 opcode) /* Bcc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, comp_get_ilong((m68k_pc_offset+=4)-4));
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,0);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B Dn */
void REGPARAM2 op_58c0_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int src = srcreg;
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	if(srcreg != val)
		mov_b_rr(srcreg, val);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (An) */
void REGPARAM2 op_58d0_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = dodgy ? scratchie++ : srcreg + 8;
	if (dodgy)
		mov_l_rr(srca, srcreg + 8);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (An)+ */
void REGPARAM2 op_58d8_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_rr(srca, srcreg + 8);
	lea_l_brr(srcreg + 8,srcreg + 8, areg_byteinc[srcreg]);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B -(An) */
void REGPARAM2 op_58e0_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{{	int srca = dodgy ? scratchie++ : srcreg + 8;
	lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-areg_byteinc[srcreg]);
	if (dodgy)
		mov_l_rr(srca, 8 + srcreg);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (d16,An) */
void REGPARAM2 op_58e8_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_rr(srca, 8 + srcreg);
	lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (d8,An,Xn) */
void REGPARAM2 op_58f0_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (xxx).W */
void REGPARAM2 op_58f8_0_comp_nf(uae_u32 opcode) /* Scc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (xxx).L */
void REGPARAM2 op_58f9_0_comp_nf(uae_u32 opcode) /* Scc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4)); /* absl */
{	int val = scratchie++;
	make_flags_live();
	setcc(val,1);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* DBcc.W Dn,#<data>.W */
void REGPARAM2 op_58c8_0_comp_nf(uae_u32 opcode) /* DBcc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int src = srcreg;
{	int offs = scratchie++;
	mov_l_ri(offs, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	sign_extend_16_rr(offs,offs);
	sub_l_ri(offs,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(offs,(uintptr)comp_pc_p);
	add_l_ri(offs,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
{	int nsrc=scratchie++;
	make_flags_live();
	mov_l_rr(nsrc,src);
	lea_l_brr(scratchie,src,(uae_s32)-1);
	mov_w_rr(src,scratchie);
	cmov_l_rr(offs,PC_P,0);
	cmov_l_rr(src,nsrc,0);
	dbcc_cond_move_ne_w(PC_P, offs, nsrc);
	save_and_discard_flags_in_nzcv();
	if(srcreg != src)
		mov_w_rr(srcreg, src);
	discard_flags_in_nzcv();
}}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B Dn */
void REGPARAM2 op_59c0_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int src = srcreg;
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	if(srcreg != val)
		mov_b_rr(srcreg, val);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (An) */
void REGPARAM2 op_59d0_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = dodgy ? scratchie++ : srcreg + 8;
	if (dodgy)
		mov_l_rr(srca, srcreg + 8);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (An)+ */
void REGPARAM2 op_59d8_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_rr(srca, srcreg + 8);
	lea_l_brr(srcreg + 8,srcreg + 8, areg_byteinc[srcreg]);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B -(An) */
void REGPARAM2 op_59e0_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{{	int srca = dodgy ? scratchie++ : srcreg + 8;
	lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-areg_byteinc[srcreg]);
	if (dodgy)
		mov_l_rr(srca, 8 + srcreg);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (d16,An) */
void REGPARAM2 op_59e8_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_rr(srca, 8 + srcreg);
	lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (d8,An,Xn) */
void REGPARAM2 op_59f0_0_comp_nf(uae_u32 opcode) /* Scc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (xxx).W */
void REGPARAM2 op_59f8_0_comp_nf(uae_u32 opcode) /* Scc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Scc.B (xxx).L */
void REGPARAM2 op_59f9_0_comp_nf(uae_u32 opcode) /* Scc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int srca = scratchie++;
	mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4)); /* absl */
{	int val = scratchie++;
	make_flags_live();
	setcc(val,0);
	sub_b_ri(val,1);
	writebyte(srca, val, scratchie);
}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* DBcc.W Dn,#<data>.W */
void REGPARAM2 op_59c8_0_comp_nf(uae_u32 opcode) /* DBcc */
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 srcreg = ((opcode >> 8) & 7);
#else
	uae_s32 srcreg = (opcode & 7);
#endif
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
{	int src = srcreg;
{	int offs = scratchie++;
	mov_l_ri(offs, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	sign_extend_16_rr(offs,offs);
	sub_l_ri(offs,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(offs,(uintptr)comp_pc_p);
	add_l_ri(offs,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
{	int nsrc=scratchie++;
	make_flags_live();
	mov_l_rr(nsrc,src);
	lea_l_brr(scratchie,src,(uae_s32)-1);
	mov_w_rr(src,scratchie);
	cmov_l_rr(offs,PC_P,1);
	cmov_l_rr(src,nsrc,1);
	dbcc_cond_move_ne_w(PC_P, offs, nsrc);
	save_and_discard_flags_in_nzcv();
	if(srcreg != src)
		mov_w_rr(srcreg, src);
	discard_flags_in_nzcv();
}}}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Bcc.W #<data>.W */
void REGPARAM2 op_6800_0_comp_nf(uae_u32 opcode) /* Bcc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	sign_extend_16_rr(src,src);
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,1);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* BccQ.B #<data> */
void REGPARAM2 op_6801_0_comp_nf(uae_u32 opcode) /* Bcc */
{
	uae_s32 srcreg = (uae_s32)(uae_s8)(opcode & 255);
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, srcreg);
	sign_extend_8_rr(src,src);
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,1);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Bcc.L #<data>.L */
void REGPARAM2 op_68ff_0_comp_nf(uae_u32 opcode) /* Bcc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, comp_get_ilong((m68k_pc_offset+=4)-4));
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,1);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Bcc.W #<data>.W */
void REGPARAM2 op_6900_0_comp_nf(uae_u32 opcode) /* Bcc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	sign_extend_16_rr(src,src);
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,0);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* BccQ.B #<data> */
void REGPARAM2 op_6901_0_comp_nf(uae_u32 opcode) /* Bcc */
{
	uae_s32 srcreg = (uae_s32)(uae_s8)(opcode & 255);
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, srcreg);
	sign_extend_8_rr(src,src);
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,0);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* Bcc.L #<data>.L */
void REGPARAM2 op_69ff_0_comp_nf(uae_u32 opcode) /* Bcc */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	uintptr v,v1,v2;
{	int src = scratchie++;
	mov_l_ri(src, comp_get_ilong((m68k_pc_offset+=4)-4));
	sub_l_ri(src,m68k_pc_offset-m68k_pc_offset_thisinst-2);
	arm_ADD_l_ri(src,(uintptr)comp_pc_p);
	mov_l_ri(PC_P,(uintptr)comp_pc_p);
	add_l_ri(src,m68k_pc_offset);
	add_l_ri(PC_P,m68k_pc_offset);
	m68k_pc_offset=0;
	v1=get_const(PC_P);
	v2=get_const(src);
	register_branch(v1,v2,0);
	make_flags_live();
}}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}
