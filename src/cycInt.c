/*
  Hatari - cycInt.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This code handles our table with callbacks for cycle accurate program
  interruption. We add any pending callback handler into a table so that we do
  not need to test for every possible interrupt event. We then scan
  the list if used entries in the table and copy the one with the least cycle
  count into the global 'PendingInterrupt' variable. This is then
  decremented by the execution loop - rather than decrement each and every
  entry (as the others cannot occur before this one).
  We support three time units: CPU cycles, ticks, and microseconds.
  Ticks are bound to CPU cycles and run at TICK_RATE MHz. Microseconds are either
  bound to the host CPU's performance counter in real-time mode or to the emulated
  CPU cycles if non-realtime mode.
*/

const char CycInt_fileid[] = "Previous cycInt.c : " __DATE__ " " __TIME__;

#include <stdint.h>
#include <assert.h>
#include "main.h"
#include "cycInt.h"
#include "m68000.h"
#include "screen.h"
#include "video.h"
#include "sysReg.h"
#include "esp.h"
#include "mo.h"
#include "ethernet.h"
#include "dma.h"
#include "floppy.h"
#include "snd.h"
#include "printer.h"
#include "kms.h"
#include "configuration.h"
#include "main.h"
#include "nd_sdl.h"

void (*PendingInterruptFunction)(void);
Sint64 PendingInterruptCounter;
int    usCheckCycles;

Sint64 nCyclesOver;
Sint64 nCyclesMainCounter;         /* Main cycles counter, counts emulated CPU cycles sind reset */


/* List of possible interrupt handlers to be store in 'PendingInterruptTable',
 * used for 'MemorySnapShot' */
static void (* const pIntHandlerFunctions[MAX_INTERRUPTS])(void) =
{
	NULL,
	Video_InterruptHandler_VBL,
	Hardclock_InterruptHandler,
    Mouse_Handler,
    ESP_InterruptHandler,
    ESP_IO_Handler,
    M2MDMA_IO_Handler,
    MO_InterruptHandler,
    MO_IO_Handler,
    ECC_IO_Handler,
    ENET_IO_Handler,
    FLP_IO_Handler,
    SND_Out_Handler,
    SND_In_Handler,
    Printer_IO_Handler,
    Main_EventHandlerInterrupt,
    nd_vbl_handler,
    nd_video_vbl_handler,
};

static INTERRUPTHANDLER InterruptHandlers[MAX_INTERRUPTS];
INTERRUPTHANDLER        PendingInterrupt;
static int              ActiveInterrupt=0;

static void CycInt_SetNewInterrupt(void);

extern Uint8 NEXTRom[0x20000];

/*-----------------------------------------------------------------------*/
/**
 * Reset interrupts, handlers
 */
