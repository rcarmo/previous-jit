/* ARM64 compatibility layer for legacy gencomp helper names.
 * Included only from compemu_support.cpp after compemu_support_arm.cpp.
 */

extern "C" bool mmu_restart;
extern "C" uae_u16 mmu_opcode;
extern "C" int mmu040_movem;
extern "C" uaecptr mmu040_movem_ea;
struct previous_mmufixup_entry { int reg; uae_u32 value; };
extern "C" previous_mmufixup_entry mmufixup[2];
extern "C" void Uae2026JitMmuPutLong(uae_u32 addr, uae_u32 value);
extern "C" uae_u32 Uae2026JitMmuFetchOpcode(uae_u32 pc);
extern "C" uintptr_t Uae2026JitMmuXlateCodeHost(uae_u32 pc);
extern "C" void Uae2026JitMmuTxnBeginCallPushPreTarget(uae_u32 pc, uae_u32 opcode, uae_u32 pre_a7, uae_u32 target_pc);
extern "C" void Uae2026JitMmuTxnBeginCallPushPreTargetCurrentA7(uae_u32 pc, uae_u32 target_pc);
extern "C" void Uae2026JitMmuTxnBeginCallPushCurrentA7ForOpcode(uae_u32 pc, uae_u32 opcode);
extern "C" void Uae2026JitMmuTxnBeginReturnPopCurrentA7(uae_u32 pc, uae_u32 opcode, uae_u32 pop_bytes);
extern "C" void Uae2026JitMmuTxnCommit(void);
extern "C" void Uae2026JitCpuCheckTicks(int cycles);
extern "C" bool Uae2026OpcodeTestModeActive(void);
extern "C" bool jit_lockstep_window_pc(uae_u32 pc);
extern "C" bool Uae2026OpcodeTestModeHandleStopTrailer(void);
extern uintptr jit_MEMBaseDiff;
static inline void jit_interpreted_op_check_ticks(void)
{
	if (jit_allow_ram_dispatch_env())
		Uae2026JitCpuCheckTicks(4);
	else
		cpu_check_ticks();
}

extern "C" {
uae_u32 Uae2026JitLastInstructionPc = 0;
uae_u32 Uae2026JitLastSr = 0;
uae_u32 Uae2026JitLastA7 = 0;
uae_u32 Uae2026JitLowpcFaultSeq = 0;
uae_u32 Uae2026JitLastLowpcFaultPc = 0;
uae_u32 Uae2026JitLastLowpcFaultAddr = 0;
uae_u32 Uae2026JitLastLowpcFaultOpcode = 0;
uae_u32 Uae2026JitLastCodeHostPc = 0;
uae_u32 Uae2026JitLastCodeHostPhys = 0xffffffffu;
uae_u32 Uae2026JitLastCodeHostWords[12] = { 0 };
uae_u32 Uae2026JitCodeHostRingSeq = 0;
uae_u32 Uae2026JitCodeHostRingPc[64] = { 0 };
uae_u32 Uae2026JitCodeHostRingPhys[64] = { 0 };
uae_u32 Uae2026JitCodeHostRingWords[64][12] = { { 0 } };
uae_u32 Uae2026JitCodeHostRingRegs[64][16] = { { 0 } };
uae_u32 Uae2026JitCodeHostRingSr[64] = { 0 };
struct flag_struct Uae2026JitLastFlags = { 0, 0 };
}

extern "C" void Uae2026JitCanonicalizePcAfterFallback(void);

extern "C" void Uae2026JitPublishFallbackState(uae_u32 pc, uae_u32 opcode)
{
	regs.fault_pc = pc;
	Uae2026JitLastInstructionPc = pc;
	Uae2026JitLastSr = regs.sr;
	Uae2026JitLastA7 = m68k_areg(regs, 7);
	Uae2026JitLastFlags = regflags;
	mmu_restart = true;
	mmu_opcode = (uae_u16)opcode;
}

static inline void jit_publish_code_fetch_state(uae_u32 pc)
{
	regs.pc = pc;
	regs.fault_pc = pc;
	Uae2026JitLastInstructionPc = pc;
	Uae2026JitLastSr = regs.sr;
	Uae2026JitLastA7 = m68k_areg(regs, 7);
	Uae2026JitLastFlags = regflags;
	mmu_restart = true;
	mmu_opcode = (uae_u16)-1;
}

static inline void jit_canonicalize_code_pc_if_ram_mmu(void)
{
	if (!jit_allow_ram_dispatch_env() || (!regs.mmu_enabled && !Uae2026OpcodeTestModeActive()))
		return;
	uae_u32 pc = m68k_getpc() & ~1u;
	jit_publish_code_fetch_state(pc);
	regs.pc_p = (uae_u8 *)Uae2026JitMmuXlateCodeHost(pc);
	regs.pc_oldp = regs.pc_p;
}

static inline uae_u16 jit_fetch_opcode_via_code_host(uae_u32 pc)
{
	jit_publish_code_fetch_state(pc);
	uae_u8 *host = (uae_u8 *)Uae2026JitMmuXlateCodeHost(pc);
	regs.pc_p = host;
	regs.pc_oldp = host;
	Uae2026JitLastCodeHostPc = pc;
	Uae2026JitLastCodeHostPhys = (jit_MEMBaseDiff && (uintptr_t)host >= jit_MEMBaseDiff)
		? (uae_u32)((uintptr_t)host - jit_MEMBaseDiff) : 0xffffffffu;
	for (unsigned wi = 0; wi < 12; wi++) {
		uae_u8 *p = host + wi * 2;
		Uae2026JitLastCodeHostWords[wi] = ((uae_u16)p[0] << 8) | p[1];
	}
	{
		const unsigned slot = Uae2026JitCodeHostRingSeq++ & 63u;
		Uae2026JitCodeHostRingPc[slot] = pc;
		Uae2026JitCodeHostRingPhys[slot] = Uae2026JitLastCodeHostPhys;
		Uae2026JitCodeHostRingSr[slot] = regs.sr;
		for (unsigned wi = 0; wi < 12; wi++)
			Uae2026JitCodeHostRingWords[slot][wi] = Uae2026JitLastCodeHostWords[wi];
		for (unsigned ri = 0; ri < 16; ri++)
			Uae2026JitCodeHostRingRegs[slot][ri] = regs.regs[ri];
	}
	return (uae_u16)Uae2026JitLastCodeHostWords[0];
}

static inline uae_u32 jit_fetch_opcode_for_current_pc(uae_u32 pc)
{
	if (jit_allow_ram_dispatch_env() && regs.mmu_enabled) {
		const uintptr_t host_pc = (uintptr_t)regs.pc_p;
		const bool host_phys_known = jit_MEMBaseDiff && host_pc >= jit_MEMBaseDiff;
		const uae_u32 host_phys = host_phys_known ? (uae_u32)(host_pc - jit_MEMBaseDiff) : 0xffffffffu;
		/* Non-identity user mappings must fetch opcodes from the code-translated
		 * host page.  Direct GET_OPCODE and the legacy MMU iword fetch can observe
		 * the stale data-view stream (for example 0200/0c80 at 00003334 after the
		 * RTE seam) while pc_p/code shadow holds the correct translated bytes.
		 * Keep low overlay/ROM identity cases on the legacy 040 fetch path; forcing
		 * every low virtual fetch through the code host perturbs early ROM SCSI boot. */
		if (host_phys_known && host_phys != pc &&
			((pc >= 0x05000000u && pc < 0x08000000u) ||
			 (regs.vbr == 0x040ae61cu && pc >= 0x00003300u && pc <= 0x00003400u) ||
			 (getenv("B2_JIT_LOW12B_CODEHOST") && regs.vbr == 0x040ae61cu && pc >= 0x00012b00u && pc <= 0x00012c00u) ||
			 (getenv("B2_JIT_LOW83_CODEHOST") && regs.vbr == 0x040ae61cu && pc >= 0x00008300u && pc <= 0x00008340u) ||
			 (getenv("B2_JIT_LOW7F_CODEHOST") && regs.vbr == 0x040ae61cu && pc >= 0x00007f70u && pc <= 0x00008000u)))
			return jit_fetch_opcode_via_code_host(pc);
		if (pc < 0x01000000u) {
			jit_publish_code_fetch_state(pc);
			return (uae_u16)Uae2026JitMmuFetchOpcode(pc);
		}
	}
	return GET_OPCODE;
}

extern "C" uae_u32 Uae2026JitPrefetchGuard(uae_u32 pc, uae_u32 opcode)
{
	/* Preserve interpreter-style restart publication around a faultable 040
	 * instruction fetch while still allowing the native JIT body to run after a
	 * successful fetch.  If the fetch faults, mmu_opcode intentionally remains
	 * 0xffff, matching the primary interpreter prefetch path; if a later native
	 * data access faults, publish the opcode that the 040 MMU fetch actually saw
	 * rather than the compile-time opcode literal. */
	Uae2026JitPublishFallbackState(pc, 0xffffu);
	regs.fault_pc = pc;
	mmu_restart = true;
	mmu_opcode = (uae_u16)-1;
	const uae_u16 fetched_opcode = (jit_allow_ram_dispatch_env() && (regs.mmu_enabled || Uae2026OpcodeTestModeActive()))
		? jit_fetch_opcode_via_code_host(pc)
		: (uae_u16)Uae2026JitMmuFetchOpcode(pc);
	if ((uae_u16)opcode != 0xffffu && fetched_opcode != (uae_u16)opcode && getenv("B2_JIT_TRACE_PREFETCH_GUARD")) {
		static unsigned long mismatch_count;
		if (mismatch_count++ < 50)
			fprintf(stderr, "JIT_PREFETCH_GUARD_MISMATCH pc=%08x compiled=%04x fetched=%04x\n",
				(unsigned)pc, (unsigned)(opcode & 0xffffu), (unsigned)fetched_opcode);
	}
	Uae2026JitPublishFallbackState(pc, fetched_opcode);
	return fetched_opcode;
}

static inline bool legacy_needflags_enabled(void)
{
	return needflags != 0;
}

static inline void legacy_copy_carry_to_flagx(void)
{
	int x = writereg(FLAGX);
	if (flags_carry_inverted)
		CSET_xc(x, NATIVE_CC_CC);
	else
		CSET_xc(x, NATIVE_CC_CS);
	LSL_wwi(x, x, 29);
	unlock2(x);
}

static inline int legacy_x86_cc_to_native(int cc)
{
	switch (cc) {
	case 0: return NATIVE_CC_VS;
	case 1: return NATIVE_CC_VC;
	case 2: return NATIVE_CC_CS;
	case 3: return NATIVE_CC_CC;
	case 4: return NATIVE_CC_EQ;
	case 5: return NATIVE_CC_NE;
	case 6: return NATIVE_CC_LS;
	case 7: return NATIVE_CC_HI;
	case 8: return NATIVE_CC_MI;
	case 9: return NATIVE_CC_PL;
	case 12: return NATIVE_CC_LT;
	case 13: return NATIVE_CC_GE;
	case 14: return NATIVE_CC_LE;
	case 15: return NATIVE_CC_GT;
	default:
		jit_abort("unsupported legacy x86 condition code %d", cc);
		return NATIVE_CC_EQ;
	}
}

static inline int legacy_addr_with_offset_avoid(int base, uae_s32 offset, int avoid)
{
	if (offset == 0)
		return base;
	int tmp = REG_WORK1;
	if (tmp == base || tmp == avoid)
		tmp = REG_WORK2;
	if (tmp == base || tmp == avoid)
		tmp = REG_WORK3;
	if (tmp == base || tmp == avoid)
		tmp = REG_WORK4;
	if (tmp == base || tmp == avoid)
		jit_abort("no temporary register for legacy host-pointer offset base=%d avoid=%d", base, avoid);
	if (offset > 0 && offset <= 4095) {
		ADD_xxi(tmp, base, offset);
		return tmp;
	}
	if (offset < 0 && offset >= -4095) {
		SUB_xxi(tmp, base, -offset);
		return tmp;
	}
	jit_abort("unsupported legacy host-pointer offset %d", offset);
	return base;
}

static inline int legacy_addr_with_offset(int base, uae_s32 offset)
{
	return legacy_addr_with_offset_avoid(base, offset, -1);
}

static inline bool legacy_bsr_target(uae_u32 pc, uae_u16 opcode, uae_u32 *target_pc)
{
	if ((opcode & 0xff00u) != 0x6100u || !target_pc)
		return false;
	uae_s32 disp = 0;
	if ((opcode & 0x00ffu) == 0x0000u)
		disp = (uae_s32)(uae_s16)get_iword(2);
	else if ((opcode & 0x00ffu) == 0x00ffu)
		disp = (uae_s32)(((uae_u32)get_iword(2) << 16) | (uae_u32)get_iword(4));
	else
		disp = (uae_s32)(uae_s8)(opcode & 0x00ffu);
	*target_pc = pc + 2u + (uae_u32)disp;
	return true;
}

static inline bool legacy_bsr_l_target(uae_u32 pc, uae_u16 opcode, uae_u32 expected_target, uae_u32 *retpc)
{
	uae_u32 target = 0;
	if (opcode != 0x61ff || !legacy_bsr_target(pc, opcode, &target))
		return false;
	if (target != expected_target)
		return false;
	if (retpc)
		*retpc = pc + 6u;
	return true;
}

static inline bool legacy_bsr_target_live(uae_u32 pc, uae_u16 opcode, uae_u32 *target_pc)
{
	if ((opcode & 0xff00u) != 0x6100u || !target_pc)
		return false;
	uae_s32 disp = 0;
	if ((opcode & 0x00ffu) == 0x0000u)
		disp = (uae_s32)(uae_s16)Uae2026JitLiveGetWord(pc + 2);
	else if ((opcode & 0x00ffu) == 0x00ffu)
		disp = (uae_s32)(((uae_u32)Uae2026JitLiveGetWord(pc + 2) << 16) |
			(uae_u32)Uae2026JitLiveGetWord(pc + 4));
	else
		disp = (uae_s32)(uae_s8)(opcode & 0x00ffu);
	*target_pc = pc + 2u + (uae_u32)disp;
	return true;
}

static inline bool legacy_call_push_txn_opcode(uae_u32 pc, uae_u16 opcode)
{
	if ((opcode & 0xff00u) == 0x6100u)
		return true;
	/* Confirmed post-RTE/user seams where JSR pushes a return address before
	   target code fetch can fault. Keep JSR transaction coverage narrow until
	   all addressing modes have producer-side target metadata; a broad JSR(An)
	   rule rolls back unrelated early boot probes and stalls before user space. */
	return (pc == 0x0000003eu || pc == 0x00003c26u || pc == 0x00008334u ||
		pc == 0x0000c52cu || pc == 0x05027706u) &&
		(opcode & 0xffc0u) == 0x4e80u;
}

static inline void legacy_maybe_begin_call_push_txn(uae_u32 pc, uae_u16 opcode)
{
	if (!jit_allow_ram_dispatch_env() || !regs.mmu_enabled || !legacy_call_push_txn_opcode(pc, opcode))
		return;
	uae_u32 target_pc = 0;
	if (legacy_bsr_target_live(pc, opcode, &target_pc)) {
		Uae2026JitMmuTxnBeginCallPushPreTarget(pc, opcode, m68k_areg(regs, 7), target_pc);
		return;
	}
	Uae2026JitMmuTxnBeginCallPushCurrentA7ForOpcode(pc, opcode);
}

static inline uae_u32 legacy_return_pop_bytes(uae_u16 opcode)
{
	if (opcode == 0x4e75u) /* RTS */
		return 4;
	if (opcode == 0x4e77u) /* RTR: PC long + CCR word */
		return 6;
	return 0;
}

static inline void legacy_maybe_begin_return_pop_txn(uae_u32 pc, uae_u16 opcode)
{
	const uae_u32 pop_bytes = legacy_return_pop_bytes(opcode);
	if (pop_bytes && jit_allow_ram_dispatch_env() && regs.mmu_enabled)
		Uae2026JitMmuTxnBeginReturnPopCurrentA7(pc, opcode, pop_bytes);
}

static inline bool legacy_rom_delay_bsr_callsite(uae_u32 pc, uae_u16 opcode, uae_u32 *retpc)
{
	if (pc != 0x01007922u && pc != 0x0100969cu && pc != 0x0100ce26u)
		return false;
	return legacy_bsr_l_target(pc, opcode, 0x010024ccu, retpc);
}

static inline bool legacy_rom_vbr_lookup_bsr_callsite(uae_u32 pc, uae_u16 opcode, uae_u32 *retpc)
{
	if (pc != 0x0100139au)
		return false;
	return legacy_bsr_l_target(pc, opcode, 0x010003c2u, retpc);
}

