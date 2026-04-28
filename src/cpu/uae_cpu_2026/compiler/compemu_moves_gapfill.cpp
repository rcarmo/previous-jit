/* Priority gap-fill handlers for MOVES helper-backed forms. */

/* MOVES.B #<data>.W,(An) */
void REGPARAM2 op_e10_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    if (dodgy) mov_l_rr(srca, srcreg + 8);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(An)+ */
void REGPARAM2 op_e18_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, srcreg + 8);
	    lea_l_brr(srcreg + 8, srcreg + 8, areg_byteinc[srcreg]);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,-(An) */
void REGPARAM2 op_e20_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-areg_byteinc[srcreg]);
	    if (dodgy) mov_l_rr(srca, 8 + srcreg);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(d16,An) */
void REGPARAM2 op_e28_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, 8 + srcreg);
	    lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(d8,An,Xn) */
void REGPARAM2 op_e30_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(xxx).W */
void REGPARAM2 op_e38_0_comp_ff(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(xxx).L */
void REGPARAM2 op_e39_0_comp_ff(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(An) */
void REGPARAM2 op_e50_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    if (dodgy) mov_l_rr(srca, srcreg + 8);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(An)+ */
void REGPARAM2 op_e58_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, srcreg + 8);
	    lea_l_brr(srcreg + 8, srcreg + 8, 2);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,-(An) */
void REGPARAM2 op_e60_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-2);
	    if (dodgy) mov_l_rr(srca, 8 + srcreg);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(d16,An) */
void REGPARAM2 op_e68_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, 8 + srcreg);
	    lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(d8,An,Xn) */
void REGPARAM2 op_e70_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(xxx).W */
void REGPARAM2 op_e78_0_comp_ff(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(xxx).L */
void REGPARAM2 op_e79_0_comp_ff(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(An) */
void REGPARAM2 op_e90_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    if (dodgy) mov_l_rr(srca, srcreg + 8);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(An)+ */
void REGPARAM2 op_e98_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, srcreg + 8);
	    lea_l_brr(srcreg + 8, srcreg + 8, 4);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,-(An) */
void REGPARAM2 op_ea0_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-4);
	    if (dodgy) mov_l_rr(srca, 8 + srcreg);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(d16,An) */
void REGPARAM2 op_ea8_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, 8 + srcreg);
	    lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(d8,An,Xn) */
void REGPARAM2 op_eb0_0_comp_ff(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(xxx).W */
void REGPARAM2 op_eb8_0_comp_ff(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(xxx).L */
void REGPARAM2 op_eb9_0_comp_ff(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(An) */
void REGPARAM2 op_e10_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    if (dodgy) mov_l_rr(srca, srcreg + 8);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(An)+ */
void REGPARAM2 op_e18_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, srcreg + 8);
	    lea_l_brr(srcreg + 8, srcreg + 8, areg_byteinc[srcreg]);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,-(An) */
void REGPARAM2 op_e20_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-areg_byteinc[srcreg]);
	    if (dodgy) mov_l_rr(srca, 8 + srcreg);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(d16,An) */
void REGPARAM2 op_e28_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, 8 + srcreg);
	    lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(d8,An,Xn) */
void REGPARAM2 op_e30_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(xxx).W */
void REGPARAM2 op_e38_0_comp_nf(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.B #<data>.W,(xxx).L */
void REGPARAM2 op_e39_0_comp_nf(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 0 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(An) */
void REGPARAM2 op_e50_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    if (dodgy) mov_l_rr(srca, srcreg + 8);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(An)+ */
void REGPARAM2 op_e58_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, srcreg + 8);
	    lea_l_brr(srcreg + 8, srcreg + 8, 2);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,-(An) */
void REGPARAM2 op_e60_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-2);
	    if (dodgy) mov_l_rr(srca, 8 + srcreg);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(d16,An) */
void REGPARAM2 op_e68_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, 8 + srcreg);
	    lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(d8,An,Xn) */
void REGPARAM2 op_e70_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(xxx).W */
void REGPARAM2 op_e78_0_comp_nf(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.W #<data>.W,(xxx).L */
void REGPARAM2 op_e79_0_comp_nf(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 1 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(An) */
void REGPARAM2 op_e90_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    if (dodgy) mov_l_rr(srca, srcreg + 8);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(An)+ */
void REGPARAM2 op_e98_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, srcreg + 8);
	    lea_l_brr(srcreg + 8, srcreg + 8, 4);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,-(An) */
void REGPARAM2 op_ea0_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = dodgy ? scratchie++ : srcreg + 8;
	    lea_l_brr(srcreg + 8, srcreg + 8, (uae_s32)-4);
	    if (dodgy) mov_l_rr(srca, 8 + srcreg);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(d16,An) */
void REGPARAM2 op_ea8_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_rr(srca, 8 + srcreg);
	    lea_l_brr(srca, srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(d8,An,Xn) */
void REGPARAM2 op_eb0_0_comp_nf(uae_u32 opcode) /* MOVES */
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
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    calc_disp_ea_020(srcreg + 8, comp_get_iword((m68k_pc_offset+=2)-2), srca, scratchie);
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(xxx).W */
void REGPARAM2 op_eb8_0_comp_nf(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}

/* MOVES.L #<data>.W,(xxx).L */
void REGPARAM2 op_eb9_0_comp_nf(uae_u32 opcode) /* MOVES */
{
	uae_u32 dodgy=0;
	uae_u32 m68k_pc_offset_thisinst=m68k_pc_offset;
	m68k_pc_offset+=2;
{	uae_u8 scratchie=S1;
	{ int extra = scratchie++;
	  mov_l_ri(extra, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2));
	  { int srca = scratchie++;
	    mov_l_ri(srca, comp_get_ilong((m68k_pc_offset+=4)-4));
	    dont_care_flags();
	    mov_l_mr((uintptr)&regs.scratchregs[0], srca);
	    { int enc = scratchie++;
	      mov_l_rr(enc, extra);
	      or_l_ri(enc, 2 << 16);
	      mov_l_mr((uintptr)&regs.jit_exception, enc);
	    }
	    flush(1);
	    call_helper((uintptr)jit_op_moves);
	  }
	}
}
    if (m68k_pc_offset > SYNC_PC_OFFSET)
        sync_m68k_pc();
}
