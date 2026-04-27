/*
  Hatari - m68000.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

*/


const char M68000_fileid[] = "Hatari m68000.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "configuration.h"
#include "hatari-glue.h"
#include "cycInt.h"
#include "m68000.h"
#include "options.h"
#include "nextMemory.h"

#include "mmu_common.h"
#include "uae2026_jit_bridge.h"
#include "uae2026_opcode_test.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

Uint32 BusErrorAddress;         /* Stores the offending address for bus-/address errors */
Uint32 BusErrorPC;              /* Value of the PC when bus error occurs */
bool bBusErrorReadWrite;        /* 0 for write error, 1 for read error */
int BusMode = BUS_MODE_CPU;	/* Used to tell which part is owning the bus (cpu, blitter, ...) */

int LastOpcodeFamily = i_NOP;	/* see the enum in readcpu.h i_XXX */
int LastInstrCycles = 0;	/* number of cycles for previous instr. (not rounded to 4) */
int Pairing = 0;		/* set to 1 if the latest 2 intr paired */
char PairingArray[ MAX_OPCODE_FAMILY ][ MAX_OPCODE_FAMILY ];


/* to convert the enum from OpcodeFamily to a readable value for pairing's debug */
const char *OpcodeName[] = { "ILLG",
	"OR","AND","EOR","ORSR","ANDSR","EORSR",
	"SUB","SUBA","SUBX","SBCD",
	"ADD","ADDA","ADDX","ABCD",
	"NEG","NEGX","NBCD","CLR","NOT","TST",
	"BTST","BCHG","BCLR","BSET",
	"CMP","CMPM","CMPA",
	"MVPRM","MVPMR","MOVE","MOVEA","MVSR2","MV2SR",
	"SWAP","EXG","EXT","MVMEL","MVMLE",
	"TRAP","MVR2USP","MVUSP2R","RESET","NOP","STOP","RTE","RTD",
	"LINK","UNLK",
	"RTS","TRAPV","RTR",
	"JSR","JMP","BSR","Bcc",
	"LEA","PEA","DBcc","Scc",
	"DIVU","DIVS","MULU","MULS",
	"ASR","ASL","LSR","LSL","ROL","ROR","ROXL","ROXR",
	"ASRW","ASLW","LSRW","LSLW","ROLW","RORW","ROXLW","ROXRW",
	"CHK","CHK2",
	"MOVEC2","MOVE2C","CAS","CAS2","DIVL","MULL",
	"BFTST","BFEXTU","BFCHG","BFEXTS","BFCLR","BFFFO","BFSET","BFINS",
	"PACK","UNPK","TAS","BKPT","CALLM","RTM","TRAPcc","MOVES",
	"FPP","FDBcc","FScc","FTRAPcc","FBcc","FSAVE","FRESTORE",
	"CINVL","CINVP","CINVA","CPUSHL","CPUSHP","CPUSHA","MOVE16",
	"MMUOP"
};


/*-----------------------------------------------------------------------*/
/**
 * Add pairing between all the bit shifting instructions and a given Opcode
 */

static void M68000_InitPairing_BitShift ( int OpCode )
{
	PairingArray[  i_ASR ][ OpCode ] = 1; 
	PairingArray[  i_ASL ][ OpCode ] = 1; 
	PairingArray[  i_LSR ][ OpCode ] = 1; 
	PairingArray[  i_LSL ][ OpCode ] = 1; 
	PairingArray[  i_ROL ][ OpCode ] = 1; 
	PairingArray[  i_ROR ][ OpCode ] = 1; 
	PairingArray[ i_ROXR ][ OpCode ] = 1; 
	PairingArray[ i_ROXL ][ OpCode ] = 1; 
}


/**
 * Init the pairing matrix
 * Two instructions can pair if PairingArray[ LastOpcodeFamily ][ OpcodeFamily ] == 1
 */
