/* Priority gap-fill handlers for 68020 bitfield helper-backed forms. */

/* These handlers intentionally use the same flush + call_helper pattern as the
 * other complex gap-fill opcodes.  The native helper owns the bitfield decode
 * and exact flag/result semantics; the compiler-side code only materializes the
 * extension word, effective-address descriptor, and operation kind. */

enum {
	JIT_BF_OP_BFTST = 0,
	JIT_BF_OP_BFEXTU = 1,
	JIT_BF_OP_BFCHG = 2,
	JIT_BF_OP_BFEXTS = 3,
	JIT_BF_OP_BFCLR = 4,
	JIT_BF_OP_BFFFO = 5,
	JIT_BF_OP_BFSET = 6
};

enum {
	JIT_BF_MODE_DREG = 0,
	JIT_BF_MODE_AIND = 1,
	JIT_BF_MODE_AD16 = 2,
	JIT_BF_MODE_AD8R = 3,
	JIT_BF_MODE_ABSW = 4,
	JIT_BF_MODE_ABSL = 5,
	JIT_BF_MODE_PC16 = 6,
	JIT_BF_MODE_PC8R = 7
};

static void comp_bitfield_gapfill(uae_u32 opcode, int op_kind, int ea_mode)
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 dstreg = (opcode >> 8) & 7;
#else
	uae_u32 dstreg = opcode & 7;
#endif
	m68k_pc_offset += 2;
{	uae_u8 scratchie = S1;
	int extra = scratchie++;
	mov_l_ri(extra, ((op_kind & 0xff) << 16) | (comp_get_iword((m68k_pc_offset += 2) - 2) & 0xffff));

	int dsta = -1;
	switch (ea_mode) {
	case JIT_BF_MODE_DREG:
		dsta = scratchie++;
		mov_l_ri(dsta, (uae_s32)(0x80000000u | (dstreg & 7)));
		break;
	case JIT_BF_MODE_AIND:
		dsta = dstreg + 8;
		break;
	case JIT_BF_MODE_AD16:
		dsta = scratchie++;
		mov_l_rr(dsta, 8 + dstreg);
		lea_l_brr(dsta, dsta, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset += 2) - 2));
		break;
	case JIT_BF_MODE_AD8R:
		dsta = scratchie++;
		calc_disp_ea_020(dstreg + 8, comp_get_iword((m68k_pc_offset += 2) - 2), dsta, scratchie);
		break;
	case JIT_BF_MODE_ABSW:
		dsta = scratchie++;
		mov_l_ri(dsta, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset += 2) - 2));
		break;
	case JIT_BF_MODE_ABSL:
		dsta = scratchie++;
		mov_l_ri(dsta, comp_get_ilong((m68k_pc_offset += 4) - 4));
		break;
	case JIT_BF_MODE_PC16:
		dsta = scratchie++;
		{
			uae_u32 address = start_pc + ((char *)comp_pc_p - (char *)start_pc_p) + m68k_pc_offset;
			uae_s32 pc16off = (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset += 2) - 2);
			mov_l_ri(dsta, address + pc16off);
		}
		break;
	case JIT_BF_MODE_PC8R:
		{
			int pctmp = scratchie++;
			dsta = scratchie++;
			uae_u32 address = start_pc + ((char *)comp_pc_p - (char *)start_pc_p) + m68k_pc_offset;
			mov_l_ri(pctmp, address);
			calc_disp_ea_020(pctmp, comp_get_iword((m68k_pc_offset += 2) - 2), dsta, scratchie);
		}
		break;
	}

	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, extra);
	mov_l_mr((uintptr)&regs.scratchregs[0], dsta);
	flush(1);
	call_helper((uintptr)jit_op_bitfield);
}
	if (m68k_pc_offset > SYNC_PC_OFFSET)
		sync_m68k_pc();
}

#define BF_GAP(OPHEX, KIND, MODE) \
void REGPARAM2 op_##OPHEX##_0_comp_ff(uae_u32 opcode) { comp_bitfield_gapfill(opcode, KIND, MODE); } \
void REGPARAM2 op_##OPHEX##_0_comp_nf(uae_u32 opcode) { comp_bitfield_gapfill(opcode, KIND, MODE); }

/* BFTST */
BF_GAP(e8c0, JIT_BF_OP_BFTST, JIT_BF_MODE_DREG)
BF_GAP(e8d0, JIT_BF_OP_BFTST, JIT_BF_MODE_AIND)
BF_GAP(e8e8, JIT_BF_OP_BFTST, JIT_BF_MODE_AD16)
BF_GAP(e8f0, JIT_BF_OP_BFTST, JIT_BF_MODE_AD8R)
BF_GAP(e8f8, JIT_BF_OP_BFTST, JIT_BF_MODE_ABSW)
BF_GAP(e8f9, JIT_BF_OP_BFTST, JIT_BF_MODE_ABSL)
BF_GAP(e8fa, JIT_BF_OP_BFTST, JIT_BF_MODE_PC16)
BF_GAP(e8fb, JIT_BF_OP_BFTST, JIT_BF_MODE_PC8R)

