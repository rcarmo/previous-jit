/* Priority gap-fill handlers for supervisor/control ops. */

/* MVSR2.W Dn */
void REGPARAM2 op_40c0_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	dont_care_flags();
	{ int enc = scratchie++;
	  mov_l_ri(enc, (srcreg & 7) | (1 << 4));
	  mov_l_mr((uintptr)&regs.jit_exception, enc);
	}
	flush(1);
	call_helper((uintptr)jit_op_mvsr2);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (An) */
void REGPARAM2 op_40d0_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = dodgy ? scratchie++ : srcreg + 8;
	  if (dodgy) mov_l_rr(srca, srcreg + 8);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (An)+ */
void REGPARAM2 op_40d8_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  mov_l_rr(srca, srcreg + 8);
	  lea_l_brr(srcreg + 8, srcreg + 8, 2);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W -(An) */
void REGPARAM2 op_40e0_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = dodgy ? scratchie++ : srcreg + 8;
	  lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-2);
	  if (dodgy) mov_l_rr(srca, 8 + srcreg);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (d16,An) */
void REGPARAM2 op_40e8_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  mov_l_rr(srca, 8 + srcreg);
	  lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (d8,An,Xn) */
void REGPARAM2 op_40f0_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (xxx).W */
void REGPARAM2 op_40f8_0_comp_ff(uae_u32 opcode) /* MVSR2 */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int srca = scratchie++;
	  mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (xxx).L */
void REGPARAM2 op_40f9_0_comp_ff(uae_u32 opcode) /* MVSR2 */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int srca = scratchie++;
	  mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B Dn */
void REGPARAM2 op_42c0_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	dont_care_flags();
	{ int enc = scratchie++;
	  mov_l_ri(enc, (srcreg & 7) | (0 << 4));
	  mov_l_mr((uintptr)&regs.jit_exception, enc);
	}
	flush(1);
	call_helper((uintptr)jit_op_mvsr2);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (An) */
void REGPARAM2 op_42d0_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = dodgy ? scratchie++ : srcreg + 8;
	  if (dodgy) mov_l_rr(srca, srcreg + 8);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (An)+ */
void REGPARAM2 op_42d8_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  mov_l_rr(srca, srcreg + 8);
	  lea_l_brr(srcreg + 8, srcreg + 8, 2);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B -(An) */
void REGPARAM2 op_42e0_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = dodgy ? scratchie++ : srcreg + 8;
	  lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-2);
	  if (dodgy) mov_l_rr(srca, 8 + srcreg);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (d16,An) */
void REGPARAM2 op_42e8_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  mov_l_rr(srca, 8 + srcreg);
	  lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (d8,An,Xn) */
void REGPARAM2 op_42f0_0_comp_ff(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (xxx).W */
void REGPARAM2 op_42f8_0_comp_ff(uae_u32 opcode) /* MVSR2 */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int srca = scratchie++;
	  mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (xxx).L */
void REGPARAM2 op_42f9_0_comp_ff(uae_u32 opcode) /* MVSR2 */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int srca = scratchie++;
	  mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W Dn */
void REGPARAM2 op_40c0_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	dont_care_flags();
	{ int enc = scratchie++;
	  mov_l_ri(enc, (srcreg & 7) | (1 << 4));
	  mov_l_mr((uintptr)&regs.jit_exception, enc);
	}
	flush(1);
	call_helper((uintptr)jit_op_mvsr2);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (An) */
void REGPARAM2 op_40d0_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = dodgy ? scratchie++ : srcreg + 8;
	  if (dodgy) mov_l_rr(srca, srcreg + 8);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (An)+ */
void REGPARAM2 op_40d8_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  mov_l_rr(srca, srcreg + 8);
	  lea_l_brr(srcreg + 8, srcreg + 8, 2);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W -(An) */
void REGPARAM2 op_40e0_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = dodgy ? scratchie++ : srcreg + 8;
	  lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-2);
	  if (dodgy) mov_l_rr(srca, 8 + srcreg);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (d16,An) */
void REGPARAM2 op_40e8_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  mov_l_rr(srca, 8 + srcreg);
	  lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (d8,An,Xn) */
void REGPARAM2 op_40f0_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (xxx).W */
void REGPARAM2 op_40f8_0_comp_nf(uae_u32 opcode) /* MVSR2 */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int srca = scratchie++;
	  mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.W (xxx).L */
void REGPARAM2 op_40f9_0_comp_nf(uae_u32 opcode) /* MVSR2 */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int srca = scratchie++;
	  mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (1 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B Dn */
void REGPARAM2 op_42c0_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	dont_care_flags();
	{ int enc = scratchie++;
	  mov_l_ri(enc, (srcreg & 7) | (0 << 4));
	  mov_l_mr((uintptr)&regs.jit_exception, enc);
	}
	flush(1);
	call_helper((uintptr)jit_op_mvsr2);
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (An) */
void REGPARAM2 op_42d0_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = dodgy ? scratchie++ : srcreg + 8;
	  if (dodgy) mov_l_rr(srca, srcreg + 8);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (An)+ */
void REGPARAM2 op_42d8_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  mov_l_rr(srca, srcreg + 8);
	  lea_l_brr(srcreg + 8, srcreg + 8, 2);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B -(An) */
void REGPARAM2 op_42e0_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = dodgy ? scratchie++ : srcreg + 8;
	  lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-2);
	  if (dodgy) mov_l_rr(srca, 8 + srcreg);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (d16,An) */
void REGPARAM2 op_42e8_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  mov_l_rr(srca, 8 + srcreg);
	  lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (d8,An,Xn) */
void REGPARAM2 op_42f0_0_comp_nf(uae_u32 opcode) /* MVSR2 */
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
	{ int srca = scratchie++;
	  calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (xxx).W */
void REGPARAM2 op_42f8_0_comp_nf(uae_u32 opcode) /* MVSR2 */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int srca = scratchie++;
	  mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MVSR2.B (xxx).L */
void REGPARAM2 op_42f9_0_comp_nf(uae_u32 opcode) /* MVSR2 */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int srca = scratchie++;
	  mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	  dont_care_flags();
	  mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	  { int enc = scratchie++;
	    mov_l_ri(enc, (1 << 3) | (0 << 4));
	    mov_l_mr((uintptr)&regs.jit_exception, enc);
	  }
	  flush(1);
	  call_helper((uintptr)jit_op_mvsr2);
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}
