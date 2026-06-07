#pragma once

#include <_cheader.h>

_Begin_C_Header

#define RB_AUTOBOOT   0x0
#define RB_POWER_OFF  0x4C4557 /* 'W' << 16 | 'E' << 8 | 'L' - Linux compatible magic */

/**
 * Reboot or power off the system.
 * @param cmd  RB_AUTOBOOT to reboot, RB_POWER_OFF to shut down
 * @return 0 on success, negative errno on failure
 */
extern int reboot(int);

_End_C_Header
