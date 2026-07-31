/* Game Boy emulator for 68k Macs
   blocklist.h - persist compiled block addresses across sessions */

#ifndef _BLOCKLIST_H
#define _BLOCKLIST_H

#include "dmg.h"

void blocklist_save(struct dmg *dmg, const char *title);
void blocklist_load(struct dmg *dmg, const char *title);

#endif
