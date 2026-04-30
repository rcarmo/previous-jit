/* Priority gap-fill handlers for CHK2/CAS helper-backed forms. */

enum {
	JIT_EA_AIND = 0,
	JIT_EA_AIPI = 1,
	JIT_EA_APDI = 2,
	JIT_EA_AD16 = 3,
	JIT_EA_AD8R = 4,
	JIT_EA_ABSW = 5,
	JIT_EA_ABSL = 6,
	JIT_EA_PC16 = 7,
	JIT_EA_PC8R = 8
};

static int comp_gapfill_ea(uae_u32 opcode, int ea_mode, int size, uae_u8 &scratchie)
{
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
	uae_u32 reg = (opcode >> 8) & 7;
#else
	uae_u32 reg = opcode & 7;
#endif
	int ea = -1;
	int inc = size == 0 ? (reg == 7 ? 2 : 1) : (size == 1 ? 2 : 4);

	switch (ea_mode) {
	case JIT_EA_AIND:
		ea = reg + 8;
		break;
	case JIT_EA_AIPI:
		ea = scratchie++;
		mov_l_rr(ea, reg + 8);
		lea_l_brr(reg + 8, reg + 8, inc);
		break;
	case JIT_EA_APDI:
		ea = scratchie++;
		lea_l_brr(reg + 8, reg + 8, -inc);
		mov_l_rr(ea, reg + 8);
		break;
	case JIT_EA_AD16:
		ea = scratchie++;
		mov_l_rr(ea, reg + 8);
		lea_l_brr(ea, ea, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset += 2) - 2));
		break;
	case JIT_EA_AD8R:
		ea = scratchie++;
		calc_disp_ea_020(reg + 8, comp_get_iword((m68k_pc_offset += 2) - 2), ea, scratchie);
		break;
	case JIT_EA_ABSW:
		ea = scratchie++;
		mov_l_ri(ea, (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset += 2) - 2));
		break;
	case JIT_EA_ABSL:
		ea = scratchie++;
		mov_l_ri(ea, comp_get_ilong((m68k_pc_offset += 4) - 4));
		break;
	case JIT_EA_PC16:
		ea = scratchie++;
		{
			uae_u32 address = start_pc + ((char *)comp_pc_p - (char *)start_pc_p) + m68k_pc_offset;
			uae_s32 pc16off = (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset += 2) - 2);
			mov_l_ri(ea, address + pc16off);
		}
		break;
	case JIT_EA_PC8R:
		{
			int pctmp = scratchie++;
			ea = scratchie++;
			uae_u32 address = start_pc + ((char *)comp_pc_p - (char *)start_pc_p) + m68k_pc_offset;
			mov_l_ri(pctmp, address);
			calc_disp_ea_020(pctmp, comp_get_iword((m68k_pc_offset += 2) - 2), ea, scratchie);
		}
		break;
	}
	return ea;
}

static void comp_chk2_gapfill(uae_u32 opcode, int size, int ea_mode)
{
	m68k_pc_offset += 2;
{	uae_u8 scratchie = S1;
	int extra = scratchie++;
	mov_l_ri(extra, ((size & 3) << 16) | (comp_get_iword((m68k_pc_offset += 2) - 2) & 0xffff));
	int ea = comp_gapfill_ea(opcode, ea_mode, size, scratchie);
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, extra);
	mov_l_mr((uintptr)&regs.scratchregs[0], ea);
	flush(1);
	call_helper((uintptr)jit_op_chk2);
}
	if (m68k_pc_offset > SYNC_PC_OFFSET)
		sync_m68k_pc();
}

static void comp_cas_gapfill(uae_u32 opcode, int size, int ea_mode)
{
	m68k_pc_offset += 2;
{	uae_u8 scratchie = S1;
	int extra = scratchie++;
	mov_l_ri(extra, ((size & 3) << 16) | (comp_get_iword((m68k_pc_offset += 2) - 2) & 0xffff));
	int ea = comp_gapfill_ea(opcode, ea_mode, size, scratchie);
	dont_care_flags();
	mov_l_mr((uintptr)&regs.jit_exception, extra);
	mov_l_mr((uintptr)&regs.scratchregs[0], ea);
	flush(1);
	call_helper((uintptr)jit_op_cas);
}
	if (m68k_pc_offset > SYNC_PC_OFFSET)
		sync_m68k_pc();
}