static void M68000_InitPairing(void)
{
	/* First, clear the matrix (pairing is false) */
	memset(PairingArray , 0 , MAX_OPCODE_FAMILY * MAX_OPCODE_FAMILY);

	/* Set all valid pairing combinations to 1 */
	PairingArray[  i_EXG ][ i_DBcc ] = 1;
	PairingArray[  i_EXG ][ i_MOVE ] = 1;
	PairingArray[  i_EXG ][ i_MOVEA] = 1;

	PairingArray[ i_CMPA ][  i_Bcc ] = 1;
	PairingArray[  i_CMP ][  i_Bcc ] = 1;

	M68000_InitPairing_BitShift ( i_DBcc );
	M68000_InitPairing_BitShift ( i_MOVE );
	M68000_InitPairing_BitShift ( i_MOVEA );
	M68000_InitPairing_BitShift ( i_LEA );
	M68000_InitPairing_BitShift ( i_JMP );

	PairingArray[ i_MULU ][ i_MOVEA] = 1; 
	PairingArray[ i_MULS ][ i_MOVEA] = 1; 
	PairingArray[ i_MULU ][ i_MOVE ] = 1; 
	PairingArray[ i_MULS ][ i_MOVE ] = 1; 

	PairingArray[ i_MULU ][ i_DIVU ] = 1;
	PairingArray[ i_MULU ][ i_DIVS ] = 1;
	PairingArray[ i_MULS ][ i_DIVU ] = 1;
	PairingArray[ i_MULS ][ i_DIVS ] = 1;

	PairingArray[ i_BTST ][  i_Bcc ] = 1;

	M68000_InitPairing_BitShift ( i_ADD );
	M68000_InitPairing_BitShift ( i_SUB );
	M68000_InitPairing_BitShift ( i_OR );
	M68000_InitPairing_BitShift ( i_AND );
	M68000_InitPairing_BitShift ( i_EOR );
	M68000_InitPairing_BitShift ( i_NOT );
	M68000_InitPairing_BitShift ( i_CLR );
	M68000_InitPairing_BitShift ( i_NEG );
	M68000_InitPairing_BitShift ( i_ADDX );
	M68000_InitPairing_BitShift ( i_SUBX );
	M68000_InitPairing_BitShift ( i_ABCD );
	M68000_InitPairing_BitShift ( i_SBCD );

	PairingArray[ i_ADD ][ i_MOVE ] = 1;		/* when using xx(an,dn) addr mode */
	PairingArray[ i_SUB ][ i_MOVE ] = 1;
}


/**
 * One-time CPU initialization.
 */
void M68000_Init(void)
{
	/* Init UAE CPU core */
	Init680x0();

	/* Init the pairing matrix */
	M68000_InitPairing();
}

static int pendingInterrupts = 0;
static bool opcode_test_mode_active = false;

static bool opcode_test_dump_enabled(void)
{
	static int cached = -1;
	if (cached < 0) {
		const char *env = getenv("B2_TEST_DUMP");
		cached = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
	}
	return cached != 0;
}

static bool opcode_test_parse_words(const char *hex, Uint16 *out_words, size_t max_words, size_t *out_count)
{
	size_t n = 0;
	const char *p = hex;
	while (*p) {
		char *end;
		unsigned long v;
		while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == ';' || *p == ':'))
			p++;
		if (!*p)
			break;
		if (n >= max_words)
			return false;
		end = NULL;
		v = strtoul(p, &end, 16);
		if (end == p || v > 0xffffUL)
			return false;
		out_words[n++] = (Uint16)v;
		p = end;
	}
	*out_count = n;
	return n > 0;
}

static bool opcode_test_parse_longs(const char *hex, Uint32 *out_longs, size_t max_longs, size_t *out_count)
{
	size_t n = 0;
	const char *p = hex;
	while (*p) {
		char *end;
		unsigned long v;
		while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == ';' || *p == ':'))
			p++;
		if (!*p)
			break;
		if (n >= max_longs)
			return false;
		end = NULL;
		v = strtoul(p, &end, 16);
		if (end == p || v > 0xffffffffUL)
			return false;
		out_longs[n++] = (Uint32)v;
		p = end;
	}
	*out_count = n;
	return n > 0;
}