static inline bool legacy_rom_rtc_read_bsr_callsite(uae_u32 pc, uae_u16 opcode, uae_u32 *retpc)
{
	(void)pc;
	return legacy_bsr_l_target(pc, opcode, 0x010077aau, retpc);
}

static inline bool legacy_ram_direct_movem_long_predec(uae_u32 pc, uae_u16 opcode)
{
	if (!jit_allow_ram_dispatch_env() || pc < 0x04000000u || pc >= 0x08000000u)
		return false;
	/* This shortcut bypasses the generated 040 MOVEM restart/fixup path.  If
	 * one of its MMU writes faults, the bridge only sees the partially-emitted
	 * helper state rather than the interpreter's normal MOVEM bookkeeping.  Keep
	 * it available for diagnostics, but default to the exact interpreter path. */
	{
		const char *env = getenv("B2_JIT_RAM_DIRECT_MOVEM_PREDEC");
		if (!(env && *env && strcmp(env, "0") != 0))
			return false;
	}
	if ((opcode & 0xfff8u) != 0x48e0u)
		return false;
	const uae_u32 dstreg = opcode & 7u;
	const uae_u16 mask = (uae_u16)Uae2026JitLiveGetWord(pc + 2);
	uaecptr srca = m68k_areg(regs, dstreg);
	uae_u16 amask = mask & 0xffu;
	uae_u16 dmask = (mask >> 8) & 0xffu;
	mmu040_movem = 1;
	mmu040_movem_ea = srca;
	while (amask) {
		srca -= 4;
		Uae2026JitMmuPutLong(srca, m68k_areg(regs, movem_index2[amask]));
		amask = movem_next[amask];
	}
	while (dmask) {
		srca -= 4;
		Uae2026JitMmuPutLong(srca, m68k_dreg(regs, movem_index2[dmask]));
		dmask = movem_next[dmask];
	}
	m68k_areg(regs, dstreg) = srca;
	mmu040_movem = 0;
	jit_set_guest_pc_fast(pc + 4);
	return true;
}

static inline bool legacy_rom_rtc_write_bsr_callsite(uae_u32 pc, uae_u16 opcode, uae_u32 *retpc)
{
	(void)pc;
	return legacy_bsr_l_target(pc, opcode, 0x010076c6u, retpc);
}

void start_needflags(void) { needflags = 1; }
void end_needflags(void) { needflags = 0; }

void duplicate_carry(void)
{
	if (!(needed_flags & FLAG_X))
		return;
	legacy_copy_carry_to_flagx();
}

void restore_carry(void)
{
	int x = readreg(FLAGX);
	SUBS_wwi(REG_WORK3, x, 1);
	unlock2(x);
	flags_carry_inverted = false;
}

void add_b(RW1 d, RR1 s) { if (legacy_needflags_enabled()) jff_ADD_b(d, s); else jnf_ADD_b(d, s); }
void add_w(RW2 d, RR2 s) { if (legacy_needflags_enabled()) jff_ADD_w(d, s); else jnf_ADD_w(d, s); }
void add_l(RW4 d, RR4 s) {
#ifdef CPU_AARCH64
	if (d == PC_P) { arm_ADD_l(d, s); return; }
#endif
	if (legacy_needflags_enabled()) jff_ADD_l(d, s); else jnf_ADD_l(d, s);
}
void add_l_ri(RW4 d, uae_s32 i) {
#ifdef CPU_AARCH64
	if (d == PC_P) { arm_ADD_l_ri(d, (uintptr)(uae_s64)i); return; }
#endif
	if (legacy_needflags_enabled()) jff_ADD_l_imm(d, i); else jnf_ADD_l_imm(d, i);
}
void sub_b(RW1 d, RR1 s) { if (legacy_needflags_enabled()) jff_SUB_b(d, s); else jnf_SUB_b(d, s); }
void sub_w(RW2 d, RR2 s) { if (legacy_needflags_enabled()) jff_SUB_w(d, s); else jnf_SUB_w(d, s); }
void sub_l(RW4 d, RR4 s) { if (legacy_needflags_enabled()) jff_SUB_l(d, s); else jnf_SUB_l(d, s); }
void sub_b_ri(RW1 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_SUB_b_imm(d, i); else jnf_SUB_b_imm(d, i); }
void and_b(RW1 d, RR1 s) { if (legacy_needflags_enabled()) jff_AND_b(d, s); else jnf_AND_b(d, s); }
void and_w(RW2 d, RR2 s) { if (legacy_needflags_enabled()) jff_AND_w(d, s); else jnf_AND_w(d, s); }
void and_l(RW4 d, RR4 s) { if (legacy_needflags_enabled()) jff_AND_l(d, s); else jnf_AND_l(d, s); }
void and_l_ri(RW4 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_AND_l_imm(d, i); else jnf_AND_l_imm(d, i); }
void or_b(RW1 d, RR1 s) { if (legacy_needflags_enabled()) jff_OR_b(d, s); else jnf_OR_b(d, s); }
void or_w(RW2 d, RR2 s) { if (legacy_needflags_enabled()) jff_OR_w(d, s); else jnf_OR_w(d, s); }
void or_l(RW4 d, RR4 s) { if (legacy_needflags_enabled()) jff_OR_l(d, s); else jnf_OR_l(d, s); }
void or_l_ri(RW4 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_OR_l_imm(d, i); else jnf_OR_l_imm(d, i); }
void xor_b(RW1 d, RR1 s) { if (legacy_needflags_enabled()) jff_EOR_b(d, s); else jnf_EOR_b(d, s); }
void xor_w(RW2 d, RR2 s) { if (legacy_needflags_enabled()) jff_EOR_w(d, s); else jnf_EOR_w(d, s); }
void xor_l(RW4 d, RR4 s) { if (legacy_needflags_enabled()) jff_EOR_l(d, s); else jnf_EOR_l(d, s); }
void cmp_b(RR1 d, RR1 s) { jff_CMP_b(d, s); }
void cmp_w(RR2 d, RR2 s) { jff_CMP_w(d, s); }
void cmp_l(RR4 d, RR4 s) { jff_CMP_l(d, s); }
void mov_b_rr(W1 d, RR1 s) { if (legacy_needflags_enabled()) jff_MOVE_b(d, s); else jnf_MOVE_b(d, s); }
void mov_w_rr(W2 d, RR2 s) { if (legacy_needflags_enabled()) jff_MOVE_w(d, s); else jnf_MOVE_w(d, s); }
void mov_w_ri(W2 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_MOVE_w_imm(d, i); else jnf_MOVE_w_imm(d, i); }

void zero_extend_8_rr(W4 d, RR1 s)
{
	if (isconst(s)) {
		set_const(d, (uae_u8)live.state[s].val);
		return;
	}
	const int s_is_d = (s == d);
	if (!s_is_d) {
		s = readreg(s);
		d = writereg(d);
	} else {
		s = d = rmw(s);
	}
	UNSIGNED8_REG_2_REG(d, s);
	if (!s_is_d)
		unlock2(d);
	unlock2(s);
}

void zero_extend_16_rr(W4 d, RR2 s)
{
	if (isconst(s)) {
		set_const(d, (uae_u16)live.state[s].val);
		return;
	}
	const int s_is_d = (s == d);
	if (!s_is_d) {
		s = readreg(s);
		d = writereg(d);
	} else {
		s = d = rmw(s);
	}
	UNSIGNED16_REG_2_REG(d, s);
	if (!s_is_d)
		unlock2(d);
	unlock2(s);
}

void sign_extend_8_rr(W4 d, RR1 s)
{
	if (isconst(s)) {
		set_const(d, (uae_s32)(uae_s8)live.state[s].val);
		return;
	}
	const int s_is_d = (s == d);
	if (!s_is_d) {
		s = readreg(s);
		d = writereg(d);
	} else {
		s = d = rmw(s);
	}
	SIGNED8_REG_2_REG(d, s);
	if (!s_is_d)
		unlock2(d);
	unlock2(s);
}

void test_b_rr(RR1 d, RR1 s)
{
	if (d == s) {
		jff_TST_b(d);
		return;
	}
	if (isconst(d))
		LOAD_U32(REG_WORK1, (uae_u8)live.state[d].val);
	else {
		int rd = readreg(d);
		SIGNED8_REG_2_REG(REG_WORK1, rd);
		unlock2(rd);
	}
	if (isconst(s))
		LOAD_U32(REG_WORK2, (uae_u8)live.state[s].val);
	else {
		int rs = readreg(s);
		SIGNED8_REG_2_REG(REG_WORK2, rs);
		unlock2(rs);
	}
	TST_ww(REG_WORK1, REG_WORK2);
	flags_carry_inverted = false;
}

void test_w_rr(RR2 d, RR2 s)
{
	if (d == s) {
		jff_TST_w(d);
		return;
	}
	if (isconst(d))
		LOAD_U32(REG_WORK1, (uae_u16)live.state[d].val);
	else {
		int rd = readreg(d);
		SIGNED16_REG_2_REG(REG_WORK1, rd);
		unlock2(rd);
	}
	if (isconst(s))
		LOAD_U32(REG_WORK2, (uae_u16)live.state[s].val);
	else {
		int rs = readreg(s);
		SIGNED16_REG_2_REG(REG_WORK2, rs);
		unlock2(rs);
	}
	TST_ww(REG_WORK1, REG_WORK2);
	flags_carry_inverted = false;
}

void test_l_rr(RR4 d, RR4 s)
{
	if (d == s) {
		jff_TST_l(d);
		return;
	}
	if (isconst(d))
		LOAD_U32(REG_WORK1, live.state[d].val);
	else {
		int rd = readreg(d);
		MOV_ww(REG_WORK1, rd);
		unlock2(rd);
	}
	if (isconst(s))
		LOAD_U32(REG_WORK2, live.state[s].val);
	else {
		int rs = readreg(s);
		MOV_ww(REG_WORK2, rs);
		unlock2(rs);
	}
	TST_ww(REG_WORK1, REG_WORK2);
	flags_carry_inverted = false;
}

void test_l_ri(RR4 d, uae_s32 i)
{
	if (isconst(d))
		LOAD_U32(REG_WORK1, live.state[d].val);
	else {
		int rd = readreg(d);
		MOV_ww(REG_WORK1, rd);
		unlock2(rd);
	}
	LOAD_U32(REG_WORK2, (uae_u32)i);
	TST_ww(REG_WORK1, REG_WORK2);
	flags_carry_inverted = false;
}

static inline void legacy_fix_inverted_carry(void)
{
	if (flags_carry_inverted) {
		MRS_NZCV_x(REG_WORK4);
		EOR_xxCflag(REG_WORK4, REG_WORK4);
		MSR_NZCV_x(REG_WORK4);
		flags_carry_inverted = false;
	}
}

static inline void legacy_invert_carry_in_pstate(void)
{
	MRS_NZCV_x(REG_WORK4);
	EOR_xxCflag(REG_WORK4, REG_WORK4);
	MSR_NZCV_x(REG_WORK4);
}

void adc_b(RW1 d, RR1 s)
{
	legacy_fix_inverted_carry();
	INIT_REGS_b(d, s);
	MOV_xi(REG_WORK1, 0);
	BFI_xxii(REG_WORK1, s, 24, 8);
	LSL_wwi(REG_WORK3, d, 24);
	ADCS_www(REG_WORK1, REG_WORK1, REG_WORK3);
	BFXIL_xxii(d, REG_WORK1, 24, 8);
	flags_carry_inverted = false;
	EXIT_REGS(d, s);
}

void adc_w(RW2 d, RR2 s)
{
	legacy_fix_inverted_carry();
	INIT_REGS_w(d, s);
	MOV_xi(REG_WORK1, 0);
	BFI_xxii(REG_WORK1, s, 16, 16);
	LSL_wwi(REG_WORK3, d, 16);
	ADCS_www(REG_WORK1, REG_WORK1, REG_WORK3);
	BFXIL_xxii(d, REG_WORK1, 16, 16);
	flags_carry_inverted = false;
	EXIT_REGS(d, s);
}

void adc_l(RW4 d, RR4 s)
{
	legacy_fix_inverted_carry();
	INIT_REGS_l(d, s);
	ADCS_www(d, d, s);
	flags_carry_inverted = false;
	EXIT_REGS(d, s);
}

void sbb_b(RW1 d, RR1 s)
{
	legacy_fix_inverted_carry();
	legacy_invert_carry_in_pstate();
	INIT_REGS_b(d, s);
	LSL_wwi(REG_WORK1, d, 24);
	LSL_wwi(REG_WORK3, s, 24);
	SBCS_www(REG_WORK1, REG_WORK1, REG_WORK3);
	BFXIL_xxii(d, REG_WORK1, 24, 8);
	flags_carry_inverted = true;
	EXIT_REGS(d, s);
}

void sbb_w(RW2 d, RR2 s)
{
	legacy_fix_inverted_carry();
	legacy_invert_carry_in_pstate();
	INIT_REGS_w(d, s);
	LSL_wwi(REG_WORK1, d, 16);
	LSL_wwi(REG_WORK3, s, 16);
	SBCS_www(REG_WORK1, REG_WORK1, REG_WORK3);
	BFXIL_xxii(d, REG_WORK1, 16, 16);
	flags_carry_inverted = true;
	EXIT_REGS(d, s);
}

void sbb_l(RW4 d, RR4 s)
{
	legacy_fix_inverted_carry();
	legacy_invert_carry_in_pstate();
	INIT_REGS_l(d, s);
	SBCS_www(d, d, s);
	flags_carry_inverted = true;
	EXIT_REGS(d, s);
}

static inline void legacy_load_rr4_to_work(int work_reg, RR4 r)
{
	if (isconst(r)) {
		LOAD_U32(work_reg, live.state[r].val);
	} else {
		int rr = readreg(r);
		MOV_ww(work_reg, rr);
		unlock2(rr);
	}
}

static inline void legacy_set_c_preserve_nzv_from_work1_work2(void)
{
	MRS_NZCV_x(REG_WORK4);
	TST_ww(REG_WORK1, REG_WORK2);
	CSET_xc(REG_WORK3, NATIVE_CC_NE);
	BFI_xxii(REG_WORK4, REG_WORK3, 29, 1);
	MSR_NZCV_x(REG_WORK4);
	flags_carry_inverted = false;
}

void bt_l_rr(RR4 d, RR4 s)
{
	legacy_load_rr4_to_work(REG_WORK1, d);
	legacy_load_rr4_to_work(REG_WORK2, s);
	UBFIZ_xxii(REG_WORK2, REG_WORK2, 0, 5);
	MOV_xi(REG_WORK3, 1);
	LSL_www(REG_WORK2, REG_WORK3, REG_WORK2);
	legacy_set_c_preserve_nzv_from_work1_work2();
}

void bt_l_ri(RR4 d, uae_s32 i)
{
	legacy_load_rr4_to_work(REG_WORK1, d);
	LOAD_U32(REG_WORK2, 1u << (i & 31));
	legacy_set_c_preserve_nzv_from_work1_work2();
}

void btc_l_rr(RW4 d, RR4 s)
{
	legacy_load_rr4_to_work(REG_WORK1, d);
	legacy_load_rr4_to_work(REG_WORK2, s);
	UBFIZ_xxii(REG_WORK2, REG_WORK2, 0, 5);
	MOV_xi(REG_WORK3, 1);
	LSL_www(REG_WORK2, REG_WORK3, REG_WORK2);
	legacy_set_c_preserve_nzv_from_work1_work2();
	jnf_BCHG_l(d, s);
}

void btr_l_rr(RW4 d, RR4 s)
{
	legacy_load_rr4_to_work(REG_WORK1, d);
	legacy_load_rr4_to_work(REG_WORK2, s);
	UBFIZ_xxii(REG_WORK2, REG_WORK2, 0, 5);
	MOV_xi(REG_WORK3, 1);
	LSL_www(REG_WORK2, REG_WORK3, REG_WORK2);
	legacy_set_c_preserve_nzv_from_work1_work2();
	jnf_BCLR_l(d, s);
}

void bts_l_rr(RW4 d, RR4 s)
{
	legacy_load_rr4_to_work(REG_WORK1, d);
	legacy_load_rr4_to_work(REG_WORK2, s);
	UBFIZ_xxii(REG_WORK2, REG_WORK2, 0, 5);
	MOV_xi(REG_WORK3, 1);
	LSL_www(REG_WORK2, REG_WORK3, REG_WORK2);
	legacy_set_c_preserve_nzv_from_work1_work2();
	jnf_BSET_l(d, s);
}

void setcc(W1 d, uae_s32 cc)
{
	d = writereg(d);
	CSET_xc(d, legacy_x86_cc_to_native(cc));
	unlock2(d);
}

