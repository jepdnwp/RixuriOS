#pragma once

/* Device-control compatibility surface.
 *
 * The initial kernel command set serves standard console descriptors. */
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
struct termios { unsigned int c_lflag; };
#define ICANON 0x0001u
#define ECHO   0x0002u
#define ISIG   0x0004u
#define IUTF8  0x0008u
#define TCGETS       0x5401UL
#define TCSETS       0x5402UL
#define TIOCGWINSZ   0x5413UL
#define TIOCSWINSZ   0x5414UL

int ioctl(int fd, unsigned long command, ...);
