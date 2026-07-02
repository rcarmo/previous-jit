#!/usr/bin/env bash

# Previous-specific opcode vectors for the UAE2026 bridge harness.
# Absolute RAM references from the BasiliskII harness are remapped from
# 0x0000A000-style addresses to Previous RAM at 0x0400A000.

declare -a TEST_ORDER=(
  ori_sr_hardfail andi_sr_hardfail eori_sr_hardfail move_from_sr_hardfail move_to_sr_hardfail sr_ops_combo
  scc_vc_vs dbvc_loop_v_set dbvs_loop_v_clear dbvc_not_taken_v_clear dbvs_not_taken_v_set
  divs_word_hardfail divu_word_hardfail divs_neg_by_neg_edge divs_by_minus_one_edge divs_zero_dividend_edge divs_overflow_edge
  divu_exact_edge divu_with_remainder_edge divu_overflow_edge
  mull_32_hardfail divl_32_hardfail mull_unsigned_32 mull_signed_32 divl_unsigned_32 divl_signed_32
  mull_u64 mull_s32_neg divl_u32_rem divl_s32_neg divl_u32_max divl_s32_neg_divisor mull_s64_neg divl_same_dq_dr divl_u64 divl_s64
  aslw_mem_hardfail lsrw_mem_hardfail rolw_mem_hardfail asrw_mem_edge roxlw_mem_edge roxrw_mem_edge
  addx_l_reg_xset subx_l_reg_xset negx_l_reg_xset addx_l_reg_xclr addx_l_predec_xset
  cmpm_l_equal tst_l_postinc_neg neg_l_postinc negx_l_postinc_xset cmp_l_postinc_d1_eq and_l_postinc_d0
  bfextu_reg_edge bfexts_reg_edge bfffo_reg_edge bfset_reg_edge bfclr_reg_edge bfchg_reg_edge bftst_reg_edge bfins_reg_edge bfins_dreg_imm bfins_dreg_narrow
  chk2_long_in_range cas_long_match_update cas2_word_match_update movep_roundtrip movem_long_predec_roundtrip
  jsr_an_call_return bsr_word_call_return seam_movea_a0_chain seam_a0_a1_chain seam_user_stack_push seam_movem_restore_frame seam_movem_restore_full_frame seam_hash_lookup_chain seam_jsr_user_stack seam_hash_call_chain seam_byte_store_d2_fault_shape seam_byte_copy_postinc_fault_shape
  pack_dn_edge unpk_dn_edge moves_write_read move_l_imm_special_long movec_vbr_roundtrip movec_sfc_roundtrip movec_dfc_roundtrip
)

declare -a FAULT_TEST_ORDER=(
  fault_bsr_target_fetch fault_jsr_target_fetch fault_rts_target_fetch fault_rtr_target_fetch fault_rte_return_fetch fault_trap_frame_write
  fault_write_byte_d2 fault_write_byte_postinc moves_dfc_write_fault moves_sfc_read_fault movem_predec_write_fault
)

declare -A TESTS
# Optional per-vector register seeds: D0-D7 A0-A7 [SR].
declare -A INIT_REGS
# Optional per-vector long memory seeds: address/value pairs.
declare -A MEM_LONGS
# Optional per-vector long memory dump addresses appended to the comparison output.
declare -A DUMP_MEM_LONGS
# Optional per-vector test PC override and expected exception/fault injection controls.
declare -A TEST_ADDRS
declare -A EXPECT_EXCEPTION
declare -A CODE_FAULT_ADDR
declare -A DATA_FAULT_ADDR
declare -A DATA_FAULT_SIZE
declare -A DATA_FAULT_WRITE
# Optional per-vector flag: force every store through the special-memory bank-write
# path (B2_JIT_ALL_SPECIAL_MEM=1 on the JIT run). Exercises writemem_special ->
# Uae2026JitBankWriteByOffset (size-selector + 3-arg call), which the default
# writemem_real path doesn't cover.
declare -A ALL_SPECIAL