bool Uae2026OpcodeTestModeSetup(void)
{
	const char *hex = getenv("B2_TEST_HEX");
	const char *init = getenv("B2_TEST_INIT");
	const uaecptr test_addr = 0x01001000;
	const uaecptr stack_addr = 0x04010000;
	const Uint32 rom_offset = 0x1000;
	Uint16 words[1024];
	Uint32 init_words[17];
	size_t n_words = 0;
	size_t init_count = 0;
	int i;

	opcode_test_mode_active = false;
	if (!(hex && *hex))
		return false;
	if (!opcode_test_parse_words(hex, words, sizeof(words) / sizeof(words[0]), &n_words)) {
		fprintf(stderr, "B2_TEST_HEX parse failed\n");
		return false;
	}

	for (i = 0; i < (int)n_words; i++) {
		NEXTRom[rom_offset + (Uint32)(i * 2)] = (Uint8)(words[i] >> 8);
		NEXTRom[rom_offset + (Uint32)(i * 2) + 1] = (Uint8)(words[i] & 0xff);
	}
	NEXTRom[rom_offset + (Uint32)(n_words * 2)] = 0x4e;
	NEXTRom[rom_offset + (Uint32)(n_words * 2) + 1] = 0x72;
	NEXTRom[rom_offset + (Uint32)(n_words * 2) + 2] = 0x27;
	NEXTRom[rom_offset + (Uint32)(n_words * 2) + 3] = 0x00;

	for (i = 0; i < 8; i++) {
		m68k_dreg(regs, i) = 0;
		m68k_areg(regs, i) = 0;
	}
	m68k_areg(regs, 7) = stack_addr;
	regs.usp = stack_addr;
	regs.isp = stack_addr;
	regs.msp = stack_addr;
	regs.sr = 0x2700;
	MakeFromSR();

	if (init && *init) {
		if (!opcode_test_parse_longs(init, init_words, sizeof(init_words) / sizeof(init_words[0]), &init_count) ||
			(init_count != 16 && init_count != 17)) {
			fprintf(stderr, "B2_TEST_INIT parse failed (need 16 or 17 hex words)\n");
			return false;
		}
		for (i = 0; i < 8; i++)
			m68k_dreg(regs, i) = init_words[i];
		for (i = 0; i < 8; i++)
			m68k_areg(regs, i) = init_words[8 + i];
		if (init_count == 17)
			regs.sr = (uae_u16)(init_words[16] & 0xffff);
		regs.usp = m68k_areg(regs, 7);
		regs.isp = m68k_areg(regs, 7);
		regs.msp = m68k_areg(regs, 7);
		MakeFromSR();
	}

	regs.stopped = 0;
	unset_special(SPCFLAG_STOP | SPCFLAG_BRK | SPCFLAG_DOTRACE | SPCFLAG_TRACE | SPCFLAG_DEBUGGER);
	m68k_setpc(test_addr);
	opcode_test_mode_active = true;
	return true;
}

bool Uae2026OpcodeTestModeActive(void)
{
	return opcode_test_mode_active;
}

