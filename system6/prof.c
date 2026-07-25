/* Game Boy emulator for 68k Macs
   prof.c - 1 khz phase sampler for wall-clock profiling */

#include <Timer.h>

#include "../src/prof.h"

volatile unsigned char prof_phase;
volatile unsigned long prof_counts[PROF_NUM_PHASES];

static TMTask prof_task;
static int prof_installed;

static pascal void prof_tick(void)
{
  prof_counts[prof_phase]++;
  PrimeTime((QElemPtr) &prof_task, 1);
}

void prof_install(void)
{
  if (prof_installed)
    return;

  prof_task.tmAddr = (TimerUPP) prof_tick;
  prof_task.tmCount = 0;
  prof_task.tmWakeUp = 0;
  prof_task.tmReserved = 0;
  InsTime((QElemPtr) &prof_task);
  PrimeTime((QElemPtr) &prof_task, 1);
  prof_installed = 1;
}

void prof_remove(void)
{
  if (!prof_installed)
    return;

  RmvTime((QElemPtr) &prof_task);
  prof_installed = 0;
}