TESTS[ori_sr_hardfail]="007C 0700"
TESTS[andi_sr_hardfail]="027C 27FF"
TESTS[eori_sr_hardfail]="0A7C 0010"
TESTS[move_from_sr_hardfail]="40C0"
TESTS[move_to_sr_hardfail]="46FC 2500 40C0"
TESTS[sr_ops_combo]="46FC 2700 007C 0010 027C F7FF 0A7C 0004 40C0"

TESTS[scc_vc_vs]="203C 7FFF FFFF 5280 58C1 59C2"
TESTS[dbvc_loop_v_set]="7001 243C 7FFF FFFF 5282 4E71 58C8 FFFA"
TESTS[dbvs_loop_v_clear]="7001 7400 4E71 59C8 FFFA"
TESTS[dbvc_not_taken_v_clear]="7001 7400 58C8 0002 7207"
TESTS[dbvs_not_taken_v_set]="7001 243C 7FFF FFFF 5282 59C8 0002 7207"

TESTS[divs_word_hardfail]="203C 0000 002A 223C 0000 0005 81C1"
TESTS[divu_word_hardfail]="203C 0000 002A 223C 0000 0005 80C1"
TESTS[divs_neg_by_neg_edge]="203C FFFF FFF1 72FD 81C1"
TESTS[divs_by_minus_one_edge]="203C FFFF FFFE 72FF 81C1"
TESTS[divs_zero_dividend_edge]="7000 7205 81C1"
TESTS[divs_overflow_edge]="203C 0001 0000 7201 81C1"
TESTS[divu_exact_edge]="203C 0000 000C 7203 80C1"
TESTS[divu_with_remainder_edge]="203C 0000 000D 7205 80C1"
TESTS[divu_overflow_edge]="203C 0001 0000 7201 80C1"

TESTS[mull_32_hardfail]="203C 0000 0064 223C 0000 0003 4C01 0000"
TESTS[divl_32_hardfail]="203C 0000 012C 223C 0000 000A 4C41 0000"
TESTS[mull_unsigned_32]="203C 0001 0000 223C 0001 0000 4C01 0000"
TESTS[mull_signed_32]="203C FFFF FFFF 223C 0000 0002 4C01 0800"
TESTS[divl_unsigned_32]="203C 0000 012C 223C 0000 000A 4C41 0000"
TESTS[divl_signed_32]="203C FFFF FFF6 223C 0000 0003 4C41 0800"
TESTS[mull_u64]="203C FFFF FFFF 223C 0000 0002 4C01 3402"
TESTS[mull_s32_neg]="203C FFFF FFFF 223C FFFF FFFF 4C00 1800"
TESTS[divl_u32_rem]="203C 0000 0064 223C 0000 0007 4C41 0002"
TESTS[divl_s32_neg]="203C FFFF FF9C 223C 0000 0007 4C41 0802"
TESTS[divl_u32_max]="203C FFFF FFFF 223C 0000 0010 4C41 0002"
TESTS[divl_s32_neg_divisor]="203C 0000 0064 223C FFFF FFF9 4C41 0802"
TESTS[mull_s64_neg]="243C FFFF FF9C 223C 0000 03E8 4C01 2C03"
TESTS[divl_same_dq_dr]="203C 0000 0064 223C 0000 0007 4C41 0000"
TESTS[divl_u64]="243C 0000 0064 263C 0000 0001 223C 0000 0007 4C41 2403"
TESTS[divl_s64]="243C FFFF FF9C 263C FFFF FFFF 223C 0000 0007 4C41 2C03"

TESTS[aslw_mem_hardfail]="41F9 0400 A000 30FC 4000 E1D0 3010"
TESTS[lsrw_mem_hardfail]="41F9 0400 A000 30FC 8001 E2D0 3010"
TESTS[rolw_mem_hardfail]="41F9 0400 A000 30FC 8001 E7D0 3010"
TESTS[asrw_mem_edge]="41F9 0400 A000 30FC 8001 E0D0 3010"
TESTS[roxlw_mem_edge]="41F9 0400 A000 30FC 0001 003C 0010 E5D0 3010"
TESTS[roxrw_mem_edge]="41F9 0400 A000 30FC 8000 003C 0010 E4D0 3010"