void cmov_l_rr(RW4 d, RR4 s, uae_s32 cc)
{
	if (d == s)
		return;
	d = rmw(d);
	if (isconst(s)) {
		LOAD_U32(REG_WORK1, live.state[s].val);
		CSEL_xxxc(d, REG_WORK1, d, legacy_x86_cc_to_native(cc));
	} else {
		s = readreg(s);
		CSEL_xxxc(d, s, d, legacy_x86_cc_to_native(cc));
		unlock2(s);
	}
	unlock2(d);
}

void mov_l_rR(W4 d, RR4 s, uae_s32 offset)
{
	d = writereg(d);
	if (isconst(s)) {
		LOAD_U64(REG_WORK1, (uintptr)(live.state[s].val + (uae_s32)offset));
		LDR_wXi(d, REG_WORK1, 0);
		unlock2(d);
		return;
	}
	const int base = readreg(s);
	const int addr = legacy_addr_with_offset(base, offset);
	LDR_wXi(d, addr, 0);
	unlock2(base);
	unlock2(d);
}

void mov_w_rR(W2 d, RR4 s, uae_s32 offset)
{
	d = writereg(d);
	if (isconst(s)) {
		LOAD_U64(REG_WORK1, (uintptr)(live.state[s].val + (uae_s32)offset));
		LDRH_wXi(d, REG_WORK1, 0);
		unlock2(d);
		return;
	}
	const int base = readreg(s);
	const int addr = legacy_addr_with_offset(base, offset);
	LDRH_wXi(d, addr, 0);
	unlock2(base);
	unlock2(d);
}

void mov_l_Rr(RR4 d, RR4 s, uae_s32 offset)
{
	int src = isconst(s) ? REG_WORK1 : readreg(s);
	if (isconst(s))
		LOAD_U32(src, live.state[s].val);
	if (isconst(d)) {
		LOAD_U64(REG_WORK2, (uintptr)(live.state[d].val + (uae_s32)offset));
		STR_wXi(src, REG_WORK2, 0);
	} else {
		const int base = readreg(d);
		const int addr = legacy_addr_with_offset_avoid(base, offset, src);
		STR_wXi(src, addr, 0);
		unlock2(base);
	}
	if (!isconst(s))
		unlock2(src);
}

void mov_w_Rr(RR4 d, RR2 s, uae_s32 offset)
{
	int src = isconst(s) ? REG_WORK1 : readreg(s);
	if (isconst(s))
		LOAD_U32(src, (uae_u16)live.state[s].val);
	if (isconst(d)) {
		LOAD_U64(REG_WORK2, (uintptr)(live.state[d].val + (uae_s32)offset));
		STRH_wXi(src, REG_WORK2, 0);
	} else {
		const int base = readreg(d);
		const int addr = legacy_addr_with_offset_avoid(base, offset, src);
		STRH_wXi(src, addr, 0);
		unlock2(base);
	}
	if (!isconst(s))
		unlock2(src);
}

void mid_bswap_16(RW2 r)
{
	if (isconst(r)) {
		set_const(r, (uae_u16)do_byteswap_16((uae_u16)live.state[r].val));
		return;
	}
	r = rmw(r);
	REV16_ww(r, r);
	unlock2(r);
}

void mid_bswap_32(RW4 r)
{
	if (isconst(r)) {
		set_const(r, do_byteswap_32((uae_u32)live.state[r].val));
		return;
	}
	r = rmw(r);
	REV32_xx(r, r);
	MOV_ww(r, r);
	unlock2(r);
}

void imul_32_32(RW4 d, RR4 s) { if (legacy_needflags_enabled()) jff_MULS32(d, s); else jnf_MULS32(d, s); }
void imul_64_32(RW4 d, RW4 s) { if (legacy_needflags_enabled()) jff_MULS64(d, s); else jnf_MULS64(d, s); }
void mul_64_32(RW4 d, RW4 s) { if (legacy_needflags_enabled()) jff_MULU64(d, s); else jnf_MULU64(d, s); }

void shra_b_ri(RW1 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_ASR_b_imm(d, i); else jnf_ASR_b_imm(d, i); }
void shra_w_ri(RW2 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_ASR_w_imm(d, i); else jnf_ASR_w_imm(d, i); }
void shra_l_ri(RW4 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_ASR_l_imm(d, i); else jnf_ASR_l_imm(d, i); }
void shra_b_rr(RW1 d, RR1 r) { if (legacy_needflags_enabled()) jff_ASR_b_reg(d, r); else jnf_ASR_b_reg(d, r); }
void shra_w_rr(RW2 d, RR1 r) { if (legacy_needflags_enabled()) jff_ASR_w_reg(d, r); else jnf_ASR_w_reg(d, r); }
void shra_l_rr(RW4 d, RR1 r) { if (legacy_needflags_enabled()) jff_ASR_l_reg(d, r); else jnf_ASR_l_reg(d, r); }
void shrl_b_ri(RW1 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_LSR_b_imm(d, i); else jnf_LSR_b_imm(d, i); }
void shrl_w_ri(RW2 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_LSR_w_imm(d, i); else jnf_LSR_w_imm(d, i); }
void shrl_l_ri(RW4 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_LSR_l_imm(d, i); else jnf_LSR_l_imm(d, i); }
void shrl_b_rr(RW1 d, RR1 r) { if (legacy_needflags_enabled()) jff_LSR_b_reg(d, r); else jnf_LSR_b_reg(d, r); }
void shrl_w_rr(RW2 d, RR1 r) { if (legacy_needflags_enabled()) jff_LSR_w_reg(d, r); else jnf_LSR_w_reg(d, r); }
void shrl_l_rr(RW4 d, RR1 r) { if (legacy_needflags_enabled()) jff_LSR_l_reg(d, r); else jnf_LSR_l_reg(d, r); }
void shll_b_ri(RW1 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_LSL_b_imm(d, i); else jnf_LSL_b_imm(d, i); }
void shll_w_ri(RW2 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_LSL_w_imm(d, i); else jnf_LSL_w_imm(d, i); }
void shll_l_ri(RW4 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_LSL_l_imm(d, i); else jnf_LSL_l_imm(d, i); }
void shll_b_rr(RW1 d, RR1 r) { if (legacy_needflags_enabled()) jff_LSL_b_reg(d, r); else jnf_LSL_b_reg(d, r); }
void shll_w_rr(RW2 d, RR1 r) { if (legacy_needflags_enabled()) jff_LSL_w_reg(d, r); else jnf_LSL_w_reg(d, r); }
void shll_l_rr(RW4 d, RR1 r) { if (legacy_needflags_enabled()) jff_LSL_l_reg(d, r); else jnf_LSL_l_reg(d, r); }
void rol_b_rr(RW1 d, RR1 r) { if (legacy_needflags_enabled()) jff_ROL_b(d, r); else jnf_ROL_b(d, r); }
void rol_w_rr(RW2 d, RR1 r) { if (legacy_needflags_enabled()) jff_ROL_w(d, r); else jnf_ROL_w(d, r); }
void rol_l_rr(RW4 d, RR1 r) { if (legacy_needflags_enabled()) jff_ROL_l(d, r); else jnf_ROL_l(d, r); }
void rol_l_ri(RW4 d, uae_s32 i) { if (legacy_needflags_enabled()) jff_ROL_l_imm(d, i); else jnf_ROL_l_imm(d, i); }
void ror_b_rr(RW1 d, RR1 r) { if (legacy_needflags_enabled()) jff_ROR_b(d, r); else jnf_ROR_b(d, r); }
void ror_w_rr(RW2 d, RR1 r) { if (legacy_needflags_enabled()) jff_ROR_w(d, r); else jnf_ROR_w(d, r); }
void ror_l_rr(RW4 d, RR1 r) { if (legacy_needflags_enabled()) jff_ROR_l(d, r); else jnf_ROR_l(d, r); }

void setcc_for_cntzero(RR4 cnt, RR4 data, int size)
{
	if (isconst(cnt) && live.state[cnt].val == 0) {
		switch (size) {
		case 1: jff_TST_b(data); break;
		case 2: jff_TST_w(data); break;
		default: jff_TST_l(data); break;
		}
	}
}

void set_zero(int r, int tmp)
{
	(void)tmp;
	MRS_NZCV_x(REG_WORK1);
	CLEAR_xxZflag(REG_WORK1, REG_WORK1);
	if (isconst(r)) {
		if ((uae_u32)live.state[r].val == 0)
			SET_xxZflag(REG_WORK1, REG_WORK1);
	} else {
		int rr = readreg(r);
		CBNZ_wi(rr, 2); /* skip next if non-zero */
		SET_xxZflag(REG_WORK1, REG_WORK1);
		unlock2(rr);
	}
	MSR_NZCV_x(REG_WORK1);
	flags_carry_inverted = false;
}

int kill_rodent(int r)
{
	(void)r;
	return 0;
}

extern "C" void Uae2026JitSyncRamToShadow(void);

void do_nothing(void)
{
#if defined(CPU_AARCH64)
	jit_diag_do_nothing_calls++;
	jit_diag_dispatch_count++;
	if (countdown < 0)
		cpu_do_check_ticks();
	countdown = 10000000;
	if (quit_program > 0)
		return;
	{
		static unsigned long dn_count = 0;
		dn_count++;
		if (dn_count <= 5 || dn_count % 50000 == 0) {
			fprintf(stderr, "DN[%lu] pc=%08x im=%u spc=%x d0=%08x a7=%08x\n",
				dn_count, m68k_getpc(), (unsigned)regs.intmask,
				(unsigned)regs.spcflags, regs.regs[0], regs.regs[15]);
		}
	}
	MakeSR();
	m68k_do_specialties();
#endif
}

static bool jit_tracewin_enabled()
{
	static int cached = -1;
	if (cached < 0)
		cached = (getenv("B2_TRACE_PC_START") && *getenv("B2_TRACE_PC_START")) ? 1 : 0;
	return cached != 0;
}

static uae_u32 jit_tracewin_start()
{
	static uae_u32 value = 0;
	static bool init = false;
	if (!init) {
		const char *env = getenv("B2_TRACE_PC_START");
		value = env && *env ? (uae_u32)strtoul(env, NULL, 0) : 0;
		init = true;
	}
	return value;
}

static uae_u32 jit_tracewin_end()
{
	static uae_u32 value = 0xffffffffu;
	static bool init = false;
	if (!init) {
		const char *env = getenv("B2_TRACE_PC_END");
		value = env && *env ? (uae_u32)strtoul(env, NULL, 0) : 0xffffffffu;
		init = true;
	}
	return value;
}

static unsigned long jit_tracewin_limit()
{
	static unsigned long value = 200;
	static bool init = false;
	if (!init) {
		const char *env = getenv("B2_TRACE_LIMIT");
		value = env && *env ? strtoul(env, NULL, 0) : 200;
		init = true;
	}
	return value;
}

static bool jit_trace_after_table_env()
{
	static int cached = -1;
	if (cached < 0)
		cached = (getenv("B2_TRACE_AFTER_TABLE") && *getenv("B2_TRACE_AFTER_TABLE") && strcmp(getenv("B2_TRACE_AFTER_TABLE"), "0") != 0) ? 1 : 0;
	return cached != 0;
}

static bool jit_tracewin_match(uae_u32 pc)
{
	if (!jit_tracewin_enabled() || pc < jit_tracewin_start() || pc > jit_tracewin_end())
		return false;
	if (jit_trace_after_table_env() && !basilisk_trace_after_table_ready)
		return false;
	return true;
}

static bool jit_pctrace_match(uae_u32 pc)
{
	if (jit_tracewin_enabled() && (pc < jit_tracewin_start() || pc > jit_tracewin_end()))
		return false;
	if (jit_trace_after_table_env() && !basilisk_trace_after_table_ready)
		return false;
	return true;
}

static bool jit_trace_table_enabled()
{
	static int cached = -1;
	if (cached < 0)
		cached = (getenv("B2_TRACE_TABLE") && *getenv("B2_TRACE_TABLE") && strcmp(getenv("B2_TRACE_TABLE"), "0") != 0) ? 1 : 0;
	return cached != 0;
}

static void jit_trace_table_maybe_dump_complete(const char *tag, unsigned long step, uae_u32 pc)
{
	static int dumped = 0;
	static int cfg_init = 0;
	static char dump_path[512];
	if (!cfg_init) {
		const char *env = getenv("B2_TRACE_TABLE_DUMP_PATH");
		dump_path[0] = 0;
		if (env && *env) {
			strncpy(dump_path, env, sizeof(dump_path) - 1);
			dump_path[sizeof(dump_path) - 1] = 0;
		}
		cfg_init = 1;
	}
	if (dumped || !dump_path[0])
		return;
	unsigned a1 = (unsigned)regs.regs[9];
	if (a1 >= 0x1e00 && a1 < 0x1e40)
		basilisk_trace_after_table_ready = true;
	if (a1 < 0x1e00 || a1 >= 0x1e40)
		return;
	FILE *f = fopen(dump_path, "wb");
	if (!f)
		return;
	for (uaecptr addr = 0x0e00; addr < 0x1e00; addr++)
		fputc((int)get_byte(addr), f);
	fclose(f);
	dumped = 1;
	fprintf(stderr, "%s_DUMP step=%lu pc=%08x a1=%08x path=%s\n", tag, step, (unsigned)pc, (unsigned)regs.regs[9], dump_path);
}

static void jit_trace_lowmem400_maybe_dump(unsigned long step, uae_u32 pc)
{
	static int dumped = 0;
	static int cfg_init = 0;
	static char dump_path[512];
	if (!cfg_init) {
		const char *env = getenv("B2_TRACE_LOWMEM400_DUMP_PATH");
		dump_path[0] = 0;
		if (env && *env) {
			strncpy(dump_path, env, sizeof(dump_path) - 1);
			dump_path[sizeof(dump_path) - 1] = 0;
		}
		cfg_init = 1;
	}
	if (dumped || !dump_path[0])
		return;
	if (pc < 0x040099f0 || pc > 0x04009a30)
		return;
	FILE *f = fopen(dump_path, "wb");
	if (!f)
		return;
	for (uaecptr addr = 0x0400; addr < 0x0800; addr++)
		fputc((int)get_byte(addr), f);
	fclose(f);
	dumped = 1;
	fprintf(stderr, "LOWMEM400_DUMP step=%lu pc=%08x d1=%08x path=%s\n", step, (unsigned)pc, (unsigned)regs.regs[1], dump_path);
}

static void jit_trace_table_log(const char *tag, unsigned long step, uae_u32 pc)
{
	if (!jit_trace_table_enabled())
		return;
	fprintf(stderr,
		"%s step=%lu pc=%08x a1=%08x e00=%08x e04=%08x e08=%08x e0c=%08x e10=%08x e14=%08x e18=%08x e1c=%08x e20=%08x e24=%08x e28=%08x e2c=%08x e30=%08x e34=%08x e38=%08x e3c=%08x\n",
		tag,
		step,
		(unsigned)pc,
		(unsigned)regs.regs[9],
		(unsigned)get_long(0x0e00),
		(unsigned)get_long(0x0e04),
		(unsigned)get_long(0x0e08),
		(unsigned)get_long(0x0e0c),
		(unsigned)get_long(0x0e10),
		(unsigned)get_long(0x0e14),
		(unsigned)get_long(0x0e18),
		(unsigned)get_long(0x0e1c),
		(unsigned)get_long(0x0e20),
		(unsigned)get_long(0x0e24),
		(unsigned)get_long(0x0e28),
		(unsigned)get_long(0x0e2c),
		(unsigned)get_long(0x0e30),
		(unsigned)get_long(0x0e34),
		(unsigned)get_long(0x0e38),
		(unsigned)get_long(0x0e3c));
	jit_trace_table_maybe_dump_complete(tag, step, pc);
}

