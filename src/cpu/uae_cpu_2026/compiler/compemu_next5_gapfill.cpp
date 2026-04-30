/* Priority gap-fill handlers for the next remaining opcode groups.
 *
 * Covered here:
 * - CALLM: unsupported on this target, same as interpreter -> illegal instruction
 * - FRESTORE: supervisor-checked helper-backed FPU state restore
 * - CINVA / CPUSHA: supervisor-checked cache maintenance no-ops
 * - MMUOP030: unsupported 68030 MMU encodings -> illegal instruction
 */

static void comp_simple_opcode_helper(uae_u32 opcode, uintptr helper)
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

static void comp_callm_gapfill(uae_u32 opcode)
{
	comp_simple_opcode_helper(opcode, (uintptr)jit_op_callm);
}

static void comp_mmuop030_gapfill(uae_u32 opcode)
{
	comp_simple_opcode_helper(opcode, (uintptr)jit_op_mmuop030);
}

static void comp_frestore_gapfill(uae_u32 opcode)
{
	comp_simple_opcode_helper(opcode, (uintptr)jit_op_frestore);
}

static void comp_cinva_gapfill(uae_u32 opcode)
{
	comp_simple_opcode_helper(opcode, (uintptr)jit_op_cinva);
}

static void comp_cpusha_gapfill(uae_u32 opcode)
{
	comp_simple_opcode_helper(opcode, (uintptr)jit_op_cpusha);
}

#define SIMPLE_GAP(OPHEX, NAME) \
void REGPARAM2 op_##OPHEX##_0_comp_ff(uae_u32 opcode) { comp_##NAME##_gapfill(opcode); } \
void REGPARAM2 op_##OPHEX##_0_comp_nf(uae_u32 opcode) { comp_##NAME##_gapfill(opcode); }

/* CALLM */
SIMPLE_GAP(6d0, callm)
SIMPLE_GAP(6e8, callm)
SIMPLE_GAP(6f0, callm)
SIMPLE_GAP(6f8, callm)
SIMPLE_GAP(6f9, callm)
SIMPLE_GAP(6fa, callm)
SIMPLE_GAP(6fb, callm)

/* 68030 MMU opcodes */
SIMPLE_GAP(f000, mmuop030)
SIMPLE_GAP(f008, mmuop030)
SIMPLE_GAP(f010, mmuop030)
SIMPLE_GAP(f018, mmuop030)
SIMPLE_GAP(f020, mmuop030)
SIMPLE_GAP(f028, mmuop030)
SIMPLE_GAP(f030, mmuop030)
SIMPLE_GAP(f038, mmuop030)
SIMPLE_GAP(f039, mmuop030)

/* FRESTORE */
SIMPLE_GAP(f350, frestore)
SIMPLE_GAP(f358, frestore)
SIMPLE_GAP(f368, frestore)
SIMPLE_GAP(f370, frestore)
SIMPLE_GAP(f378, frestore)
SIMPLE_GAP(f379, frestore)
SIMPLE_GAP(f37a, frestore)
SIMPLE_GAP(f37b, frestore)

/* CINVA */
SIMPLE_GAP(f418, cinva)
SIMPLE_GAP(f419, cinva)
SIMPLE_GAP(f41a, cinva)
SIMPLE_GAP(f41b, cinva)
SIMPLE_GAP(f41c, cinva)
SIMPLE_GAP(f41d, cinva)
SIMPLE_GAP(f41e, cinva)
SIMPLE_GAP(f41f, cinva)

/* CPUSHA */
SIMPLE_GAP(f438, cpusha)
SIMPLE_GAP(f439, cpusha)
SIMPLE_GAP(f43a, cpusha)
SIMPLE_GAP(f43b, cpusha)
SIMPLE_GAP(f43c, cpusha)
SIMPLE_GAP(f43d, cpusha)
SIMPLE_GAP(f43e, cpusha)
SIMPLE_GAP(f43f, cpusha)

#undef SIMPLE_GAP