# Extend-arithmetic carry/X vectors (gate coverage for the 513b75c fallback
# flag-spill). ori.b #$10,ccr sets X; andi.b #$EF,ccr clears X. Register-form
# checks flags_carry_inverted threading; addx_l_predec_xset routes addx.l
# -(a1),-(a0) through the interpreter FALLBACK (predec (An) stays on fallback
# per compemu_support_arm.cpp:1550) -> the exact seam 513b75c patches, for
# carry/X instead of Z. Result loaded back to d0 for the register-dump compare.
TESTS[addx_l_reg_xset]="203C 0000 0001 223C 0000 0002 003C 0010 D181"
TESTS[subx_l_reg_xset]="203C 0000 0005 223C 0000 0002 003C 0010 9181"
TESTS[negx_l_reg_xset]="203C 0000 0001 003C 0010 4080"
TESTS[addx_l_reg_xclr]="203C 0000 0001 223C 0000 0002 023C 00EF D181"
TESTS[addx_l_predec_xset]="41F9 0400 A010 20BC 0000 0002 43F9 0400 A020 22BC 0000 0001 5888 5889 003C 0010 D189 2010"

# 513b75c SAFETY PROBE (uncovered flag-setting fallback ops, N/Z/C/V/X spread):
# if fix-ON jit == interp for these, the fallback flag-spill does not clobber
# any software-flag handler these exercise. Not in the 81-gate by design.
TESTS[cmpm_l_equal]="41F9 0400 A010 43F9 0400 A020 20BC 0000 0007 22BC 0000 0007 B388 2010"
TESTS[tst_l_postinc_neg]="43F9 0400 A010 22BC FFFF FFFF 4A99 2029 FFFC"
TESTS[neg_l_postinc]="43F9 0400 A010 22BC 0000 0001 4499 2029 FFFC"
TESTS[negx_l_postinc_xset]="43F9 0400 A010 22BC 0000 0000 003C 0010 4099 2029 FFFC"
TESTS[cmp_l_postinc_d1_eq]="43F9 0400 A010 22BC 0000 0042 223C 0000 0042 B299"
TESTS[and_l_postinc_d0]="43F9 0400 A010 22BC 0000 000F 203C 0000 00FF C099"

TESTS[bfextu_reg_edge]="203C ABCD EF01 E9C0 0200"
TESTS[bfexts_reg_edge]="203C ABCD EF01 EBC0 0200"
TESTS[bfffo_reg_edge]="203C 0000 0100 EDC0 0200"
TESTS[bfset_reg_edge]="203C FF00 00FF EEC0 0208"
TESTS[bfclr_reg_edge]="203C FFFF FFFF ECC0 0208"
TESTS[bfchg_reg_edge]="203C FF00 FF00 EAC0 0208"
TESTS[bftst_reg_edge]="203C 8000 0000 E8C0 0008"
TESTS[bfins_reg_edge]="7042 203C FFFF 0000 EFC0 0200"
TESTS[bfins_dreg_imm]="203C 0000 00A5 4281 EFC1 0108"
TESTS[bfins_dreg_narrow]="203C 0000 000F 2200 EFC1 0204"