void exec_nostats(void)
{
#if defined(CPU_AARCH64)
	jit_diag_exec_nostats_calls++;
	jit_diag_dispatch_count++;
	jit_diag_maybe_print();
	jit_canonicalize_code_pc_if_ram_mmu();
	{
		uintptr pcp = (uintptr)regs.pc_p;
		uintptr ram_base = (uintptr)RAMBaseHost;
		uintptr ram_limit = ram_base + RAMSize;
		uintptr rom_base = (uintptr)ROMBaseHost;
		uintptr rom_limit = rom_base + ROMSize;
		uintptr low_rom_base = rom_base - (uintptr)ROMBaseMac;
		uintptr low_rom_limit = low_rom_base + ROMSize;
		bool valid_host_pc = ((pcp >= ram_base && pcp < ram_limit) ||
		                      (pcp >= rom_base && pcp < rom_limit) ||
		                      (pcp >= low_rom_base && pcp < low_rom_limit));
		if (!valid_host_pc || (pcp & 1)) {
			static int bad_count = 0;
			uae_u32 safe_pc = regs.pc & ~1u;
			/* If safe_pc looks like 24-bit sign-extended ROM address
			   (0xFFxxxxxx with underlying 0x008xxxxx), mask to 24 bits. */
			if ((safe_pc & 0xFF000000) == 0xFF000000 && (safe_pc & 0x00800000))
				safe_pc &= 0x00FFFFFF;
			if (bad_count++ < 50)
				fprintf(stderr, "JIT: exec_nostats bad pc_p=%p regs.pc=%08x d0=%08x d1=%08x a0=%08x a1=%08x a2=%08x a7=%08x sr=%04x spc=%08x oldp=%p last_setpc=%p last_kind=%u last_seq=%lu\n",
					(void*)regs.pc_p, regs.pc,
					regs.regs[0], regs.regs[1], regs.regs[8], regs.regs[9], regs.regs[10], regs.regs[15],
					(unsigned)regs.sr, (unsigned)regs.spcflags, (void*)regs.pc_oldp,
					(void*)jit_last_setpc_value, (unsigned)jit_last_setpc_kind, jit_last_setpc_seq);
			/* Re-derive pc_p from guest PC.  In RAM/MMU mode this must use
			 * code-space MMU translation; get_real_address() is a direct shadow
			 * lookup and maps low user virtual PCs to stale ROM-overlay bytes.
			 * Publish the target first because the code translation itself can
			 * fault and must restart from safe_pc, not the previous instruction. */
			regs.pc = safe_pc;
			regs.fault_pc = safe_pc;
			Uae2026JitLastInstructionPc = safe_pc;
			if (jit_allow_ram_dispatch_env() && (regs.mmu_enabled || Uae2026OpcodeTestModeActive()))
				regs.pc_p = (uae_u8 *)Uae2026JitMmuXlateCodeHost(safe_pc);
			else
				regs.pc_p = get_real_address(safe_pc, 0, sz_word);
			regs.pc_oldp = regs.pc_p;
		}
	}
#endif
	static unsigned long trace_count = 0;
	for (;;) {
#if defined(CPU_AARCH64)
		jit_maybe_apply_runtime_helpers();
		if (SPCFLAGS_TEST(SPCFLAG_ALL))
			return;
#endif
		uae_u32 before_pc = m68k_getpc();
		uae_u32 opcode = jit_fetch_opcode_for_current_pc(before_pc);
		Uae2026JitMmuTxnCommit();
		Uae2026JitPublishFallbackState(before_pc, opcode);
		if (Uae2026OpcodeTestModeActive() && (uae_u16)opcode == 0x4e72u) {
			m68k_setpc(before_pc);
			Uae2026OpcodeTestModeHandleStopTrailer();
			return;
		}
		if (legacy_ram_direct_movem_long_predec(before_pc, (uae_u16)opcode)) {
			jit_interpreted_op_check_ticks();
			if (SPCFLAGS_TEST(SPCFLAG_ALL))
				return;
			continue;
		}
		{
			uae_u32 retpc = 0;
			if ((legacy_rom_delay_bsr_callsite(before_pc, (uae_u16)opcode, &retpc) &&
				 jit_op_rom_delay_bsr_callsite(before_pc, retpc)) ||
				(legacy_rom_vbr_lookup_bsr_callsite(before_pc, (uae_u16)opcode, &retpc) &&
				 jit_op_rom_vbr_global_lookup_callsite(before_pc, retpc)) ||
				(legacy_rom_rtc_write_bsr_callsite(before_pc, (uae_u16)opcode, &retpc) &&
				 jit_op_rom_rtc_write_byte_callsite(before_pc, retpc)) ||
				(legacy_rom_rtc_read_bsr_callsite(before_pc, (uae_u16)opcode, &retpc) &&
				 jit_op_rom_rtc_read_byte_callsite(before_pc, retpc))) {
				jit_interpreted_op_check_ticks();
				if (SPCFLAGS_TEST(SPCFLAG_ALL))
					return;
				continue;
			}
		}
		if (legacy_call_push_txn_opcode(before_pc, (uae_u16)opcode))
			legacy_maybe_begin_call_push_txn(before_pc, (uae_u16)opcode);
		else
			legacy_maybe_begin_return_pop_txn(before_pc, (uae_u16)opcode);
		bool trace_this = trace_count < jit_tracewin_limit() && jit_tracewin_match(before_pc);
		if (trace_this) {
			fprintf(stderr,
				"TRACEWINJ BEFORE step=%lu pc=%08x op=%04x regs.pc=%08x pc_p=%p oldp=%p d0=%08x d1=%08x a0=%08x a1=%08x a2=%08x a7=%08x sr=%04x nzcv=%08x x=%08x\n",
				trace_count + 1,
				(unsigned)before_pc,
				(unsigned)opcode,
				(unsigned)regs.pc,
				(void*)regs.pc_p,
				(void*)regs.pc_oldp,
				(unsigned)regs.regs[0],
				(unsigned)regs.regs[1],
				(unsigned)regs.regs[8],
				(unsigned)regs.regs[9],
				(unsigned)regs.regs[10],
				(unsigned)regs.regs[15],
				(unsigned)regs.sr,
				(unsigned)regflags.nzcv,
				(unsigned)regflags.x);
			jit_trace_table_log("TRACEWINJTAB", trace_count + 1, before_pc);
		}
		(*cpufunctbl[opcode])(opcode);
		Uae2026JitCanonicalizePcAfterFallback();
		if (trace_this) {
			uae_u32 after_pc = m68k_getpc();
			trace_count++;
			fprintf(stderr,
				"TRACEWINJ AFTER step=%lu pc=%08x op=%04x regs.pc=%08x pc_p=%p oldp=%p d0=%08x d1=%08x a0=%08x a1=%08x a2=%08x a7=%08x sr=%04x nzcv=%08x x=%08x\n",
				trace_count,
				(unsigned)after_pc,
				(unsigned)opcode,
				(unsigned)regs.pc,
				(void*)regs.pc_p,
				(void*)regs.pc_oldp,
				(unsigned)regs.regs[0],
				(unsigned)regs.regs[1],
				(unsigned)regs.regs[8],
				(unsigned)regs.regs[9],
				(unsigned)regs.regs[10],
				(unsigned)regs.regs[15],
				(unsigned)regs.sr,
				(unsigned)regflags.nzcv,
				(unsigned)regflags.x);
			jit_trace_table_log("TRACEWINJTAB", trace_count, after_pc);
		}
		jit_interpreted_op_check_ticks();
		if (end_block(opcode) || SPCFLAGS_TEST(SPCFLAG_ALL))
			return;
	}
}

