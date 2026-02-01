#ifndef _DEBUG_H
#define _DEBUG_H

#include "compiler.h"
#include "dmg.h"

void debug_log_string(const char *str);

void debug_log_block(struct code_block *block);

void debug_dump_vram(struct dmg *dmg);

#endif