TESTS[chk2_long_in_range]="41F9 0400 A000 20FC 0000 0005 20FC 0000 000A 41F9 0400 A000 7007 04D0 0800"
TESTS[cas_long_match_update]="41F9 0400 A000 20FC 1111 2222 41F9 0400 A000 203C 1111 2222 223C 3333 4444 0ED0 0040 2010"
TESTS[cas2_word_match_update]="41F9 0400 A000 30FC 1111 41F9 0400 A010 30FC 2222 203C 3333 0000 223C 4444 0000 743C 363C 1111 383C 2222 0CFC 8002 9044 3010 3229 0010"
TESTS[movep_roundtrip]="41F9 0400 A000 203C A1B2 C3D4 01C8 0000 4280 0148 0000"
TESTS[movem_long_predec_roundtrip]="41F9 0400 A020 203C 1111 2222 223C 3333 4444 48E0 C000 4CD8 000C"
TESTS[jsr_an_call_return]="41FA 000A 4E90 7201 6000 0006 702A 4E75 7402"
TESTS[bsr_word_call_return]="6100 0008 7201 6000 0006 702B 4E75 7402"
TESTS[seam_movea_a0_chain]="2050 2008"
INIT_REGS[seam_movea_a0_chain]="04018258 00000009 04018258 00000014 00036074 00000002 00000000 00000000 04018258 050941AD 0000E06C 0000E068 0401AE94 0401B908 03FFFF80 03FFFF58 0010"
MEM_LONGS[seam_movea_a0_chain]="04018258 04018758"
TESTS[seam_a0_a1_chain]="2050 2268 0020 2009"
INIT_REGS[seam_a0_a1_chain]="04018258 00000009 04018258 00000014 00036074 00000002 00000000 00000000 04018258 050941AD 0000E06C 0000E068 0401AE94 0401B908 03FFFF80 03FFFF58 0010"
MEM_LONGS[seam_a0_a1_chain]="04018258 04018758 04018778 050A1BE0"
TESTS[seam_user_stack_push]="2F09 200F 588F"
INIT_REGS[seam_user_stack_push]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 050941AD 00000000 00000000 00000000 00000000 00000000 04010000 0010"
DUMP_MEM_LONGS[seam_user_stack_push]="0400FFFC"
TESTS[seam_movem_restore_frame]="4CDF 4707 2E0F"
INIT_REGS[seam_movem_restore_frame]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
MEM_LONGS[seam_movem_restore_frame]="04010000 04018258 04010004 00000009 04010008 04018258 0401000C 04018258 04010010 050941AD 04010014 0000E06C 04010018 03FFFF80"
TESTS[seam_movem_restore_full_frame]="4CDF 7FFF 2E0F"
INIT_REGS[seam_movem_restore_full_frame]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
MEM_LONGS[seam_movem_restore_full_frame]="04010000 11111111 04010004 22222222 04010008 33333333 0401000C 44444444 04010010 55555555 04010014 66666666 04010018 77777777 0401001C 88888888 04010020 03FFFF6C 04010024 03FFBEA5 04010028 0001AA0C 0401002C 0001AA00 04010030 0001693E 04010034 00000000 04010038 03FFFF90"
TESTS[seam_hash_lookup_chain]="2050 2F09 2268 0020 222F 000C C291 2031 1C08 2040 2008"
INIT_REGS[seam_hash_lookup_chain]="04018258 00000009 04018258 00000014 00036074 00000002 00000000 00000000 04018258 050941AD 0000E06C 0000E068 0401AE94 0401B908 03FFFF80 04010000 0010"
MEM_LONGS[seam_hash_lookup_chain]="04018258 04018758 04018778 050A1BE0 04010008 00000009 050A1BE0 FFFFFFFF 050A1C0C 04019000"
DUMP_MEM_LONGS[seam_hash_lookup_chain]="0400FFFC"
TESTS[seam_jsr_user_stack]="4E90 200F 6004 7C66 4E75 4E71"
INIT_REGS[seam_jsr_user_stack]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 TEST+0006 00000000 00000000 00000000 00000000 00000000 03FFFF80 04010000 0010"
TESTS[seam_hash_call_chain]="2050 2F09 2268 0020 222F 000C C291 2031 1C08 2040 6602 600E 202F 000C 2068 0018 4E90 588F 6008 7E00 6004 7C55 4E75 4E71"
INIT_REGS[seam_hash_call_chain]="04018258 00000009 04018258 00000014 00036074 00000002 00000000 00000000 04018258 050941AD 0000E06C 0000E068 0401AE94 0401B908 03FFFF80 04010000 0010"
MEM_LONGS[seam_hash_call_chain]="04018258 04018758 04018778 050A1BE0 04010008 00000009 050A1BE0 FFFFFFFF 050A1C0C 04019000 04019018 TEST+002A"
DUMP_MEM_LONGS[seam_hash_call_chain]="0400FFFC 0400FFF8"
TESTS[seam_byte_store_d2_fault_shape]="1082 2010"
INIT_REGS[seam_byte_store_d2_fault_shape]="00000000 00000000 00000029 00000000 00000000 00000000 00000000 00000000 0400A000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
MEM_LONGS[seam_byte_store_d2_fault_shape]="0400A000 11223344"
DUMP_MEM_LONGS[seam_byte_store_d2_fault_shape]="0400A000"
TESTS[seam_byte_copy_postinc_fault_shape]="109A 2010"
INIT_REGS[seam_byte_copy_postinc_fault_shape]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 0400A000 00000000 0400A010 00000000 00000000 00000000 00000000 04010000 0010"
MEM_LONGS[seam_byte_copy_postinc_fault_shape]="0400A000 11223344 0400A010 5A667788"
DUMP_MEM_LONGS[seam_byte_copy_postinc_fault_shape]="0400A000 0400A010"

