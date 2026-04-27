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
  bfextu_reg_edge bfexts_reg_edge bfffo_reg_edge bfset_reg_edge bfclr_reg_edge bfchg_reg_edge bftst_reg_edge bfins_reg_edge bfins_dreg_imm bfins_dreg_narrow
  pack_dn_edge unpk_dn_edge moves_write_read movec_vbr_roundtrip movec_sfc_roundtrip movec_dfc_roundtrip
)

declare -A TESTS

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

TESTS[pack_dn_edge]="203C 0000 1234 8140 0000"
TESTS[unpk_dn_edge]="203C 0000 0012 8180 0000"
TESTS[moves_write_read]="41F9 0400 A000 203C DEAD BEEF 0E90 0800 2010"
TESTS[movec_vbr_roundtrip]="203C 1234 0000 4E7B 0801 4E7A 1801"
TESTS[movec_sfc_roundtrip]="7005 4E7B 0000 4E7A 1000"
TESTS[movec_dfc_roundtrip]="7003 4E7B 0001 4E7A 1001"
