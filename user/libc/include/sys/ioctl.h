#pragma once

/* Device-control compatibility surface.
 *
 * The spelling exists for porting; RIX_SYS_IOCTL has no kernel handler
 * and this wrapper always fails closed with ENOSYS. Terminal control
 * remains termios-over-TTTY (see Phase 17); no ioctl command set is
 * defined yet. */

int ioctl(int fd, unsigned long command, ...);