void CycInt_Reset(void) {
	int i;

	/* Reset counts */
    PendingInterrupt.time = 0;
	ActiveInterrupt       = 0;
	nCyclesOver           = 0;
    nCyclesMainCounter    = 0;
    usCheckCycles         = 0;
        
	/* Reset interrupt table */
	for (i=0; i<MAX_INTERRUPTS; i++) {
		InterruptHandlers[i].type      = CYC_INT_NONE;
		InterruptHandlers[i].time      = INT64_MAX;
		InterruptHandlers[i].pFunction = pIntHandlerFunctions[i];
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Find next interrupt to occur, and store to global variables for decrement
 * in instruction decode loop.
 * (SC) Microseconf interrupts are skipped here and handled in the decode loop.
 */
static void CycInt_SetNewInterrupt(void) {
	Sint64       LowestCycleCount = INT64_MAX;
	interrupt_id LowestInterrupt  = INTERRUPT_NULL;
    
	/* Find next interrupt to go off */
	for(int i = INTERRUPT_NULL+1; i < MAX_INTERRUPTS; i++) {
        /* Is interrupt pending? */
        if(InterruptHandlers[i].type == CYC_INT_CPU && InterruptHandlers[i].time < LowestCycleCount) {
            LowestCycleCount = InterruptHandlers[i].time;
            LowestInterrupt  = i;
        }
	}

	/* Set new counts, active interrupt */
    PendingInterrupt = InterruptHandlers[LowestInterrupt];
	ActiveInterrupt  = LowestInterrupt;
}

/*-----------------------------------------------------------------------*/
/**
 * Adjust all interrupt timings, MUST call CycInt_SetNewInterrupt after this.
 */
static void CycInt_UpdateInterrupt(void) {
	int i;

	/* Adjust table by subtracting cycles that have passed since last update */
	for (i = 0; i < MAX_INTERRUPTS; i++) {
		if (InterruptHandlers[i].type == CYC_INT_CPU)
			InterruptHandlers[i].time -= nCyclesOver;
	}
    nCyclesOver = 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Check all microsecond interrupt timings
 */
bool CycInt_SetNewInterruptUs(void) {
    Sint64 now = host_time_us();
    if (ConfigureParams.System.bRealtime) {
        for(int i = 0; i < MAX_INTERRUPTS; i++) {
            if (InterruptHandlers[i].type == CYC_INT_US && now > InterruptHandlers[i].time) {
                PendingInterrupt = InterruptHandlers[i];
                PendingInterrupt.time = -1;
                ActiveInterrupt       = i;
                return true;
            }
        }
    }
    return false;
}

static int cycint_rate_env = -1;
static int cycint_anchor_env = -1;
static unsigned long cycint_armed[MAX_INTERRUPTS] = {0};
static unsigned long cycint_fired[MAX_INTERRUPTS] = {0};

void CycInt_TimingAnchorReport(void)
{
	fprintf(stderr, "TIMINGANCHOR_CYCINT active=%d ptype=%d ptime=%lld",
		ActiveInterrupt, PendingInterrupt.type, (long long)PendingInterrupt.time);
	for (int i = 0; i < MAX_INTERRUPTS; i++)
		if (cycint_armed[i] || cycint_fired[i])
			fprintf(stderr, " h%d=%lu/%lu", i, cycint_armed[i], cycint_fired[i]);
	fprintf(stderr, "\n");
	fflush(stderr);
}

static void cycint_probe(int kind, int handler)
{
	/* B2_INT_RATE probe: per-handler arm/fire counts for the CycInt queue, so a
	   device timer that stops firing under one engine can be named directly. */
	static time_t last = 0;
	if (cycint_rate_env < 0)
		cycint_rate_env = getenv("B2_INT_RATE") ? 1 : 0;
	if (cycint_anchor_env < 0)
		cycint_anchor_env = getenv("B2_SCSI_TRACE_STOP_AT") ? 1 : 0;
	/* The bounded timing anchor needs exact final counts without periodic
	 * wall-clock reports. Keep counters whenever either diagnostic is armed. */
	if (!cycint_rate_env && !cycint_anchor_env)
		return;
	if (handler >= 0 && handler < MAX_INTERRUPTS) {
		if (kind == 0)
			cycint_armed[handler]++;
		else
			cycint_fired[handler]++;
	}
	if (cycint_rate_env) {
		const time_t now = time(NULL);
		if (now - last >= 2) {
			last = now;
			fprintf(stderr, "CYCINT active=%d ptype=%d ptime=%lld",
				ActiveInterrupt, PendingInterrupt.type,
				(long long)PendingInterrupt.time);
			for (int i = 0; i < MAX_INTERRUPTS; i++)
				if (cycint_armed[i] || cycint_fired[i])
					fprintf(stderr, " h%d=%lu/%lu", i, cycint_armed[i], cycint_fired[i]);
			fprintf(stderr, "\n");
			fflush(stderr);
		}
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Adjust all interrupt timings as 'ActiveInterrupt' has occured, and
 * remove from active list.
 */
void CycInt_AcknowledgeInterrupt(void) {
	cycint_probe(1, ActiveInterrupt);
	/* Update list cycle counts */
	CycInt_UpdateInterrupt();

	/* Disable interrupt entry which has just occured */
	InterruptHandlers[ActiveInterrupt].type = CYC_INT_NONE;

	/* Set new */
	CycInt_SetNewInterrupt();
}

/*-----------------------------------------------------------------------*/
/**
 * Add interrupt to occur from now.
 */
void CycInt_AddRelativeInterruptCycles(Sint64 CycleTime, interrupt_id Handler) {
	assert(CycleTime >= 0);
	cycint_probe(0, (int)Handler);
	{
		/* B2_CYCINT_TRACE=<n>: name the handler and deadline of the first n
		 * armed CPU-cycle interrupts. Two runs of the same engine must produce
		 * an identical stream; anything else means a device deadline is being
		 * computed from something other than emulated time, which makes the
		 * whole run nondeterministic and defeats any engine-to-engine differ. */
		static unsigned long n = 0;
		static unsigned long limit = 0;
		static int initialized = 0;
		extern int64_t nCyclesMainCounter;
		if (!initialized) {
			const char *env = getenv("B2_CYCINT_TRACE");
			limit = (env && *env) ? strtoul(env, NULL, 0) : 0;
			initialized = 1;
		}
		if (n < limit) {
			++n;
			fprintf(stderr, "CYCARM %lu handler=%d cycles=%lld total=%lld pc=%08x\n",
				n, (int)Handler, (long long)CycleTime,
				(long long)nCyclesMainCounter, (unsigned)m68k_getpc());
			fflush(stderr);
		}
	}

	/* Update list cycle counts with current PendingInterruptCount before adding a new int, */
	/* because CycInt_SetNewInterrupt can change the active int / PendingInterruptCount */
	if ( ActiveInterrupt > 0 )
		CycInt_UpdateInterrupt();

	InterruptHandlers[Handler].type = CYC_INT_CPU;
	InterruptHandlers[Handler].time = CycleTime;

	/* Set new active int and compute a new value for PendingInterruptCount*/
	CycInt_SetNewInterrupt();
}

/*-----------------------------------------------------------------------*/
/**
 * Add interrupt to occur us microsencods from now
 * Use usreal if we are in realtime mode
 */
void CycInt_AddRelativeInterruptUs(Sint64 us, Sint64 usreal, interrupt_id Handler) {
    assert(us >= 0);
    
    if(ConfigureParams.System.bRealtime) {
        /* Update list cycle counts with current PendingInterruptCount before adding a new int, */
        /* because CycInt_SetNewInterrupt can change the active int / PendingInterruptCount */
        if ( ActiveInterrupt > 0 ) CycInt_UpdateInterrupt();
        
        if ( usreal > 0 ) us = usreal;
        
        InterruptHandlers[Handler].type = CYC_INT_US;
        InterruptHandlers[Handler].time = host_time_us() + us;
        
        /* Set new active int and compute a new value for PendingInterruptCount*/
        CycInt_SetNewInterrupt();
    } else {
        CycInt_AddRelativeInterruptCycles(us * ConfigureParams.System.nCpuFreq, Handler);
    }
}

/*-----------------------------------------------------------------------*/
/**
 * Add interrupt to occur microseconds from now. Convert to cycles.
 * Use UsTimeFast if we are in realtime mode.
 */
void CycInt_AddRelativeInterruptUsCycles(Sint64 us, Sint64 usreal, interrupt_id Handler) {
    
    if (ConfigureParams.System.bRealtime && usreal > 0) {
       us = usreal;
    }

    CycInt_AddRelativeInterruptCycles(us * ConfigureParams.System.nCpuFreq, Handler);
}

/*-----------------------------------------------------------------------*/
/**
 * Remove a pending interrupt from our table
 */
void CycInt_RemovePendingInterrupt(interrupt_id Handler) {
	/* Update list cycle counts, including the handler we want to remove */
	/* to be able to resume it later */
	CycInt_UpdateInterrupt();

	/* Stop interrupt after CycInt_UpdateInterrupt */
	InterruptHandlers[Handler].type = CYC_INT_NONE;

	/* Set new */
	CycInt_SetNewInterrupt();
}


/*-----------------------------------------------------------------------*/
/**
 * Return true if interrupt is active in list
 */
bool CycInt_InterruptActive(interrupt_id Handler)
{
    return InterruptHandlers[Handler].type != CYC_INT_NONE;
}