TESTS[pack_dn_edge]="203C 0000 1234 8140 0000"
TESTS[unpk_dn_edge]="203C 0000 0012 8180 0000"
TESTS[moves_write_read]="41F9 0400 A000 203C DEAD BEEF 0E90 0800 2010"
# Regression guard for the special-memory long-store truncation class (the
# NeXTBus card-probe bug fixed in f9167ac): move.l #imm32,(d16,An) into memory,
# forced through the bank-write path (ALL_SPECIAL). Pre-fix the JIT truncated the
# long to a byte (0x84 into the BE MSB -> 0x84000000); interp and the fixed JIT
# both store the full 0x01001c84. lea 0x0400A000,a0; move.l #0x01001c84,0(a0);
# move.l (a0),d0.
TESTS[move_l_imm_special_long]="41F9 0400 A000 217C 0100 1C84 0000 2010"
ALL_SPECIAL[move_l_imm_special_long]=1
DUMP_MEM_LONGS[move_l_imm_special_long]="0400A000"
TESTS[movec_vbr_roundtrip]="203C 1234 0000 4E7B 0801 4E7A 1801"
TESTS[movec_sfc_roundtrip]="7005 4E7B 0000 4E7A 1000"
TESTS[movec_dfc_roundtrip]="7003 4E7B 0001 4E7A 1001"

TESTS[fault_bsr_target_fetch]="6106 7201 6004 702B 4E75 7402"
INIT_REGS[fault_bsr_target_fetch]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
EXPECT_EXCEPTION[fault_bsr_target_fetch]=2
CODE_FAULT_ADDR[fault_bsr_target_fetch]="TEST+0008"
DUMP_MEM_LONGS[fault_bsr_target_fetch]="0400FFFC"

TESTS[fault_jsr_target_fetch]="4E90 7E55"
INIT_REGS[fault_jsr_target_fetch]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 TEST+0100 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
EXPECT_EXCEPTION[fault_jsr_target_fetch]=2
CODE_FAULT_ADDR[fault_jsr_target_fetch]="TEST+0100"
DUMP_MEM_LONGS[fault_jsr_target_fetch]="0400FFFC 04010000"

TESTS[fault_rts_target_fetch]="4E75 7201"
INIT_REGS[fault_rts_target_fetch]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
MEM_LONGS[fault_rts_target_fetch]="04010000 TEST+0100"
EXPECT_EXCEPTION[fault_rts_target_fetch]=2
CODE_FAULT_ADDR[fault_rts_target_fetch]="TEST+0100"
DUMP_MEM_LONGS[fault_rts_target_fetch]="04010000 04010004"

TESTS[fault_rtr_target_fetch]="4E77 7201"
INIT_REGS[fault_rtr_target_fetch]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
MEM_LONGS[fault_rtr_target_fetch]="04010000 00100400 04010002 TEST+0100"
EXPECT_EXCEPTION[fault_rtr_target_fetch]=2
CODE_FAULT_ADDR[fault_rtr_target_fetch]="TEST+0100"
DUMP_MEM_LONGS[fault_rtr_target_fetch]="04010000 04010004"

TESTS[fault_rte_return_fetch]="4E73 7201"
INIT_REGS[fault_rte_return_fetch]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 2700"
MEM_LONGS[fault_rte_return_fetch]="04010000 00100100 04010002 TEST+0100 04010006 00000000"
EXPECT_EXCEPTION[fault_rte_return_fetch]=2
CODE_FAULT_ADDR[fault_rte_return_fetch]="TEST+0100"
DUMP_MEM_LONGS[fault_rte_return_fetch]="04010000 04010004 04010008"