void Uae2026OpcodeTestModeFinish(void)
{
	if (!opcode_test_mode_active)
		return;
	opcode_test_mode_active = false;
	if (opcode_test_dump_enabled()) {
		MakeSR();
		fprintf(stderr,
			"REGDUMP: D0=%08x D1=%08x D2=%08x D3=%08x D4=%08x D5=%08x D6=%08x D7=%08x "
			"A0=%08x A1=%08x A2=%08x A3=%08x A4=%08x A5=%08x A6=%08x A7=%08x SR=%04x PC=%08x\n",
			(unsigned)m68k_dreg(regs, 0), (unsigned)m68k_dreg(regs, 1),
			(unsigned)m68k_dreg(regs, 2), (unsigned)m68k_dreg(regs, 3),
			(unsigned)m68k_dreg(regs, 4), (unsigned)m68k_dreg(regs, 5),
			(unsigned)m68k_dreg(regs, 6), (unsigned)m68k_dreg(regs, 7),
			(unsigned)m68k_areg(regs, 0), (unsigned)m68k_areg(regs, 1),
			(unsigned)m68k_areg(regs, 2), (unsigned)m68k_areg(regs, 3),
			(unsigned)m68k_areg(regs, 4), (unsigned)m68k_areg(regs, 5),
			(unsigned)m68k_areg(regs, 6), (unsigned)m68k_areg(regs, 7),
			(unsigned)regs.sr, (unsigned)m68k_getpc());
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Reset CPU 68000 variables
 */
void M68000_Reset(bool bCold) {
    pendingInterrupts = 0;
    if (bCold) {
        /* Clear registers, but we need to keep SPCFLAG_MODE_CHANGE and SPCFLAG_BRK unchanged */
        int spcFlags = regs.spcflags & (SPCFLAG_MODE_CHANGE | SPCFLAG_BRK);
        memset(&regs, 0, sizeof(regs));
        regs.spcflags = spcFlags;
    }
    /* Now reset the WINUAE CPU core */
    m68k_reset(bCold);
    BusMode = BUS_MODE_CPU;
}


/*-----------------------------------------------------------------------*/
/**
 * Stop 680x0 emulation
 */
void M68000_Stop(void)
{
    M68000_SetSpecial(SPCFLAG_BRK);
}


/*-----------------------------------------------------------------------*/
/**
 * Start 680x0 emulation
 */
void M68000_Start(void)
{
	if (Uae2026OpcodeTestModeSetup()) {
		m68k_go(true);
		Uae2026OpcodeTestModeFinish();
		_Exit(0);
	}
	m68k_go(true);
}


/*-----------------------------------------------------------------------*/
/**
 * Check whether CPU settings have been changed.
 */
void M68000_CheckCpuSettings(void)
{
    if (ConfigureParams.System.nCpuFreq < 20)
    {
        ConfigureParams.System.nCpuFreq = 16;
    }
    else if (ConfigureParams.System.nCpuFreq < 24)
    {
        ConfigureParams.System.nCpuFreq = 20;
    }
    else if (ConfigureParams.System.nCpuFreq < 32)
    {
        ConfigureParams.System.nCpuFreq = 25;
    }
    else if (ConfigureParams.System.nCpuFreq < 40)
    {
        ConfigureParams.System.nCpuFreq = 33;
    } else {
        if (ConfigureParams.System.bTurbo) {
            ConfigureParams.System.nCpuFreq = 40;
        } else {
            ConfigureParams.System.nCpuFreq = 33;
        }
    }
	changed_prefs.cpu_level = ConfigureParams.System.nCpuLevel;
	changed_prefs.cpu_compatible = ConfigureParams.System.bCompatibleCpu;

	switch (changed_prefs.cpu_level) {
		case 0 : changed_prefs.cpu_model = 68000; break;
		case 1 : changed_prefs.cpu_model = 68010; break;
		case 2 : changed_prefs.cpu_model = 68020; break;
		case 3 : changed_prefs.cpu_model = 68030; break;
		case 4 : changed_prefs.cpu_model = 68040; break;
		case 5 : changed_prefs.cpu_model = 68060; break;
		default: fprintf (stderr, "Init680x0() : Error, cpu_level unknown\n");
	}

    changed_prefs.fpu_model = ConfigureParams.System.n_FPUType;
    switch (changed_prefs.fpu_model) {
        case 68881: changed_prefs.fpu_revision = 0x1f; break;
        case 68882: changed_prefs.fpu_revision = 0x20; break;
        case 68040:
            if (ConfigureParams.System.bTurbo)
                changed_prefs.fpu_revision = 0x41;
            else
                changed_prefs.fpu_revision = 0x40;
            break;
		default: fprintf (stderr, "Init680x0() : Error, fpu_model unknown\n");
    }

	changed_prefs.fpu_strict = ConfigureParams.System.bCompatibleFPU;
	changed_prefs.mmu_model = ConfigureParams.System.bMMU?changed_prefs.cpu_model:0;

	if (table68k)
		check_prefs_changed_cpu();
}

/*-----------------------------------------------------------------------*/
/**
 * BUSERROR - Access outside valid memory range.
 * Use bRead = 0 for write errors and bRead = 1 for read errors!
 */
void M68000_BusError(Uint32 addr, bool bRead)
{
	exception2 (addr, bRead, 0, regs.s ? 5 : 1); /* assumes data access,
                                                  size not set */
}
#if 0
void M68000_BusError(Uint32 addr, bool bRead)
{
	/* FIXME: In prefetch mode, m68k_getpc() seems already to point to the next instruction */
	// BusErrorPC = M68000_GetPC();		/* [NP] We set BusErrorPC in m68k_run_1 */

	
	if ((regs.spcflags & SPCFLAG_BUSERROR) == 0)	/* [NP] Check that the opcode has not already generated a read bus error */
	{
        regs.mmu_fault_addr = addr;
		BusErrorAddress = addr;				/* Store for exception frame */
		bBusErrorReadWrite = bRead;

        if (currprefs.mmu_model) {
            /* This is a hack for the special status word, this needs to be corrected later */
            if (ConfigureParams.System.nCpuLevel==3) { /* CPU 68030 */
                int fc = 5; /* hack */
                regs.mmu_ssw = (fc&1) ? MMU030_SSW_DF : (MMU030_SSW_FB|MMU030_SSW_RB);
                regs.mmu_ssw |= bRead ? MMU030_SSW_RW : 0;
                regs.mmu_ssw |= fc&MMU030_SSW_FC_MASK;
                /*switch (size) {
                 case 4: regs.mmu_ssw |= MMU030_SSW_SIZE_L; break;
                 case 2: regs.mmu_ssw |= MMU030_SSW_SIZE_W; break;
                 case 1: regs.mmu_ssw |= MMU030_SSW_SIZE_B; break;
                 default: break;
                 }*/
                printf("Bus Error: Warning! Using hacked SSW (%04X)!\n", regs.mmu_ssw);
            }
            THROW(2);
            return;
        }

		M68000_SetSpecial(SPCFLAG_BUSERROR);		/* The exception will be done in newcpu.c */
	}
}
#endif

/*-----------------------------------------------------------------------*/
/**
 * Exception handler
 */
void M68000_Exception(Uint32 ExceptionVector , int ExceptionSource)
{
	int exceptionNr = ExceptionVector/4;

	if ((ExceptionSource == M68000_EXC_SRC_AUTOVEC)
		&& (exceptionNr>24 && exceptionNr<32))	/* 68k autovector interrupt? */
	{
		/* Handle autovector interrupts the UAE's way
		 * (see intlev() and do_specialties() in UAE CPU core) */
		int intnr = exceptionNr - 24;
		pendingInterrupts |= (1 << intnr);
		M68000_SetSpecial(SPCFLAG_INT);
	}

	else							/* direct CPU exceptions */
	{
		Uint16 SR;

		/* Was the CPU stopped, i.e. by a STOP instruction? */
		if (regs.spcflags & SPCFLAG_STOP)
		{
			regs.stopped = 0;
			M68000_UnsetSpecial(SPCFLAG_STOP);    /* All is go,go,go! */
		}

		/* 68k exceptions are handled by Exception() of the UAE CPU core */
		Exception(exceptionNr/*, m68k_getpc(), ExceptionSource*/);

		SR = M68000_GetSR();

		/* Set Status Register so interrupt can ONLY be stopped by another interrupt
		 * of higher priority! */
		
		SR = (SR&SR_CLEAR_IPL)|0x0600;     /* DSP, level 6 */
        
        M68000_SetSR(SR);
	}
}