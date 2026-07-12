#ifndef _HOST_SHIM_OSUTILS_H
#define _HOST_SHIM_OSUTILS_H

// stand-in for the Mac Toolbox header of the same name. src/mbc.c uses
// GetDateTime for the MBC3 RTC; the host version is implemented in shims.c
void GetDateTime(unsigned long *secs);

#endif