TESTS[fault_trap_frame_write]="4E40 7201"
INIT_REGS[fault_trap_frame_write]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
EXPECT_EXCEPTION[fault_trap_frame_write]=2
DATA_FAULT_ADDR[fault_trap_frame_write]="0400FFF8"
DATA_FAULT_SIZE[fault_trap_frame_write]=W
DATA_FAULT_WRITE[fault_trap_frame_write]=1
DUMP_MEM_LONGS[fault_trap_frame_write]="0400FFF8 0400FFFC"

TESTS[fault_write_byte_d2]="1082 2010"
INIT_REGS[fault_write_byte_d2]="00000000 00000000 00000029 00000000 00000000 00000000 00000000 00000000 0400A000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
MEM_LONGS[fault_write_byte_d2]="0400A000 11223344"
EXPECT_EXCEPTION[fault_write_byte_d2]=2
DATA_FAULT_ADDR[fault_write_byte_d2]="0400A000"
DATA_FAULT_SIZE[fault_write_byte_d2]=B
DATA_FAULT_WRITE[fault_write_byte_d2]=1
DUMP_MEM_LONGS[fault_write_byte_d2]="0400A000"

TESTS[fault_write_byte_postinc]="109A 2010"
INIT_REGS[fault_write_byte_postinc]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 0400A000 00000000 0400A010 00000000 00000000 00000000 00000000 04010000 0010"
MEM_LONGS[fault_write_byte_postinc]="0400A000 11223344 0400A010 5A667788"
EXPECT_EXCEPTION[fault_write_byte_postinc]=2
DATA_FAULT_ADDR[fault_write_byte_postinc]="0400A000"
DATA_FAULT_SIZE[fault_write_byte_postinc]=B
DATA_FAULT_WRITE[fault_write_byte_postinc]=1
DUMP_MEM_LONGS[fault_write_byte_postinc]="0400A000 0400A010"

TESTS[moves_dfc_write_fault]="7001 4E7B 0001 0E90 0800 2010"
INIT_REGS[moves_dfc_write_fault]="DEADBEEF 00000000 00000000 00000000 00000000 00000000 00000000 00000000 0400A000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 2700"
MEM_LONGS[moves_dfc_write_fault]="0400A000 11223344"
EXPECT_EXCEPTION[moves_dfc_write_fault]=2
DATA_FAULT_ADDR[moves_dfc_write_fault]="0400A000"
DATA_FAULT_SIZE[moves_dfc_write_fault]=L
DATA_FAULT_WRITE[moves_dfc_write_fault]=1
DUMP_MEM_LONGS[moves_dfc_write_fault]="0400A000"

TESTS[moves_sfc_read_fault]="7001 4E7B 0000 0E90 0000 2010"
INIT_REGS[moves_sfc_read_fault]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 0400A000 00000000 00000000 00000000 00000000 00000000 00000000 04010000 2700"
MEM_LONGS[moves_sfc_read_fault]="0400A000 11223344"
EXPECT_EXCEPTION[moves_sfc_read_fault]=2
DATA_FAULT_ADDR[moves_sfc_read_fault]="0400A000"
DATA_FAULT_SIZE[moves_sfc_read_fault]=L
DATA_FAULT_WRITE[moves_sfc_read_fault]=0
DUMP_MEM_LONGS[moves_sfc_read_fault]="0400A000"

TESTS[movem_predec_write_fault]="203C 1111 2222 223C 3333 4444 48E0 C000 4CDF 0003"
INIT_REGS[movem_predec_write_fault]="00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 0400A020 00000000 00000000 00000000 00000000 00000000 00000000 04010000 0010"
MEM_LONGS[movem_predec_write_fault]="0400A018 AAAAAAAA 0400A01C BBBBBBBB"
EXPECT_EXCEPTION[movem_predec_write_fault]=2
DATA_FAULT_ADDR[movem_predec_write_fault]="0400A01C"
DATA_FAULT_SIZE[movem_predec_write_fault]=L
DATA_FAULT_WRITE[movem_predec_write_fault]=1
DUMP_MEM_LONGS[movem_predec_write_fault]="0400A018 0400A01C"
