#include <Events.h>

#include "../src/dmg.h"
#include "emulator.h"
#include "input.h"
#include "dialogs.h"

// indices match keyMappings order
// (up, down, left, right, a, b, select, start)
static struct {
  int button;
  int field;
} buttonMap[8] = {
  { BUTTON_UP, FIELD_JOY },
  { BUTTON_DOWN, FIELD_JOY },
  { BUTTON_LEFT, FIELD_JOY },
  { BUTTON_RIGHT, FIELD_JOY },
  { BUTTON_A, FIELD_ACTION },
  { BUTTON_B, FIELD_ACTION },
  { BUTTON_SELECT, FIELD_ACTION },
  { BUTTON_START, FIELD_ACTION }
};

// edge-detect against the last poll
void PollGameInput(void)
{
  KeyMap keys;
  unsigned char *raw = (unsigned char *) keys;
  static unsigned char prev[8];
  int k;

  GetKeys(keys);
  for (k = 0; k < 8; k++) {
    int code = keyMappings[k];
    unsigned char down = (raw[code >> 3] >> (code & 7)) & 1;

    if (down == prev[k]) {
      continue;
    }
    prev[k] = down;
    dmg_set_button(&dmg, buttonMap[k].field, buttonMap[k].button, down);
  }
}