#define CHK2_GAP(OPHEX, SIZE, MODE) \
void REGPARAM2 op_##OPHEX##_0_comp_ff(uae_u32 opcode) { comp_chk2_gapfill(opcode, SIZE, MODE); } \
void REGPARAM2 op_##OPHEX##_0_comp_nf(uae_u32 opcode) { comp_chk2_gapfill(opcode, SIZE, MODE); }

/* CHK2.B */
CHK2_GAP(d0, 0, JIT_EA_AIND)
CHK2_GAP(e8, 0, JIT_EA_AD16)
CHK2_GAP(f0, 0, JIT_EA_AD8R)
CHK2_GAP(f8, 0, JIT_EA_ABSW)
CHK2_GAP(f9, 0, JIT_EA_ABSL)
CHK2_GAP(fa, 0, JIT_EA_PC16)
CHK2_GAP(fb, 0, JIT_EA_PC8R)

/* CHK2.W */
CHK2_GAP(2d0, 1, JIT_EA_AIND)
CHK2_GAP(2e8, 1, JIT_EA_AD16)
CHK2_GAP(2f0, 1, JIT_EA_AD8R)
CHK2_GAP(2f8, 1, JIT_EA_ABSW)
CHK2_GAP(2f9, 1, JIT_EA_ABSL)
CHK2_GAP(2fa, 1, JIT_EA_PC16)
CHK2_GAP(2fb, 1, JIT_EA_PC8R)

/* CHK2.L */
CHK2_GAP(4d0, 2, JIT_EA_AIND)
CHK2_GAP(4e8, 2, JIT_EA_AD16)
CHK2_GAP(4f0, 2, JIT_EA_AD8R)
CHK2_GAP(4f8, 2, JIT_EA_ABSW)
CHK2_GAP(4f9, 2, JIT_EA_ABSL)
CHK2_GAP(4fa, 2, JIT_EA_PC16)
CHK2_GAP(4fb, 2, JIT_EA_PC8R)

#undef CHK2_GAP

#define CAS_GAP(OPHEX, SIZE, MODE) \
void REGPARAM2 op_##OPHEX##_0_comp_ff(uae_u32 opcode) { comp_cas_gapfill(opcode, SIZE, MODE); } \
void REGPARAM2 op_##OPHEX##_0_comp_nf(uae_u32 opcode) { comp_cas_gapfill(opcode, SIZE, MODE); }

/* CAS.B */
CAS_GAP(ad0, 0, JIT_EA_AIND)
CAS_GAP(ad8, 0, JIT_EA_AIPI)
CAS_GAP(ae0, 0, JIT_EA_APDI)
CAS_GAP(ae8, 0, JIT_EA_AD16)
CAS_GAP(af0, 0, JIT_EA_AD8R)
CAS_GAP(af8, 0, JIT_EA_ABSW)
CAS_GAP(af9, 0, JIT_EA_ABSL)

/* CAS.W */
CAS_GAP(cd0, 1, JIT_EA_AIND)
CAS_GAP(cd8, 1, JIT_EA_AIPI)
CAS_GAP(ce0, 1, JIT_EA_APDI)
CAS_GAP(ce8, 1, JIT_EA_AD16)
CAS_GAP(cf0, 1, JIT_EA_AD8R)
CAS_GAP(cf8, 1, JIT_EA_ABSW)
CAS_GAP(cf9, 1, JIT_EA_ABSL)

/* CAS.L */
CAS_GAP(ed0, 2, JIT_EA_AIND)
CAS_GAP(ed8, 2, JIT_EA_AIPI)
CAS_GAP(ee0, 2, JIT_EA_APDI)
CAS_GAP(ee8, 2, JIT_EA_AD16)
CAS_GAP(ef0, 2, JIT_EA_AD8R)
CAS_GAP(ef8, 2, JIT_EA_ABSW)
CAS_GAP(ef9, 2, JIT_EA_ABSL)

#undef CAS_GAP