void execute_normal(void)
{
#if defined(CPU_AARCH64)
	jit_diag_execute_normal_calls++;
	jit_diag_dispatch_count++;
	/* If quit_program is set (e.g. M68K_EXEC_RETURN), skip everything
	   and return immediately. Running further instructions would
	   execute random memory past the test code boundary. */
	if (quit_program > 0)
		return;
	/* Handle pending interrupts on every execute_normal entry. */
	if (__atomic_load_n(&regs.spcflags, __ATOMIC_ACQUIRE) & SPCFLAG_ALL) {
		MakeSR();
		m68k_do_specialties();
	}

	jit_diag_maybe_print();
	jit_canonicalize_code_pc_if_ram_mmu();
	/* If pc_p is outside valid Mac memory range (corrupt), re-derive it. */
	{
		uintptr pcp = (uintptr)regs.pc_p;
		uintptr ram_base = (uintptr)RAMBaseHost;
		uintptr ram_limit = ram_base + RAMSize;
		uintptr rom_base = (uintptr)ROMBaseHost;
		uintptr rom_limit = rom_base + ROMSize;
		uintptr low_rom_base = rom_base - (uintptr)ROMBaseMac;
		uintptr low_rom_limit = low_rom_base + ROMSize;
		bool valid_host_pc = ((pcp >= ram_base && pcp < ram_limit) ||
		                      (pcp >= rom_base && pcp < rom_limit) ||
		                      (pcp >= low_rom_base && pcp < low_rom_limit));
		if (!valid_host_pc || (pcp & 1)) {
			static int fix_count = 0;
			uae_u32 safe_pc = regs.pc & ~1u;
			/* If safe_pc looks like 24-bit sign-extended ROM address
			   (0xFFxxxxxx with underlying 0x008xxxxx), mask to 24 bits. */
			if ((safe_pc & 0xFF000000) == 0xFF000000 && (safe_pc & 0x00800000))
				safe_pc &= 0x00FFFFFF;
			if (fix_count++ < 50)
				fprintf(stderr, "JIT: exec_normal bad pc_p=%p regs.pc=%08x safe=%08x "
					"d0=%08x d1=%08x a0=%08x a7=%08x sr=%04x spc=%08x oldp=%p "
					"isp=%08x msp=%08x s=%d m=%d\n",
					(void*)regs.pc_p, regs.pc, safe_pc,
					regs.regs[0], regs.regs[1], regs.regs[8], regs.regs[15],
					(unsigned)regs.sr, (unsigned)regs.spcflags, (void*)regs.pc_oldp,
					(unsigned)regs.isp, (unsigned)regs.msp, regs.s, regs.m);
			/* Check if the guest address is in valid executable memory:
			   - Basilisk-style RAM: 0 <= pc < RAMSize
			   - Previous RAM: 0x04000000 <= pc < 0x04000000 + RAMSize
			   - ROM: ROMBaseMac <= pc < ROMBaseMac + ROMSize
			   Anything else (NuBus space, frame buffer, unmapped) is a bus error. */
			const bool mmu_code_path = jit_allow_ram_dispatch_env() && (regs.mmu_enabled || Uae2026OpcodeTestModeActive());
			bool valid_mac_pc = (safe_pc < (uae_u32)RAMSize) ||
				(safe_pc >= 0x04000000u && safe_pc < 0x04000000u + (uae_u32)RAMSize) ||
				(safe_pc >= (uae_u32)ROMBaseMac && safe_pc < (uae_u32)(ROMBaseMac + ROMSize));
			if (!valid_mac_pc && !mmu_code_path) {
				/* Guest PC points to unmapped memory (e.g. NuBus slot probe).
				   Generate a bus error exception to let the ROM's handler
				   deal with it, just like real hardware would. */
				static int buserr_count = 0;
				if (buserr_count++ < 10)
					fprintf(stderr, "JIT: bus error for unmapped PC=%08x a7=%08x isp=%08x (triggering Exception 2)\n",
						safe_pc, m68k_areg(regs, 7), regs.isp);
				/* Restore a7 from ISP/MSP — the JIT may have desynchronized them */
				if (regs.s && !regs.m && regs.isp >= 0x1000)
					m68k_areg(regs, 7) = regs.isp;
				else if (regs.s && regs.m && regs.msp >= 0x1000)
					m68k_areg(regs, 7) = regs.msp;
				Exception(2, safe_pc);
				return;
			}
			/* Valid Mac address — re-derive pc_p from the guest PC.  In RAM/MMU
			 * mode this must use code-space MMU translation; get_real_address()
			 * maps low user virtual PCs to stale ROM-overlay shadow bytes.  Do
			 * not pre-filter virtual PCs through direct physical windows when
			 * the 040 MMU is active; let the code translation decide validity. */
			regs.pc = safe_pc;
			regs.fault_pc = safe_pc;
			Uae2026JitLastInstructionPc = safe_pc;
			if (mmu_code_path)
				regs.pc_p = (uae_u8 *)Uae2026JitMmuXlateCodeHost(safe_pc);
			else
				regs.pc_p = get_real_address(safe_pc, 0, sz_word);
			regs.pc_oldp = regs.pc_p;
		}
	}
#endif
	if (!check_for_cache_miss()) {
		cpu_history pc_hist[MAXRUN];
		memset(pc_hist, 0, sizeof(pc_hist));
		int blocklen = 0;
		int total_cycles = 0;
		/* Match the original UAE compiler model: start_pc is the guest base
		 * address and start_pc_p is the host pointer corresponding to that base.
		 * Using the current pc_p as both base and moving PC pointer makes compiled
		 * fall-through PCs drift by host-pointer deltas. */
		start_pc_p = regs.pc_oldp;
		start_pc = regs.pc;
#if defined(CPU_AARCH64)
		jit_lowpc_compile_trace_begin();
		{
			uae_u32 trace_a1 = regs.regs[9];
			if (trace_a1 >= 0x1e00 && trace_a1 < 0x1e40)
				basilisk_trace_after_table_ready = true;
			static unsigned long pctrace_count = 0;
			static unsigned long pctrace_limit = 0;
			static bool pctrace_init = false;
			if (!pctrace_init) {
				const char *env = getenv("B2_JIT_PCTRACE");
				pctrace_limit = env ? strtoul(env, NULL, 10) : 0;
				pctrace_init = true;
			}
			uae_u32 pc = m68k_getpc();
			if (pctrace_limit && pctrace_count < pctrace_limit && jit_pctrace_match(pc)) {
				static unsigned long pctrace_words = 0;
				static bool pctrace_words_init = false;
				if (!pctrace_words_init) {
					const char *env = getenv("B2_JIT_PCTRACE_WORDS");
					pctrace_words = env ? strtoul(env, NULL, 10) : 0;
					pctrace_words_init = true;
				}
				static int pctrace_stack = -1;
				static int pctrace_mem = -1;
				static int pctrace_live_words = -1;
				if (pctrace_stack < 0) {
					const char *env = getenv("B2_JIT_PCTRACE_STACK");
					pctrace_stack = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
				}
				if (pctrace_mem < 0) {
					const char *env = getenv("B2_JIT_PCTRACE_MEM");
					pctrace_mem = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
				}
				if (pctrace_live_words < 0) {
					const char *env = getenv("B2_JIT_PCTRACE_LIVE");
					pctrace_live_words = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
				}
				unsigned long current_step = pctrace_count++;
				uintptr_t pcp_phys = (jit_MEMBaseDiff && (uintptr_t)regs.pc_p >= jit_MEMBaseDiff) ? ((uintptr_t)regs.pc_p - jit_MEMBaseDiff) : 0xffffffffu;
				fprintf(stderr, "PCTRACE %lu %08x phys=%08x pc_p=%p oldp=%p d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x a5=%08x a6=%08x a7=%08x sr=%04x nzcv=%08x x=%08x\n",
					current_step, pc, (unsigned)pcp_phys, (void*)regs.pc_p, (void*)regs.pc_oldp,
					regs.regs[0], regs.regs[1], regs.regs[2], regs.regs[3],
					regs.regs[4], regs.regs[5], regs.regs[6], regs.regs[7],
					regs.regs[8], regs.regs[9], regs.regs[10], regs.regs[11],
					regs.regs[12], regs.regs[13], regs.regs[14], regs.regs[15],
					(unsigned)regs.sr, regflags.nzcv, regflags.x);
				jit_trace_table_log("PCTTABLE", current_step, pc);
				jit_trace_lowmem400_maybe_dump(current_step, pc);
				if (pctrace_stack) {
					uaecptr sp = m68k_areg(regs, 7);
					fprintf(stderr,
						"PCTSTACK %08x sm4=%08x s0=%08x s4=%08x s8=%08x\n",
						pc,
						(unsigned)get_long(sp - 4),
						(unsigned)get_long(sp + 0),
						(unsigned)get_long(sp + 4),
						(unsigned)get_long(sp + 8));
				}
				if (pctrace_mem) {
					uaecptr a0v = m68k_areg(regs, 0);
					uaecptr a3v = m68k_areg(regs, 3);
					fprintf(stderr,
						"PCTMEM %08x m1e4=%08x m1e8=%08x m20c=%08x m40b0334=%08x m40b62c4=%08x m40b62ec=%08x "
						"vec24=%08x vec25=%08x vec26=%08x vec27=%08x vec28=%08x vec29=%08x vec30=%08x vec31=%08x "
						"ma0m4=%08x ma3=%08x ma3p4=%08x\n",
						pc,
						(unsigned)get_long(0x1e4),
						(unsigned)get_long(0x1e8),
						(unsigned)get_long(0x20c),
						(unsigned)get_long(0x040b0334),
						(unsigned)get_long(0x040b62c4),
						(unsigned)get_long(0x040b62ec),
						(unsigned)get_long(regs.vbr + 4 * 24),
						(unsigned)get_long(regs.vbr + 4 * 25),
						(unsigned)get_long(regs.vbr + 4 * 26),
						(unsigned)get_long(regs.vbr + 4 * 27),
						(unsigned)get_long(regs.vbr + 4 * 28),
						(unsigned)get_long(regs.vbr + 4 * 29),
						(unsigned)get_long(regs.vbr + 4 * 30),
						(unsigned)get_long(regs.vbr + 4 * 31),
						(unsigned)get_long(a0v >= 4 ? a0v - 4 : a0v),
						(unsigned)get_long(a3v),
						(unsigned)get_long(a3v + 4));
				}
				if (pctrace_words) {
					if (pctrace_words > 12)
						pctrace_words = 12;
					fprintf(stderr, "PCTOPS %08x", pc);
					for (unsigned long wi = 0; wi < pctrace_words; wi++) {
						uae_u16 w = get_iword((int)(wi * 2));
						fprintf(stderr, " w%lu=%04x", wi, (unsigned)w);
					}
					fprintf(stderr, "\n");
					if (regs.pc_p) {
						fprintf(stderr, "PCTSHADOW %08x", pc);
						for (unsigned long wi = 0; wi < pctrace_words; wi++) {
							uae_u8 *p = regs.pc_p + wi * 2;
							uae_u16 w = ((uae_u16)p[0] << 8) | p[1];
							fprintf(stderr, " w%lu=%04x", wi, (unsigned)w);
						}
						fprintf(stderr, "\n");
					}
					if (pctrace_live_words && pcp_phys != 0xffffffffu) {
						fprintf(stderr, "PCTLIVE %08x", pc);
						for (unsigned long wi = 0; wi < pctrace_words; wi++)
							fprintf(stderr, " w%lu=%04x", wi, (unsigned)Uae2026JitLiveGetWord((uae_u32)pcp_phys + (uae_u32)(wi * 2)));
						fprintf(stderr, "\n");
					}
				}
			}
		}
#endif
#if defined(CPU_AARCH64)
		/* Inhibit one_tick() during block tracing. The tick thread's
		   one_tick() has side effects (incrementing Ticks, SDL events)
		   that happen during interpreter tracing but NOT during native
		   block execution. This asymmetry causes different execution paths.
		   Inhibiting during tracing (typically <64 instructions, ~microseconds)
		   has negligible impact on 60Hz timing accuracy. */
		/* ARM64: tick_inhibit was previously set during block tracing to
		   prevent one_tick() side effects from causing non-deterministic
		   paths. However, with B2_JIT_MAXRUN=1, every instruction traces
		   individually, and the inhibit starves the 60Hz timer, preventing
		   boot progress. Allow one_tick() during tracing. */
		extern bool tick_inhibit;
		/* Do NOT inhibit ticks during tracing. With MAXRUN=1 every dispatch
		   traces one instruction; inhibiting here starves the 60Hz timer
		   and prevents the Mac OS Device Manager from completing async I/O. */
		/* tick_inhibit = false; — already false from compile_block */
		uae_u32 verify_block_pc = m68k_getpc();
		const bool verify_this_block = !jit_block_verify_reentrant && jit_verify_block_target_pc(verify_block_pc);
		if (verify_this_block)
			jit_block_verify_entry_capture(verify_block_pc);
#endif
		for (;;) {
			uae_u16 *pre_fetch_pcp = (uae_u16 *)regs.pc_p;
			pc_hist[blocklen++].location = pre_fetch_pcp;
			int trace_index = blocklen - 1;
			uae_u32 pc_before_op = m68k_getpc();
			uae_u32 opcode = jit_fetch_opcode_for_current_pc(pc_before_op);
#if defined(CPU_AARCH64)
			jit_lowpc_compile_trace_record(trace_index, pc_before_op, opcode, (uintptr)pre_fetch_pcp, (uintptr)regs.pc_p);
#endif
			Uae2026JitMmuTxnCommit();
			Uae2026JitPublishFallbackState(pc_before_op, opcode);
			if (Uae2026OpcodeTestModeActive() && (uae_u16)opcode == 0x4e72u) {
				m68k_setpc(pc_before_op);
				Uae2026OpcodeTestModeHandleStopTrailer();
				return;
			}
			if (legacy_ram_direct_movem_long_predec(pc_before_op, (uae_u16)opcode)) {
				jit_interpreted_op_check_ticks();
				total_cycles += 4 * CYCLE_UNIT;
				return;
			}
			uae_u32 delay_retpc = 0;
			bool helper_callsite = (legacy_rom_delay_bsr_callsite(pc_before_op, (uae_u16)opcode, &delay_retpc) &&
				jit_op_rom_delay_bsr_callsite(pc_before_op, delay_retpc)) ||
				(legacy_rom_vbr_lookup_bsr_callsite(pc_before_op, (uae_u16)opcode, &delay_retpc) &&
				jit_op_rom_vbr_global_lookup_callsite(pc_before_op, delay_retpc)) ||
				(legacy_rom_rtc_write_bsr_callsite(pc_before_op, (uae_u16)opcode, &delay_retpc) &&
				jit_op_rom_rtc_write_byte_callsite(pc_before_op, delay_retpc)) ||
				(legacy_rom_rtc_read_bsr_callsite(pc_before_op, (uae_u16)opcode, &delay_retpc) &&
				jit_op_rom_rtc_read_byte_callsite(pc_before_op, delay_retpc));
			if (!helper_callsite && legacy_call_push_txn_opcode(pc_before_op, (uae_u16)opcode))
				legacy_maybe_begin_call_push_txn(pc_before_op, (uae_u16)opcode);
			else if (!helper_callsite)
				legacy_maybe_begin_return_pop_txn(pc_before_op, (uae_u16)opcode);
			if (!helper_callsite) {
				(*cpufunctbl[opcode])(opcode);
				Uae2026JitCanonicalizePcAfterFallback();
			}
			jit_interpreted_op_check_ticks();
			total_cycles += 4 * CYCLE_UNIT;
			/* ARM64 correctness barrier: branch-controlled loops still expose
			 * endblock/direct-chain state bugs (Bcc/DBcc can stop early or fall
			 * through into extension words after a few compiled iterations).  If
			 * the trace builder encounters such a branch, keep this whole trace in
			 * the interpreter for now.  This is conservative but lets the JIT
			 * continue compiling straight-line code elsewhere while preserving
			 * branch-loop correctness. */
			const bool current_is_bcc = ((opcode & 0xf000u) == 0x6000u && opcode != 0x6000u);
			const bool current_is_dbcc = ((opcode & 0xf0f8u) == 0x50c8u);
			const bool current_is_stack_pop_move = (opcode == 0x241fu);
			const bool current_is_stack_push_pea =
				(opcode == 0x4850u || opcode == 0x4868u || opcode == 0x4870u ||
				 opcode == 0x4878u || opcode == 0x4879u || opcode == 0x487au || opcode == 0x487bu);
			const bool current_is_return = (opcode == 0x4e73u || opcode == 0x4e74u || opcode == 0x4e75u || opcode == 0x4e76u || opcode == 0x4e77u);
			const bool current_is_link_unlk = ((opcode & 0xfff8u) == 0x4e50u || (opcode & 0xfff8u) == 0x4e58u);
			/* Immediate bit operations (BTST/BCHG/BCLR/BSET, 0x08xx) have
			 * extension-word + memory side effects that are still unsafe across
			 * native continuation; keep the trace in the interpreter. */
			/* Only memory-EA immediate bit-ops carry extension-word + memory side
			 * effects unsafe across native continuation. Register-direct forms
			 * (BTST/BCHG/BCLR/BSET #imm,Dn, EA mode 000) touch only Dn + Z and have
			 * compiled handlers (op_800/840/880/8c0), so let the trace continue. */
			const bool current_is_immediate_bitop =
			    ((opcode & 0xff00u) == 0x0800u) && ((opcode & 0x0038u) != 0x0000u);
			const bool current_is_ethernet_reset_island = (pc_before_op >= 0x010014a0u && pc_before_op <= 0x010014d0u);
			const bool current_is_jsr_jmp = ((opcode & 0xffc0u) == 0x4e80u || (opcode & 0xffc0u) == 0x4ec0u);
			/* Optional, default-off: when B2_JIT_LOCKSTEP_NOBCC is set, drop the
			 * Bcc trace barrier INSIDE the lockstep arming window only, so the
			 * c74 Bcc-liveness block reaches compile_block and the lockstep DUT
			 * hook can arm + step the gold interpreter against it. Scoped to the
			 * window via jit_lockstep_window_pc so normal boots are unperturbed. */
			static int ls_nobcc = -1;
			if (ls_nobcc < 0) ls_nobcc = getenv("B2_JIT_LOCKSTEP_NOBCC") ? 1 : 0;
			const bool drop_bcc = ls_nobcc && jit_lockstep_window_pc(pc_before_op);
			const bool trace_barrier_op = (current_is_bcc && !drop_bcc) || current_is_dbcc || current_is_stack_pop_move || current_is_stack_push_pea || current_is_return || current_is_link_unlk || current_is_immediate_bitop || current_is_ethernet_reset_island || current_is_jsr_jmp;
			if (trace_barrier_op) {
				/* B2_JIT_BARRIER_STATS: per-barrier-type bailout frequency ranking
				 * (default-off). Names which trace_barrier dominates the boot path
				 * so native-codegen work can target the highest-frequency barrier. */
				static int bstats = -1;
				if (bstats < 0) bstats = getenv("B2_JIT_BARRIER_STATS") ? 1 : 0;
				if (bstats) {
					static unsigned long c_bcc=0,c_dbcc=0,c_pop=0,c_push=0,c_ret=0,c_lnk=0,c_bit=0,c_enet=0,c_jmp=0,c_tot=0;
					if (current_is_bcc && !drop_bcc) c_bcc++;
					else if (current_is_dbcc) c_dbcc++;
					else if (current_is_stack_pop_move) c_pop++;
					else if (current_is_stack_push_pea) c_push++;
					else if (current_is_return) c_ret++;
					else if (current_is_link_unlk) c_lnk++;
					else if (current_is_immediate_bitop) c_bit++;
					else if (current_is_ethernet_reset_island) c_enet++;
					else if (current_is_jsr_jmp) c_jmp++;
					if (++c_tot % 200000 == 0)
						fprintf(stderr, "JIT_BARRIER_STATS tot=%lu bcc=%lu jsr_jmp=%lu ret=%lu dbcc=%lu link_unlk=%lu push=%lu pop=%lu imm_bitop=%lu enet=%lu pc=0x%08x\n",
							c_tot, c_bcc, c_jmp, c_ret, c_dbcc, c_lnk, c_push, c_pop, c_bit, c_enet, (unsigned)pc_before_op);
				}
				if (verify_this_block) {
					fprintf(stderr, "JITBLOCKVERIFY block=%08x len=%d skipped=trace_barrier pc=%08x op=%04x\n",
						(unsigned)verify_block_pc, blocklen, (unsigned)pc_before_op, (unsigned)(opcode & 0xffffu));
					jit_block_verify_entry_reset();
				}
				return;
			}
			int maxrun_limit = MAXRUN;
			{
				static int env_maxrun = -1;
				if (env_maxrun < 0) {
					const char *env = getenv("B2_JIT_MAXRUN");
					env_maxrun = (env && *env) ? atoi(env) : MAXRUN;
				}
				maxrun_limit = env_maxrun;
			}
			bool must_end = helper_callsite || __atomic_load_n(&regs.spcflags, __ATOMIC_ACQUIRE) || blocklen >= maxrun_limit;
			/* When the lockstep NOBCC window drops a Bcc barrier, also terminate the
			 * block AT the Bcc so it actually compiles (otherwise the trace runs on to
			 * the next barrier — e.g. the c74 CRC loop's trailing DBF — and is skipped,
			 * leaving the c74 ALU ops uninstrumented). */
			if (drop_bcc && current_is_bcc)
				must_end = true;
			if (!must_end && end_block(opcode)) {
				uintptr new_pcp = (uintptr)regs.pc_p;
				uintptr blk_start = (uintptr)pc_hist[0].location;
				uintptr cur_insn = (uintptr)pc_hist[blocklen - 1].location;
				/* Follow forward short branches to keep straight-line code
				   together, but NEVER follow backward branches.  A backward
				   branch (target <= current instruction) creates a loop;
				   unrolling it into a single block causes DBRA/DBcc to
				   execute a fixed unroll count instead of the runtime
				   counter value.  End the block at the branch and let
				   block chaining handle the backward edge. */
				if (new_pcp > cur_insn && new_pcp < blk_start + 512
				    && blocklen < 32)
					continue;
				must_end = true;
			}
			if (must_end) {
#if defined(CPU_AARCH64)
				tick_inhibit = false;
				uae_u32 block_pc = verify_this_block ? verify_block_pc : get_virtual_address((uae_u8*)pc_hist[0].location);
				{
					static int trace_log = 0;
					if (0 && trace_log++ < 50)
						fprintf(stderr, "TRACE_END blk=%08x len=%d\n", block_pc, blocklen);
				}
				if (verify_this_block) {
					jit_block_verify_run(pc_hist, blocklen, total_cycles, block_pc);
					return;
				}
#endif
				compile_block(pc_hist, blocklen, total_cycles);
				return;
			}
		}
	}
}

void execute_exception(uae_u32 cycles)
{
	countdown -= cycles;
	Exception(regs.jit_exception, 0);
	regs.jit_exception = 0;
}

/* --- JIT native-call helpers for SR/CCR opcodes --- */

extern "C" void jit_op_MakeFromSR(void)
{
    MakeFromSR();
}

extern "C" void jit_op_MakeSR(void)
{
    MakeSR();
}

/* ORI/ANDI/EORI to SR/CCR helpers.
   regs.jit_exception encoding:
   - bits 0-15: immediate value
   - bit 16: 1=word (full SR), 0=byte (CCR only) */

extern "C" void jit_op_orsr(void)
{
    uae_u32 encoded = regs.jit_exception;
    uae_u16 val = (uae_u16)(encoded & 0xFFFF);
    int is_word = (encoded >> 16) & 1;
    MakeSR();
    if (is_word) {
        regs.sr |= val;
    } else {
        regs.sr = (regs.sr & 0xFF00) | ((regs.sr | val) & 0xFF);
    }
    MakeFromSR();
}

extern "C" void jit_op_andsr(void)
{
    uae_u32 encoded = regs.jit_exception;
    uae_u16 val = (uae_u16)(encoded & 0xFFFF);
    int is_word = (encoded >> 16) & 1;
    MakeSR();
    if (is_word) {
        regs.sr &= val;
    } else {
        regs.sr = (regs.sr & 0xFF00) | ((regs.sr & val) & 0xFF);
    }
    MakeFromSR();
}

extern "C" void jit_op_eorsr(void)
{
    uae_u32 encoded = regs.jit_exception;
    uae_u16 val = (uae_u16)(encoded & 0xFFFF);
    int is_word = (encoded >> 16) & 1;
    MakeSR();
    if (is_word) {
        regs.sr ^= val;
    } else {
        regs.sr = (regs.sr & 0xFF00) | ((regs.sr ^ val) & 0xFF);
    }
    MakeFromSR();
}

/* MOVEC helpers */
extern "C" void jit_op_movec2(void)
{
    uae_u32 ext = regs.jit_exception;
    int rn = (ext >> 12) & 15;
    int cr = ext & 0xFFF;
    uae_u32 *regp = &regs.regs[rn];
    if (m68k_movec2(cr, regp) && jit_allow_ram_dispatch_env())
        regs.spcflags |= 0x800;
}

extern "C" void jit_op_move2c(void)
{
    uae_u32 ext = regs.jit_exception;
    int rn = (ext >> 12) & 15;
    int cr = ext & 0xFFF;
    uae_u32 *regp = &regs.regs[rn];
    if (m68k_move2c(cr, regp) && jit_allow_ram_dispatch_env())
        regs.spcflags |= 0x800;
}

/* ================================================================
 * JIT native-call helpers for opcodes that were interpreter-only.
 * Called via compemu_raw_call from compiled JIT blocks.
 * All helpers operate on regs struct directly after flush.
 * ================================================================ */

/* --- Divide helpers ---
 * These are called after src and dst are flushed to regs.
 * regs.jit_exception encodes: bits 0-2 = dst reg, bits 3-5 = src value location.
 * Actually, simpler: we store both values to scratch regs.
 */

/* Helper for DIVU.W Dn (16-bit unsigned divide)
 * jit_exception = (dst_reg << 16) | src_value(16 bit)
 * But src could be from memory... 
 * 
 * Actually, the simplest approach for DIV:
 * The gencomp genamode already loads src into a virtual register.
 * We store src to scratchregs[0], and the dst reg number to jit_exception.
 * The dst value is in regs.regs[dst_reg].
 */