/* BFEXTU */
BF_GAP(e9c0, JIT_BF_OP_BFEXTU, JIT_BF_MODE_DREG)
BF_GAP(e9d0, JIT_BF_OP_BFEXTU, JIT_BF_MODE_AIND)
BF_GAP(e9e8, JIT_BF_OP_BFEXTU, JIT_BF_MODE_AD16)
BF_GAP(e9f0, JIT_BF_OP_BFEXTU, JIT_BF_MODE_AD8R)
BF_GAP(e9f8, JIT_BF_OP_BFEXTU, JIT_BF_MODE_ABSW)
BF_GAP(e9f9, JIT_BF_OP_BFEXTU, JIT_BF_MODE_ABSL)
BF_GAP(e9fa, JIT_BF_OP_BFEXTU, JIT_BF_MODE_PC16)
BF_GAP(e9fb, JIT_BF_OP_BFEXTU, JIT_BF_MODE_PC8R)

/* BFCHG */
BF_GAP(eac0, JIT_BF_OP_BFCHG, JIT_BF_MODE_DREG)
BF_GAP(ead0, JIT_BF_OP_BFCHG, JIT_BF_MODE_AIND)
BF_GAP(eae8, JIT_BF_OP_BFCHG, JIT_BF_MODE_AD16)
BF_GAP(eaf0, JIT_BF_OP_BFCHG, JIT_BF_MODE_AD8R)
BF_GAP(eaf8, JIT_BF_OP_BFCHG, JIT_BF_MODE_ABSW)
BF_GAP(eaf9, JIT_BF_OP_BFCHG, JIT_BF_MODE_ABSL)

/* BFEXTS */
BF_GAP(ebc0, JIT_BF_OP_BFEXTS, JIT_BF_MODE_DREG)
BF_GAP(ebd0, JIT_BF_OP_BFEXTS, JIT_BF_MODE_AIND)
BF_GAP(ebe8, JIT_BF_OP_BFEXTS, JIT_BF_MODE_AD16)
BF_GAP(ebf0, JIT_BF_OP_BFEXTS, JIT_BF_MODE_AD8R)
BF_GAP(ebf8, JIT_BF_OP_BFEXTS, JIT_BF_MODE_ABSW)
BF_GAP(ebf9, JIT_BF_OP_BFEXTS, JIT_BF_MODE_ABSL)
BF_GAP(ebfa, JIT_BF_OP_BFEXTS, JIT_BF_MODE_PC16)
BF_GAP(ebfb, JIT_BF_OP_BFEXTS, JIT_BF_MODE_PC8R)

/* BFCLR */
BF_GAP(ecc0, JIT_BF_OP_BFCLR, JIT_BF_MODE_DREG)
BF_GAP(ecd0, JIT_BF_OP_BFCLR, JIT_BF_MODE_AIND)
BF_GAP(ece8, JIT_BF_OP_BFCLR, JIT_BF_MODE_AD16)
BF_GAP(ecf0, JIT_BF_OP_BFCLR, JIT_BF_MODE_AD8R)
BF_GAP(ecf8, JIT_BF_OP_BFCLR, JIT_BF_MODE_ABSW)
BF_GAP(ecf9, JIT_BF_OP_BFCLR, JIT_BF_MODE_ABSL)

/* BFFFO */
BF_GAP(edc0, JIT_BF_OP_BFFFO, JIT_BF_MODE_DREG)
BF_GAP(edd0, JIT_BF_OP_BFFFO, JIT_BF_MODE_AIND)
BF_GAP(ede8, JIT_BF_OP_BFFFO, JIT_BF_MODE_AD16)
BF_GAP(edf0, JIT_BF_OP_BFFFO, JIT_BF_MODE_AD8R)
BF_GAP(edf8, JIT_BF_OP_BFFFO, JIT_BF_MODE_ABSW)
BF_GAP(edf9, JIT_BF_OP_BFFFO, JIT_BF_MODE_ABSL)
BF_GAP(edfa, JIT_BF_OP_BFFFO, JIT_BF_MODE_PC16)
BF_GAP(edfb, JIT_BF_OP_BFFFO, JIT_BF_MODE_PC8R)

/* BFSET */
BF_GAP(eec0, JIT_BF_OP_BFSET, JIT_BF_MODE_DREG)
BF_GAP(eed0, JIT_BF_OP_BFSET, JIT_BF_MODE_AIND)
BF_GAP(eee8, JIT_BF_OP_BFSET, JIT_BF_MODE_AD16)
BF_GAP(eef0, JIT_BF_OP_BFSET, JIT_BF_MODE_AD8R)
BF_GAP(eef8, JIT_BF_OP_BFSET, JIT_BF_MODE_ABSW)
BF_GAP(eef9, JIT_BF_OP_BFSET, JIT_BF_MODE_ABSL)

#undef BF_GAP
