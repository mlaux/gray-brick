#include <Patches.h>

#include "cpu_cache.h"

// from Inside Macintosh: OS Utilities
// "Determining If a System Software Routine is Available"
Boolean TrapAvailable(short trapWord)
{
  TrapType trType;

  /* determine whether it is an Operating System or Toolbox routine */
  if ((trapWord & 0x0800) == 0) {
    trType = OSTrap;
  } else {
    trType = ToolTrap;
  }

  /* filter cases where older systems mask with $1FF rather than $3FF */
  if (
    trType == ToolTrap
      && (trapWord & 0x03ff) >= 0x200
      && GetToolboxTrapAddress(0xa86e) == GetToolboxTrapAddress(0xaa6e)
  ) {
      return false;
  }

  return NGetTrapAddress(trapWord, trType) != GetToolboxTrapAddress(_Unimplemented);
}