extern "C" void jit_op_divu_w(void)
{
    int dst_reg = regs.jit_exception & 0xF;
    uae_u32 src = regs.scratchregs[0];
    uae_u32 dst = regs.regs[dst_reg];
    
    if (src == 0) {
        /* Division by zero — trigger exception */
        Exception(5, 0);
        return;
    }
    
    uae_u32 quot = dst / (uae_u16)src;
    uae_u32 rem = dst % (uae_u16)src;
    
    /* Check overflow: quotient must fit in 16 bits */
    if (quot > 0xFFFF) {
        /* Overflow: set V flag, leave dest unchanged */
        SET_VFLG(1);
        SET_NFLG(1); /* N is set on overflow per M68K spec */
        SET_CFLG(0);
        return;
    }
    
    SET_VFLG(0);
    SET_CFLG(0);
    SET_ZFLG(quot == 0);
    SET_NFLG((uae_s16)quot < 0);
    regs.regs[dst_reg] = (rem << 16) | (quot & 0xFFFF);
}

extern "C" void jit_op_divs_w(void)
{
    int dst_reg = regs.jit_exception & 0xF;
    uae_s32 src = (uae_s16)(regs.scratchregs[0] & 0xFFFF);
    uae_s32 dst = (uae_s32)regs.regs[dst_reg];
    
    if (src == 0) {
        Exception(5, 0);
        return;
    }
    
    uae_s32 quot = dst / src;
    uae_s32 rem = dst % src;
    
    /* Check overflow: quotient must fit in signed 16 bits */
    if (quot > 32767 || quot < -32768) {
        SET_VFLG(1);
        SET_NFLG(1);
        SET_CFLG(0);
        return;
    }
    
    SET_VFLG(0);
    SET_CFLG(0);
    SET_ZFLG(quot == 0);
    SET_NFLG((uae_s16)quot < 0);
    regs.regs[dst_reg] = ((uae_u16)rem << 16) | ((uae_u16)quot & 0xFFFF);
}

/* --- Long multiply/divide (32×32→64, 64/32→32:32) ---
 * These use two extension words encoding Dl, Dh registers.
 * jit_exception = extension word
 * scratchregs[0] = source value
 */

extern "C" void jit_op_mull(void)
{
    uae_u32 ext = regs.jit_exception;
    uae_u32 src = regs.scratchregs[0];
    int dl = (ext >> 12) & 7;
    int dh = ext & 7;
    int is_signed = ext & 0x800;
    int is_64bit = ext & 0x400;
    
    if (is_signed) {
        uae_s64 result = (uae_s64)(uae_s32)src * (uae_s64)(uae_s32)regs.regs[dl];
        SET_VFLG(0);
        SET_CFLG(0);
        if (is_64bit) {
            regs.regs[dh] = (uae_u32)(result >> 32);
            regs.regs[dl] = (uae_u32)result;
            SET_ZFLG(result == 0);
            SET_NFLG(result < 0);
        } else {
            regs.regs[dl] = (uae_u32)result;
            SET_ZFLG((uae_u32)result == 0);
            SET_NFLG((uae_s32)result < 0);
            if (result > 0x7FFFFFFFLL || result < -0x80000000LL)
                SET_VFLG(1);
        }
    } else {
        uae_u64 result = (uae_u64)src * (uae_u64)regs.regs[dl];
        SET_VFLG(0);
        SET_CFLG(0);
        if (is_64bit) {
            regs.regs[dh] = (uae_u32)(result >> 32);
            regs.regs[dl] = (uae_u32)result;
            SET_ZFLG(result == 0);
            SET_NFLG((uae_s64)result < 0);
        } else {
            regs.regs[dl] = (uae_u32)result;
            SET_ZFLG((uae_u32)result == 0);
            SET_NFLG((uae_s32)(uae_u32)result < 0);
            if (result > 0xFFFFFFFFULL)
                SET_VFLG(1);
        }
    }
}

extern "C" void jit_op_divl(void)
{
    uae_u32 ext = regs.jit_exception;
    uae_u32 src = regs.scratchregs[0];
    int dq = (ext >> 12) & 7;
    int dr = ext & 7;
    int is_signed = ext & 0x800;
    int is_64bit = ext & 0x400;
    
    if (src == 0) {
        Exception(5, 0);
        return;
    }
    
    if (is_signed) {
        if (is_64bit) {
            uae_s64 dividend = ((uae_s64)(uae_s32)regs.regs[dr] << 32) | regs.regs[dq];
            uae_s64 quot = dividend / (uae_s32)src;
            uae_s32 rem = dividend % (uae_s32)src;
            if (quot > 0x7FFFFFFFLL || quot < -0x80000000LL) {
                SET_VFLG(1);
                SET_NFLG(1);
                SET_CFLG(0);
                return;
            }
            SET_VFLG(0); SET_CFLG(0);
            SET_ZFLG((uae_s32)quot == 0);
            SET_NFLG((uae_s32)quot < 0);
            regs.regs[dr] = (uae_u32)rem;
            regs.regs[dq] = (uae_u32)quot;
        } else {
            uae_s32 quot = (uae_s32)regs.regs[dq] / (uae_s32)src;
            uae_s32 rem = (uae_s32)regs.regs[dq] % (uae_s32)src;
            SET_VFLG(0); SET_CFLG(0);
            SET_ZFLG(quot == 0);
            SET_NFLG(quot < 0);
            if (dr != dq) regs.regs[dr] = (uae_u32)rem;
            regs.regs[dq] = (uae_u32)quot;
        }
    } else {
        if (is_64bit) {
            uae_u64 dividend = ((uae_u64)regs.regs[dr] << 32) | regs.regs[dq];
            uae_u64 quot = dividend / src;
            uae_u32 rem = dividend % src;
            if (quot > 0xFFFFFFFFULL) {
                SET_VFLG(1);
                SET_NFLG(1);
                SET_CFLG(0);
                return;
            }
            SET_VFLG(0); SET_CFLG(0);
            SET_ZFLG((uae_u32)quot == 0);
            SET_NFLG((uae_s32)(uae_u32)quot < 0);
            regs.regs[dr] = rem;
            regs.regs[dq] = (uae_u32)quot;
        } else {
            uae_u32 quot = regs.regs[dq] / src;
            uae_u32 rem = regs.regs[dq] % src;
            SET_VFLG(0); SET_CFLG(0);
            SET_ZFLG(quot == 0);
            SET_NFLG((uae_s32)quot < 0);
            if (dr != dq) regs.regs[dr] = rem;
            regs.regs[dq] = quot;
        }
    }
}

/* --- BCD helpers --- */

extern "C" void jit_op_abcd(void)
{
    /* jit_exception: bits 0-2 = dst reg, bits 3-5 = src reg, bit 6 = predec mode */
    int dst_reg = regs.jit_exception & 7;
    int src_reg = (regs.jit_exception >> 3) & 7;
    int predec = (regs.jit_exception >> 6) & 1;
    
    uae_u8 src_val, dst_val;
    uae_u32 src_addr, dst_addr;
    
    if (predec) {
        /* -(An),-(An) mode */
        src_addr = regs.regs[8 + src_reg] -= 1;
        dst_addr = regs.regs[8 + dst_reg] -= 1;
        src_val = get_byte(src_addr);
        dst_val = get_byte(dst_addr);
    } else {
        /* Dn,Dn mode */
        src_val = (uae_u8)regs.regs[src_reg];
        dst_val = (uae_u8)regs.regs[dst_reg];
    }
    
    int x = GET_XFLG();
    int lo = (src_val & 0xF) + (dst_val & 0xF) + x;
    int carry = 0;
    if (lo > 9) { lo -= 10; carry = 1; }
    int hi = ((src_val >> 4) & 0xF) + ((dst_val >> 4) & 0xF) + carry;
    carry = 0;
    if (hi > 9) { hi -= 10; carry = 1; }
    
    uae_u8 result = (uae_u8)((hi << 4) | (lo & 0xF));
    
    SET_XFLG(carry);
    SET_CFLG(carry);
    if (result != 0) SET_ZFLG(0);
    /* N and V are undefined for BCD */
    
    if (predec) {
        put_byte(dst_addr, result);
    } else {
        regs.regs[dst_reg] = (regs.regs[dst_reg] & 0xFFFFFF00) | result;
    }
}

extern "C" void jit_op_sbcd(void)
{
    int dst_reg = regs.jit_exception & 7;
    int src_reg = (regs.jit_exception >> 3) & 7;
    int predec = (regs.jit_exception >> 6) & 1;
    
    uae_u8 src_val, dst_val;
    uae_u32 src_addr, dst_addr;
    
    if (predec) {
        src_addr = regs.regs[8 + src_reg] -= 1;
        dst_addr = regs.regs[8 + dst_reg] -= 1;
        src_val = get_byte(src_addr);
        dst_val = get_byte(dst_addr);
    } else {
        src_val = (uae_u8)regs.regs[src_reg];
        dst_val = (uae_u8)regs.regs[dst_reg];
    }
    
    int x = GET_XFLG();
    int lo = (dst_val & 0xF) - (src_val & 0xF) - x;
    int borrow = 0;
    if (lo < 0) { lo += 10; borrow = 1; }
    int hi = ((dst_val >> 4) & 0xF) - ((src_val >> 4) & 0xF) - borrow;
    borrow = 0;
    if (hi < 0) { hi += 10; borrow = 1; }
    
    uae_u8 result = (uae_u8)((hi << 4) | (lo & 0xF));
    
    SET_XFLG(borrow);
    SET_CFLG(borrow);
    if (result != 0) SET_ZFLG(0);
    
    if (predec) {
        put_byte(dst_addr, result);
    } else {
        regs.regs[dst_reg] = (regs.regs[dst_reg] & 0xFFFFFF00) | result;
    }
}

extern "C" void jit_op_nbcd(void)
{
    /* jit_exception: bits 0-2 = reg (for Dn mode), bit 3 = 1 if memory (addr in scratchregs[0]) */
    int reg = regs.jit_exception & 7;
    int is_mem = (regs.jit_exception >> 3) & 1;
    
    uae_u8 val;
    uae_u32 addr = 0;
    
    if (is_mem) {
        addr = regs.scratchregs[0];
        val = get_byte(addr);
    } else {
        val = (uae_u8)regs.regs[reg];
    }
    
    int x = GET_XFLG();
    int lo = 0 - (val & 0xF) - x;
    int borrow = 0;
    if (lo < 0) { lo += 10; borrow = 1; }
    int hi = 0 - ((val >> 4) & 0xF) - borrow;
    borrow = 0;
    if (hi < 0) { hi += 10; borrow = 1; }
    
    uae_u8 result = (uae_u8)((hi << 4) | (lo & 0xF));
    
    SET_XFLG(borrow);
    SET_CFLG(borrow);
    if (result != 0) SET_ZFLG(0);
    
    if (is_mem) {
        put_byte(addr, result);
    } else {
        regs.regs[reg] = (regs.regs[reg] & 0xFFFFFF00) | result;
    }
}

/* --- MOVEP helpers --- */

extern "C" void jit_op_mvprm(void)
{
    /* Move register to peripheral (byte-interleaved write)
     * jit_exception: bits 0-2 = An, bits 3-5 = Dn, bit 6 = long mode */
    int an = regs.jit_exception & 7;
    int dn = (regs.jit_exception >> 3) & 7;
    int is_long = (regs.jit_exception >> 6) & 1;
    uae_s16 disp = (uae_s16)(regs.jit_exception >> 16);
    uae_u32 addr = regs.regs[8 + an] + disp;
    uae_u32 val = regs.regs[dn];
    
    if (is_long) {
        put_byte(addr, (val >> 24) & 0xFF); addr += 2;
        put_byte(addr, (val >> 16) & 0xFF); addr += 2;
    }
    put_byte(addr, (val >> 8) & 0xFF); addr += 2;
    put_byte(addr, val & 0xFF);
}

extern "C" void jit_op_mvpmr(void)
{
    /* Move peripheral to register (byte-interleaved read) */
    int an = regs.jit_exception & 7;
    int dn = (regs.jit_exception >> 3) & 7;
    int is_long = (regs.jit_exception >> 6) & 1;
    uae_s16 disp = (uae_s16)(regs.jit_exception >> 16);
    uae_u32 addr = regs.regs[8 + an] + disp;
    uae_u32 val = 0;
    
    if (is_long) {
        val = (get_byte(addr) << 24); addr += 2;
        val |= (get_byte(addr) << 16); addr += 2;
    }
    val |= (get_byte(addr) << 8); addr += 2;
    val |= get_byte(addr);
    
    if (is_long) {
        regs.regs[dn] = val;
    } else {
        regs.regs[dn] = (regs.regs[dn] & 0xFFFF0000) | (val & 0xFFFF);
    }
}

/* --- Privileged/flow control helpers --- */

extern "C" void jit_op_mvr2usp(void)
{
    int rn = regs.jit_exception & 0xF;
    regs.usp = regs.regs[rn];
}

extern "C" void jit_op_mvusp2r(void)
{
    int rn = regs.jit_exception & 0xF;
    regs.regs[rn] = regs.usp;
}

extern "C" void jit_op_mvsr2(void)
{
    /* jit_exception: bits 0-2 = destination register (for Dn mode)
     * bit 3 = memory destination, bit 4 = word form (vs byte/CCR form) */
    int reg = regs.jit_exception & 7;
    int is_mem = (regs.jit_exception >> 3) & 1;
    int is_word = (regs.jit_exception >> 4) & 1;
    uae_u16 value;

    if (is_word && !regs.s) {
        Exception(8, 0);
        return;
    }

    MakeSR();
    /* MakeSR() resolves to the interpreter build that reads the condition
     * codes from regflags using FLAGBIT_Z=14 (cznv layout).  The JIT writes
     * its flags into regflags.nzcv using the ARM64 NZCV layout
     * (N=bit31, Z=bit30, C=bit29, V=bit28; X held in regflags.x at bit29).
     * When MOVE.W SR,<ea> is reached straight from JIT-set flags, MakeSR()
     * therefore drops/relocates the CCR bits.  Rebuild the low 5 SR bits
     * directly from the JIT's own layout so the value matches the
     * interpreter result. */
    {
        const uae_u32 nz = regflags.nzcv;
        const uae_u16 ccr =
            (uae_u16)((((regflags.x >> 29) & 1) << 4) | /* X -> SR bit 4 */
                      (((nz >> 31) & 1) << 3) |         /* N -> SR bit 3 */
                      (((nz >> 30) & 1) << 2) |         /* Z -> SR bit 2 */
                      (((nz >> 28) & 1) << 1) |         /* V -> SR bit 1 */
                      ((nz >> 29) & 1));                /* C -> SR bit 0 */
        regs.sr = (uae_u16)((regs.sr & ~0x1f) | ccr);
    }
    value = is_word ? (regs.sr & 0xffff) : (regs.sr & 0x00ff);

    if (is_mem) {
        put_word(regs.scratchregs[0], value);
    } else {
        regs.regs[reg] = (regs.regs[reg] & ~0xffff) | value;
    }
}

extern "C" void jit_op_reset(void)
{
    /* RESET instruction — in emulation, this is a no-op */
}

extern "C" void jit_op_rte(void)
{
    /* RTE is exact in RAM/MMU mode because every frame read, SR transition,
     * stack adjustment, and target prefetch can fault.  Publish the pre-op
     * state before entering the interpreter implementation so the bridge can
     * build/restart the same exception frame if any of those accesses escape. */
    uae_u32 rte_pc = regs.fault_pc ? regs.fault_pc :
        (Uae2026JitLastInstructionPc ? Uae2026JitLastInstructionPc : m68k_getpc());
    Uae2026JitPublishFallbackState(rte_pc, 0x4e73u);
    (*cpufunctbl[0x4e73])(0x4e73);
}

extern "C" void jit_op_rtr(void)
{
    /* RTR: pop CCR (word) and PC from stack */
    uae_u32 sp = m68k_areg(regs, 7);
    MakeSR();
    uae_u16 ccr = get_word(sp); sp += 2;
    regs.sr = (regs.sr & 0xFF00) | (ccr & 0xFF);
    MakeFromSR();
    regs.pc = get_long(sp); sp += 4;
    m68k_areg(regs, 7) = sp;
    fill_prefetch_0();
}

extern "C" void jit_op_stop(void)
{
    /* STOP #imm: load SR from immediate and halt */
    uae_u16 new_sr = (uae_u16)regs.jit_exception;
    regs.sr = new_sr;
    MakeFromSR();
    regs.stopped = 1;
    regs.spcflags |= 0x002; /* Previous SPCFLAG_STOP */
}

extern "C" void jit_op_trap(void)
{
    /* TRAP #vector */
    int vector = regs.jit_exception & 0xF;
    Exception(vector + 32, 0);
}

extern "C" void jit_op_trapv(void)
{
    if (GET_VFLG()) {
        Exception(7, 0);
    }
}

