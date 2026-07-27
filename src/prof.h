#ifndef _PROF_H
#define _PROF_H

// no-ops unless GB6_PROFILING is defined.

enum {
    PROF_OTHER,     // event handling, pacing, dispatch overhead
    PROF_JIT,       // executing compiled code
    PROF_COMPILE,   // compiling blocks
    PROF_SYNC,      // dmg_sync_hw minus the nested phases below
    PROF_RENDER,    // render_frame minus lcd_draw
    PROF_DRAW,      // convert + blit to screen
    PROF_NUM_PHASES
};

#ifdef GB6_PROFILING

extern volatile unsigned char prof_phase;
extern volatile unsigned long prof_counts[PROF_NUM_PHASES];

void prof_install(void);
void prof_remove(void);

#define PROF_SET(p) (prof_phase = (p))

#else

#define PROF_SET(p) ((void) 0)

#endif

#endif