extern "C" void jit_op_moves(void)
{
    /* jit_exception: bits 0-15 = extension word, bits 16-17 = size (0=B,1=W,2=L)
     * scratchregs[0] = effective address */
    uae_u32 encoded = regs.jit_exception;
    uae_u16 extra = (uae_u16)(encoded & 0xffff);
    int size = (encoded >> 16) & 3;
    uae_u32 addr = regs.scratchregs[0];
    int regnum = (extra >> 12) & 15;

    if (!regs.s) {
        Exception(8, 0);
        return;
    }

    if (extra & 0x0800) {
        /* register -> memory */
        uae_u32 src = regs.regs[regnum & 15];
        switch (size) {
        case 0: put_byte(addr, src); break;
        case 1: put_word(addr, src); break;
        default: put_long(addr, src); break;
        }
    } else {
        /* memory -> register */
        switch (size) {
        case 0: {
            uae_s8 src = (uae_s8)get_byte(addr);
            if (extra & 0x8000)
                m68k_areg(regs, regnum & 7) = (uae_s32)src;
            else
                m68k_dreg(regs, regnum & 7) = (m68k_dreg(regs, regnum & 7) & ~0xff) | ((uae_u8)src);
            break;
        }
        case 1: {
            uae_s16 src = (uae_s16)get_word(addr);
            if (extra & 0x8000)
                m68k_areg(regs, regnum & 7) = (uae_s32)src;
            else
                m68k_dreg(regs, regnum & 7) = (m68k_dreg(regs, regnum & 7) & ~0xffff) | ((uae_u16)src);
            break;
        }
        default: {
            uae_u32 src = get_long(addr);
            if (extra & 0x8000)
                m68k_areg(regs, regnum & 7) = src;
            else
                m68k_dreg(regs, regnum & 7) = src;
            break;
        }
        }
    }
}

extern "C" void jit_op_chk(void)
{
    /* CHK Dn,<ea> — trap if Dn < 0 or Dn > src */
    int dst_reg = regs.jit_exception & 7;
    uae_s16 src = (uae_s16)(regs.scratchregs[0] & 0xFFFF);
    uae_s16 dst = (uae_s16)(regs.regs[dst_reg] & 0xFFFF);
    
    SET_ZFLG(dst == 0);
    SET_VFLG(0);
    SET_CFLG(0);
    
    if (dst < 0) {
        SET_NFLG(1);
        Exception(6, 0);
    } else if (dst > src) {
        SET_NFLG(0);
        Exception(6, 0);
    }
}

/* --- CHK2/CAS helpers --- */
static inline uae_s32 jit_sign_extend_size(uae_u32 v, int size)
{
    switch (size) {
    case 0: return (uae_s32)(uae_s8)v;
    case 1: return (uae_s32)(uae_s16)v;
    default: return (uae_s32)v;
    }
}

static inline uae_u32 jit_size_mask(int size)
{
    switch (size) {
    case 0: return 0xffu;
    case 1: return 0xffffu;
    default: return 0xffffffffu;
    }
}

extern "C" void jit_op_chk2(void)
{
    uae_u32 enc = regs.jit_exception;
    int size = (enc >> 16) & 3;
    uae_u16 extra = enc & 0xffff;
    uae_u32 addr = regs.scratchregs[0];
    int regno = (extra >> 12) & 15;

    uae_s32 lower, upper, reg = (uae_s32)regs.regs[regno];
    switch (size) {
    case 0:
        lower = (uae_s32)(uae_s8)get_byte(addr);
        upper = (uae_s32)(uae_s8)get_byte(addr + 1);
        if ((extra & 0x8000) == 0)
            reg = (uae_s32)(uae_s8)reg;
        break;
    case 1:
        lower = (uae_s32)(uae_s16)get_word(addr);
        upper = (uae_s32)(uae_s16)get_word(addr + 2);
        if ((extra & 0x8000) == 0)
            reg = (uae_s32)(uae_s16)reg;
        break;
    default:
        lower = (uae_s32)get_long(addr);
        upper = (uae_s32)get_long(addr + 4);
        break;
    }

    SET_ZFLG(upper == reg || lower == reg);
    int outside = lower <= upper ? (reg < lower || reg > upper) : (reg > upper || reg < lower);
    SET_CFLG(outside);
    if ((extra & 0x0800) && outside)
        Exception(6, 0);
}

static inline void jit_cas_flags(uae_u32 dst, uae_u32 cmp, int size)
{
    uae_u32 mask = jit_size_mask(size);
    dst &= mask;
    cmp &= mask;
    uae_u32 newv = (dst - cmp) & mask;
    int bits = size == 0 ? 8 : (size == 1 ? 16 : 32);
    uae_u32 sign = 1u << (bits - 1);
    int flgs = (cmp & sign) != 0;
    int flgo = (dst & sign) != 0;
    int flgn = (newv & sign) != 0;
    SET_ZFLG(newv == 0);
    SET_VFLG((flgs != flgo) && (flgn != flgo));
    SET_CFLG(cmp > dst);
    SET_NFLG(flgn != 0);
}

extern "C" void jit_op_cas(void)
{
    uae_u32 enc = regs.jit_exception;
    int size = (enc >> 16) & 3;
    uae_u16 extra = enc & 0xffff;
    uae_u32 addr = regs.scratchregs[0];
    int ru = (extra >> 6) & 7;
    int rc = extra & 7;
    uae_u32 dst;

    switch (size) {
    case 0:
        dst = get_byte(addr);
        jit_cas_flags(dst, regs.regs[rc], size);
        if (GET_ZFLG())
            put_byte(addr, regs.regs[ru]);
        else
            regs.regs[rc] = (regs.regs[rc] & ~0xffu) | (dst & 0xffu);
        break;
    case 1:
        dst = get_word(addr);
        jit_cas_flags(dst, regs.regs[rc], size);
        if (GET_ZFLG())
            put_word(addr, regs.regs[ru]);
        else
            regs.regs[rc] = (regs.regs[rc] & ~0xffffu) | (dst & 0xffffu);
        break;
    default:
        dst = get_long(addr);
        jit_cas_flags(dst, regs.regs[rc], size);
        if (GET_ZFLG())
            put_long(addr, regs.regs[ru]);
        else
            regs.regs[rc] = dst;
        break;
    }
}

/* --- TAS helper --- */
extern "C" void jit_op_tas(void)
{
    /* jit_exception: bit 3 = memory mode, bits 0-2 = Dn reg (if !memory)
     * scratchregs[0] = EA address (if memory) */
    int is_mem = (regs.jit_exception >> 3) & 1;
    int reg = regs.jit_exception & 7;
    
    uae_u8 val;
    uae_u32 addr = 0;
    
    if (is_mem) {
        addr = regs.scratchregs[0];
        val = get_byte(addr);
    } else {
        val = (uae_u8)regs.regs[reg];
    }
    
    SET_ZFLG(val == 0);
    SET_NFLG(val & 0x80);
    SET_VFLG(0);
    SET_CFLG(0);
    
    val |= 0x80; /* Set bit 7 */
    
    if (is_mem) {
        put_byte(addr, val);
    } else {
        regs.regs[reg] = (regs.regs[reg] & 0xFFFFFF00) | val;
    }
}

/* --- PACK/UNPK helpers --- */
extern "C" void jit_op_pack(void)
{
    /* PACK Dn,Dn,#adj or PACK -(An),-(An),#adj */
    int dst_reg = regs.jit_exception & 7;
    int src_reg = (regs.jit_exception >> 3) & 7;
    int predec = (regs.jit_exception >> 6) & 1;
    uae_s16 adj = (uae_s16)(regs.jit_exception >> 16);
    
    uae_u16 val;
    if (predec) {
        uae_u32 src_addr = regs.regs[8 + src_reg] -= 2;
        val = get_word(src_addr);
    } else {
        val = (uae_u16)regs.regs[src_reg];
    }
    
    val += adj;
    uae_u8 result = ((val >> 4) & 0xF0) | (val & 0x0F);
    
    if (predec) {
        uae_u32 dst_addr = regs.regs[8 + dst_reg] -= 1;
        put_byte(dst_addr, result);
    } else {
        regs.regs[dst_reg] = (regs.regs[dst_reg] & 0xFFFFFF00) | result;
    }
}

extern "C" void jit_op_unpk(void)
{
    int dst_reg = regs.jit_exception & 7;
    int src_reg = (regs.jit_exception >> 3) & 7;
    int predec = (regs.jit_exception >> 6) & 1;
    uae_s16 adj = (uae_s16)(regs.jit_exception >> 16);
    
    uae_u8 val;
    if (predec) {
        uae_u32 src_addr = regs.regs[8 + src_reg] -= 1;
        val = get_byte(src_addr);
    } else {
        val = (uae_u8)regs.regs[src_reg];
    }
    
    uae_u16 result = ((val & 0xF0) << 4) | (val & 0x0F);
    result += adj;
    
    if (predec) {
        uae_u32 dst_addr = regs.regs[8 + dst_reg] -= 2;
        put_word(dst_addr, result);
    } else {
        regs.regs[dst_reg] = (regs.regs[dst_reg] & 0xFFFF0000) | result;
    }
}

/* --- 68020 bitfield helpers --- */
static inline uae_u32 jit_bf_mask(int width)
{
    return width >= 32 ? 0xffffffffu : ((1u << width) - 1u);
}

static inline uae_u32 jit_bf_rotl32(uae_u32 v, int shift)
{
    shift &= 31;
    return shift ? ((v << shift) | (v >> (32 - shift))) : v;
}

static inline uae_u32 jit_bf_rotr32(uae_u32 v, int shift)
{
    shift &= 31;
    return shift ? ((v >> shift) | (v << (32 - shift))) : v;
}

static inline bool jit_helper_live_memory(uae_u32 addr)
{
    if (!jit_allow_ram_dispatch_env())
        return false;
    const uae_u32 end = addr + 8u;
    if (end >= addr && ((addr < 0x00020000u && end <= 0x00020000u) ||
        (addr >= 0x01000000u && end <= 0x01020000u)))
        return false;
    return true;
}

static inline uae_u32 jit_mem_bget(uae_u32 addr) { return jit_helper_live_memory(addr) ? Uae2026JitLiveGetByte(addr) : get_byte(addr); }
static inline uae_u32 jit_mem_wget(uae_u32 addr) { return jit_helper_live_memory(addr) ? Uae2026JitLiveGetWord(addr) : get_word(addr); }
static inline uae_u32 jit_mem_lget(uae_u32 addr) { return jit_helper_live_memory(addr) ? Uae2026JitLiveGetLong(addr) : get_long(addr); }
static inline void jit_mem_bput(uae_u32 addr, uae_u32 val) { if (jit_helper_live_memory(addr)) Uae2026JitLivePutByte(addr, val); else put_byte(addr, val); }
static inline void jit_mem_wput(uae_u32 addr, uae_u32 val) { if (jit_helper_live_memory(addr)) Uae2026JitLivePutWord(addr, val); else put_word(addr, val); }
static inline void jit_mem_lput(uae_u32 addr, uae_u32 val) { if (jit_helper_live_memory(addr)) Uae2026JitLivePutLong(addr, val); else put_long(addr, val); }

static uae_u32 jit_bf_get_mem(uae_u32 src, uae_u32 bdata[2], uae_s32 offset, int width)
{
    uae_u32 tmp, res, mask;

    offset &= 7;
    mask = 0xffffffffu << (32 - width);
    switch ((offset + width + 7) >> 3) {
    case 1:
        tmp = jit_mem_bget(src);
        res = tmp << (24 + offset);
        bdata[0] = tmp & ~(mask >> (24 + offset));
        break;
    case 2:
        tmp = jit_mem_wget(src);
        res = tmp << (16 + offset);
        bdata[0] = tmp & ~(mask >> (16 + offset));
        break;
    case 3:
        tmp = jit_mem_wget(src);
        res = tmp << (16 + offset);
        bdata[0] = tmp & ~(mask >> (16 + offset));
        tmp = jit_mem_bget(src + 2);
        res |= tmp << (8 + offset);
        bdata[1] = tmp & ~(mask >> (8 + offset));
        break;
    case 4:
        tmp = jit_mem_lget(src);
        res = tmp << offset;
        bdata[0] = tmp & ~(mask >> offset);
        break;
    case 5:
        tmp = jit_mem_lget(src);
        res = tmp << offset;
        bdata[0] = tmp & ~(mask >> offset);
        tmp = jit_mem_bget(src + 4);
        res |= tmp >> (8 - offset);
        bdata[1] = tmp & ~(mask << (8 - offset));
        break;
    default:
        res = 0;
        break;
    }
    return res;
}

static void jit_bf_put_mem(uae_u32 dst, uae_u32 bdata[2], uae_u32 val, uae_s32 offset, int width)
{
    offset = (offset & 7) + width;
    switch ((offset + 7) >> 3) {
    case 1:
        jit_mem_bput(dst, bdata[0] | (val << (8 - offset)));
        break;
    case 2:
        jit_mem_wput(dst, bdata[0] | (val << (16 - offset)));
        break;
    case 3:
        jit_mem_wput(dst, bdata[0] | (val >> (offset - 16)));
        jit_mem_bput(dst + 2, bdata[1] | (val << (24 - offset)));
        break;
    case 4:
        jit_mem_lput(dst, bdata[0] | (val << (32 - offset)));
        break;
    case 5:
        jit_mem_lput(dst, bdata[0] | (val >> (offset - 32)));
        jit_mem_bput(dst + 4, bdata[1] | (val << (40 - offset)));
        break;
    }
}

extern "C" void jit_op_bitfield(void)
{
    enum {
        JIT_BF_OP_BFTST = 0,
        JIT_BF_OP_BFEXTU = 1,
        JIT_BF_OP_BFCHG = 2,
        JIT_BF_OP_BFEXTS = 3,
        JIT_BF_OP_BFCLR = 4,
        JIT_BF_OP_BFFFO = 5,
        JIT_BF_OP_BFSET = 6
    };

    uae_u32 encoded = regs.jit_exception;
    uae_u16 ext = encoded & 0xffff;
    int op = (encoded >> 16) & 0xff;
    uae_u32 ea_info = regs.scratchregs[0];

    uae_s32 offset = (ext & 0x0800) ? (uae_s32)regs.regs[(ext >> 6) & 7] : ((ext >> 6) & 0x1f);
    int width = (((((ext & 0x0020) ? regs.regs[ext & 7] : ext) - 1) & 0x1f) + 1);
    uae_u32 mask = jit_bf_mask(width);
    int result_reg = (ext >> 12) & 7;

    if (ea_info & 0x80000000u) {
        int dreg = ea_info & 7;
        int reg_offset = offset & 31;
        uae_u32 rotated = jit_bf_rotl32(regs.regs[dreg], reg_offset);
        uae_u32 field = rotated >> (32 - width);

        SET_NFLG(((uae_s32)rotated) < 0);
        SET_ZFLG(field == 0);
        SET_VFLG(0);
        SET_CFLG(0);

        switch (op) {
        case JIT_BF_OP_BFTST:
            break;
        case JIT_BF_OP_BFEXTU:
            regs.regs[result_reg] = field;
            break;
        case JIT_BF_OP_BFEXTS:
            regs.regs[result_reg] = (uae_u32)((uae_s32)rotated >> (32 - width));
            break;
        case JIT_BF_OP_BFFFO: {
            uae_u32 ffo = reg_offset;
            uae_u32 bit = 1u << (width - 1);
            while (bit) {
                if (field & bit)
                    break;
                bit >>= 1;
                ffo++;
            }
            regs.regs[result_reg] = ffo;
            break;
        }
        case JIT_BF_OP_BFCHG:
        case JIT_BF_OP_BFCLR:
        case JIT_BF_OP_BFSET: {
            uae_u32 new_field;
            if (op == JIT_BF_OP_BFCHG)
                new_field = field ^ mask;
            else if (op == JIT_BF_OP_BFCLR)
                new_field = 0;
            else
                new_field = mask;
            uae_u32 preserve = (width == 32) ? 0 : (rotated & ((1u << (32 - width)) - 1u));
            uae_u32 merged = preserve | (new_field << (32 - width));
            regs.regs[dreg] = jit_bf_rotr32(merged, reg_offset);
            break;
        }
        }
        return;
    }

    uae_u32 dsta = ea_info + (offset >> 3);
    uae_u32 bdata[2] = { 0, 0 };
    uae_u32 tmp = jit_bf_get_mem(dsta, bdata, offset, width);
    uae_u32 field = tmp >> (32 - width);

    SET_NFLG(((uae_s32)tmp) < 0);
    SET_ZFLG(field == 0);
    SET_VFLG(0);
    SET_CFLG(0);

    switch (op) {
    case JIT_BF_OP_BFTST:
        break;
    case JIT_BF_OP_BFEXTU:
        regs.regs[result_reg] = field;
        break;
    case JIT_BF_OP_BFEXTS:
        regs.regs[result_reg] = (uae_u32)((uae_s32)tmp >> (32 - width));
        break;
    case JIT_BF_OP_BFFFO: {
        uae_u32 ffo = offset;
        uae_u32 bit = 1u << (width - 1);
        while (bit) {
            if (field & bit)
                break;
            bit >>= 1;
            ffo++;
        }
        regs.regs[result_reg] = ffo;
        break;
    }
    case JIT_BF_OP_BFCHG:
        jit_bf_put_mem(dsta, bdata, field ^ mask, offset, width);
        break;
    case JIT_BF_OP_BFCLR:
        jit_bf_put_mem(dsta, bdata, 0, offset, width);
        break;
    case JIT_BF_OP_BFSET:
        jit_bf_put_mem(dsta, bdata, mask, offset, width);
        break;
    }
}

/* --- BFINS helper --- */
extern "C" void jit_op_bfins(void)
{
    /* Bit field insert — complex encoding.
     * jit_exception = extension word
     * scratchregs[0] = effective address (for memory EA) or reg number + 0x80000000 for Dn
     * The extension word:
     *   bits 15-12: Dn (source data register)
     *   bit 11: Do (0=offset in ext, 1=offset in Dn)
     *   bits 10-6: offset or offset reg
     *   bit 5: Dw (0=width in ext, 1=width in Dn)
     *   bits 4-0: width or width reg */
    uae_u32 ext = regs.jit_exception;
    uae_u32 ea_info = regs.scratchregs[0];
    
    int dn = (ext >> 12) & 7;
    int do_reg = (ext >> 11) & 1;
    int offset = do_reg ? (regs.regs[(ext >> 6) & 7] & 31) : ((ext >> 6) & 31);
    int dw_reg = (ext >> 5) & 1;
    int width = dw_reg ? (regs.regs[ext & 7] & 31) : (ext & 31);
    if (width == 0) width = 32;
    
    uae_u32 ins_data = regs.regs[dn];
    
    if (ea_info & 0x80000000) {
        /* Register destination */
        int dreg = ea_info & 7;
        uae_u32 val = regs.regs[dreg];
        uae_u32 mask = (width == 32) ? 0xFFFFFFFF : ((1u << width) - 1);
        int shift = 32 - offset - width;
        if (shift < 0) shift += 32; /* shouldn't happen for reg */
        val &= ~(mask << shift);
        val |= ((ins_data & mask) << shift);
        regs.regs[dreg] = val;
        
        /* Set flags based on inserted field */
        uae_u32 field = (ins_data & mask);
        SET_NFLG((field >> (width - 1)) & 1);
        SET_ZFLG(field == 0);
        SET_VFLG(0);
        SET_CFLG(0);
    } else {
        /* Memory destination — byte-oriented bit manipulation */
        uae_u32 addr = ea_info;
        int byte_offset = offset >> 3;
        int bit_offset = offset & 7;
        addr += byte_offset;
        
        /* Read enough bytes to cover the field */
        int total_bits = bit_offset + width;
        int bytes_needed = (total_bits + 7) >> 3;
        uae_u32 val = 0;
        for (int i = 0; i < bytes_needed && i < 5; i++) {
            val = (val << 8) | jit_mem_bget(addr + i);
        }
        
        uae_u32 mask = (width == 32) ? 0xFFFFFFFF : ((1u << width) - 1);
        int shift = (bytes_needed * 8) - bit_offset - width;
        val &= ~(mask << shift);
        val |= ((ins_data & mask) << shift);
        
        for (int i = 0; i < bytes_needed && i < 5; i++) {
            jit_mem_bput(addr + i, (val >> ((bytes_needed - 1 - i) * 8)) & 0xFF);
        }
        
        uae_u32 field = (ins_data & mask);
        SET_NFLG((field >> (width - 1)) & 1);
        SET_ZFLG(field == 0);
        SET_VFLG(0);
        SET_CFLG(0);
    }
}

/* --- ROXL/ROXR register helpers --- */

extern "C" void jit_op_roxl(void)
{
    /* jit_exception: bits 0-2 = dst reg, bits 3-4 = size (0=B,1=W,2=L),
     * scratchregs[0] = count */
    int dst_reg = regs.jit_exception & 7;
    int size = (regs.jit_exception >> 3) & 3;
    int count = regs.scratchregs[0] & 63;
    
    int x = GET_XFLG();
    uae_u32 val = regs.regs[dst_reg];
    int bits;
    uae_u32 mask;
    
    switch (size) {
        case 0: bits = 8; mask = 0xFF; break;
        case 1: bits = 16; mask = 0xFFFF; break;
        default: bits = 32; mask = 0xFFFFFFFF; break;
    }
    
    val &= mask;
    int modcount = count % (bits + 1); /* ROXL modulo is bits+1 (includes X) */
    
    for (int i = 0; i < modcount; i++) {
        int msb = (val >> (bits - 1)) & 1;
        val = ((val << 1) | x) & mask;
        x = msb;
    }
    
    SET_XFLG(x);
    SET_CFLG(count ? x : GET_XFLG());
    SET_ZFLG(val == 0);
    SET_NFLG((val >> (bits - 1)) & 1);
    SET_VFLG(0);
    
    regs.regs[dst_reg] = (regs.regs[dst_reg] & ~mask) | val;
}

extern "C" void jit_op_roxr(void)
{
    int dst_reg = regs.jit_exception & 7;
    int size = (regs.jit_exception >> 3) & 3;
    int count = regs.scratchregs[0] & 63;
    
    int x = GET_XFLG();
    uae_u32 val = regs.regs[dst_reg];
    int bits;
    uae_u32 mask;
    
    switch (size) {
        case 0: bits = 8; mask = 0xFF; break;
        case 1: bits = 16; mask = 0xFFFF; break;
        default: bits = 32; mask = 0xFFFFFFFF; break;
    }
    
    val &= mask;
    int modcount = count % (bits + 1);
    
    for (int i = 0; i < modcount; i++) {
        int lsb = val & 1;
        val = ((val >> 1) | ((uae_u32)x << (bits - 1))) & mask;
        x = lsb;
    }
    
    SET_XFLG(x);
    SET_CFLG(count ? x : GET_XFLG());
    SET_ZFLG(val == 0);
    SET_NFLG((val >> (bits - 1)) & 1);
    SET_VFLG(0);
    
    regs.regs[dst_reg] = (regs.regs[dst_reg] & ~mask) | val;
}

/* --- Memory shift/rotate helpers ---
 * These operate on a word at an EA (shift by 1 bit only).
 * scratchregs[0] = effective address */

extern "C" void jit_op_asrw(void)
{
    uae_u32 addr = regs.scratchregs[0];
    uae_s16 val = (uae_s16)get_word(addr);
    int lsb = val & 1;
    val >>= 1; /* Arithmetic shift preserves sign */
    put_word(addr, (uae_u16)val);
    SET_XFLG(lsb); SET_CFLG(lsb);
    SET_ZFLG(val == 0);
    SET_NFLG(val < 0);
    SET_VFLG(0);
}

extern "C" void jit_op_aslw(void)
{
    uae_u32 addr = regs.scratchregs[0];
    uae_u16 val = get_word(addr);
    int msb = (val >> 15) & 1;
    uae_u16 result = val << 1;
    int new_msb = (result >> 15) & 1;
    put_word(addr, result);
    SET_XFLG(msb); SET_CFLG(msb);
    SET_ZFLG(result == 0);
    SET_NFLG(new_msb);
    SET_VFLG(msb != new_msb); /* V set if sign changed */
}

extern "C" void jit_op_lsrw(void)
{
    uae_u32 addr = regs.scratchregs[0];
    uae_u16 val = get_word(addr);
    int lsb = val & 1;
    val >>= 1;
    put_word(addr, val);
    SET_XFLG(lsb); SET_CFLG(lsb);
    SET_ZFLG(val == 0);
    SET_NFLG(0);
    SET_VFLG(0);
}

extern "C" void jit_op_lslw(void)
{
    uae_u32 addr = regs.scratchregs[0];
    uae_u16 val = get_word(addr);
    int msb = (val >> 15) & 1;
    val <<= 1;
    put_word(addr, val);
    SET_XFLG(msb); SET_CFLG(msb);
    SET_ZFLG(val == 0);
    SET_NFLG((val >> 15) & 1);
    SET_VFLG(0);
}

extern "C" void jit_op_rolw(void)
{
    uae_u32 addr = regs.scratchregs[0];
    uae_u16 val = get_word(addr);
    int msb = (val >> 15) & 1;
    val = (val << 1) | msb;
    put_word(addr, val);
    SET_CFLG(msb);
    SET_ZFLG(val == 0);
    SET_NFLG((val >> 15) & 1);
    SET_VFLG(0);
}

extern "C" void jit_op_rorw(void)
{
    uae_u32 addr = regs.scratchregs[0];
    uae_u16 val = get_word(addr);
    int lsb = val & 1;
    val = (val >> 1) | (lsb << 15);
    put_word(addr, val);
    SET_CFLG(lsb);
    SET_ZFLG(val == 0);
    SET_NFLG((val >> 15) & 1);
    SET_VFLG(0);
}

extern "C" void jit_op_roxlw(void)
{
    uae_u32 addr = regs.scratchregs[0];
    uae_u16 val = get_word(addr);
    int msb = (val >> 15) & 1;
    int x = GET_XFLG();
    val = (val << 1) | x;
    put_word(addr, val);
    SET_XFLG(msb); SET_CFLG(msb);
    SET_ZFLG(val == 0);
    SET_NFLG((val >> 15) & 1);
    SET_VFLG(0);
}

extern "C" void jit_op_roxrw(void)
{
    uae_u32 addr = regs.scratchregs[0];
    uae_u16 val = get_word(addr);
    int lsb = val & 1;
    int x = GET_XFLG();
    val = (val >> 1) | (x << 15);
    put_word(addr, val);
    SET_XFLG(lsb); SET_CFLG(lsb);
    SET_ZFLG(val == 0);
    SET_NFLG((val >> 15) & 1);
    SET_VFLG(0);
}

/* --- Unsupported/system/cache/FPU-frame helpers --- */
extern "C" void jit_op_callm(void)
{
    op_illg(regs.jit_exception & 0xffff);
}

extern "C" void jit_op_mmuop030(void)
{
    op_illg(regs.jit_exception & 0xffff);
}

extern "C" void jit_op_mmu_final(void)
{
    uae_u16 opcode = regs.jit_exception & 0xffff;
    if ((opcode & 0x0fe0) == 0x0500) {
        regs.mmusr = 0;
        return;
    }
    if ((opcode & 0x0fd8) == 0x0548) {
        return;
    }
    op_illg(opcode);
}

extern "C" void jit_op_frestore(void)
{
    if (!regs.s) {
        Exception(8, 0);
        return;
    }
    /* The current Previous UAE2026 bridge is built without linked FPU frame
     * helpers.  Treat FRESTORE as a privileged no-op for coverage parity with
     * the existing FPU-disabled JIT path; full state-frame restoration belongs
     * with the FPU tranche. */
}

extern "C" void jit_op_cinva(void)
{
    if (!regs.s) {
        Exception(8, 0);
        return;
    }
    flush_internals();
}

extern "C" void jit_op_cpusha(void)
{
    if (!regs.s) {
        Exception(8, 0);
        return;
    }
    flush_internals();
}

extern "C" void jit_op_cache_line(void)
{
    if (!regs.s) {
        Exception(8, 0);
        return;
    }
    flush_internals();
}

extern "C" void jit_op_bkpt(void)
{
    op_illg(regs.jit_exception & 0xffff);
}

extern "C" void jit_op_rtm(void)
{
    op_illg(regs.jit_exception & 0xffff);
}

extern "C" void jit_op_fsave(void)
{
    if (!regs.s) {
        Exception(8, 0);
        return;
    }
    /* FPU frame helpers are not linked in the current bridge build. */
}

extern "C" void jit_op_ftrapcc(void)
{
    /* FPU condition evaluation belongs to the FPU tranche; keep as no-op. */
}

extern "C" void jit_op_fdbcc(void)
{
    /* FPU condition evaluation belongs to the FPU tranche; keep as no-op. */
}

extern "C" void jit_op_movep(void)
{
    uae_u32 enc = regs.jit_exception;
    uae_u16 opcode = enc & 0xffff;
    int kind = (enc >> 16) & 3;
    int dreg = (opcode >> 9) & 7;
    uae_u32 addr = regs.scratchregs[0];

    switch (kind) {
    case 0: { /* memory -> Dn, word */
        uae_u16 val = (get_byte(addr) << 8) | get_byte(addr + 2);
        regs.regs[dreg] = (regs.regs[dreg] & ~0xffffu) | val;
        break;
    }
    case 1: { /* memory -> Dn, long */
        uae_u32 val = (get_byte(addr) << 24) | (get_byte(addr + 2) << 16) |
                      (get_byte(addr + 4) << 8) | get_byte(addr + 6);
        regs.regs[dreg] = val;
        break;
    }
    case 2: { /* Dn -> memory, word */
        uae_u32 val = regs.regs[dreg];
        put_byte(addr, val >> 8);
        put_byte(addr + 2, val);
        break;
    }
    case 3: { /* Dn -> memory, long */
        uae_u32 val = regs.regs[dreg];
        put_byte(addr, val >> 24);
        put_byte(addr + 2, val >> 16);
        put_byte(addr + 4, val >> 8);
        put_byte(addr + 6, val);
        break;
    }
    }
}

extern "C" void jit_op_cas2(void)
{
    uae_u32 extra = regs.scratchregs[0];
    int size = ((regs.jit_exception & 0xffff) == 0x0cfc) ? 1 : 2;
    uae_u32 rn1 = regs.regs[(extra >> 28) & 15];
    uae_u32 rn2 = regs.regs[(extra >> 12) & 15];
    int ru1 = (extra >> 22) & 7;
    int rc1 = (extra >> 16) & 7;
    int ru2 = (extra >> 6) & 7;
    int rc2 = extra & 7;
    uae_u32 dst1 = size == 1 ? get_word(rn1) : get_long(rn1);
    uae_u32 dst2 = size == 1 ? get_word(rn2) : get_long(rn2);

    jit_cas_flags(dst1, regs.regs[rc1], size);
    if (GET_ZFLG()) {
        jit_cas_flags(dst2, regs.regs[rc2], size);
        if (GET_ZFLG()) {
            if (size == 1) {
                put_word(rn1, regs.regs[ru1]);
                put_word(rn2, regs.regs[ru2]);
            } else {
                put_long(rn1, regs.regs[ru1]);
                put_long(rn2, regs.regs[ru2]);
            }
            return;
        }
    }
    if (size == 1) {
        regs.regs[rc2] = (regs.regs[rc2] & ~0xffffu) | (dst2 & 0xffffu);
        regs.regs[rc1] = (regs.regs[rc1] & ~0xffffu) | (dst1 & 0xffffu);
    } else {
        regs.regs[rc2] = dst2;
        regs.regs[rc1] = dst1;
    }
}

/* --- TRAPcc helper --- */
extern "C" void jit_op_trapcc(void)
{
    int cc = regs.jit_exception & 15;
    if (cctrue(cc)) {
        Exception(7, 0);
    }
}


/* ================================================================
 * JIT FPU shadow register sync (MPFR ↔ double)
 * Called at JIT block boundaries when USE_JIT_FPU is enabled.
 * ================================================================ */
#ifdef USE_JIT_FPU
#include "fpu/fpu.h"

/* Sync MPFR FP registers → JIT shadow doubles (block entry) */
extern "C" void jit_fpu_sync_to_shadow(void)
{
#ifdef FPU_MPFR
    for (int i = 0; i < 8; i++) {
        regs.jit_fpregs[i] = mpfr_get_d(fpu.registers[i].f, MPFR_RNDN);
    }
#else
    for (int i = 0; i < 8; i++) {
        regs.jit_fpregs[i] = (double)fpu.registers[i];
    }
#endif
}

/* Sync JIT shadow doubles → MPFR FP registers (block exit) */
extern "C" void jit_fpu_sync_from_shadow(void)
{
#ifdef FPU_MPFR
    for (int i = 0; i < 8; i++) {
        mpfr_set_d(fpu.registers[i].f, regs.jit_fpregs[i], MPFR_RNDN);
        fpu.registers[i].nan_bits = 0xffffffffffffffffULL;
        fpu.registers[i].nan_sign = 0;
    }
#else
    for (int i = 0; i < 8; i++) {
        fpu.registers[i] = (fpu_register)regs.jit_fpregs[i];
    }
#endif
}

#endif /* USE_JIT_FPU */
