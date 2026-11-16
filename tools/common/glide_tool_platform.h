/*
 * Portable C99 platform helpers for glide-ng diagnostic & benchmark tools.
 * Supports Linux (POSIX), DOS (Open Watcom v2 + CauseWay), and Windows (Win32).
 */
#ifndef GLIDE_TOOL_PLATFORM_H
#define GLIDE_TOOL_PLATFORM_H

#if defined(__linux__) || defined(__unix__)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#  endif
#endif

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__DOS__) || defined(__MSDOS__)
#  include <conio.h>
#  include <dos.h>
#  include <time.h>
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <conio.h>
#  include <windows.h>
#else
#  include <sys/select.h>
#  include <termios.h>
#  include <time.h>
#  include <unistd.h>
#endif

/* --- Platform identification string --- */
static inline const char* gtool_platform_name(void) {
#if defined(__DOS__) || defined(__MSDOS__)
  return "DOS/32 (CauseWay)";
#elif defined(_WIN32)
  return "Windows";
#elif defined(__linux__)
  return "Linux";
#else
  return "Unix";
#endif
}

/* --- Portable case-insensitive comparisons --- */
static inline int gtool_strcasecmp(const char* a, const char* b) {
  unsigned char ca, cb;
  if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
  while (*a && *b) {
    ca = (unsigned char)tolower((unsigned char)*a++);
    cb = (unsigned char)tolower((unsigned char)*b++);
    if (ca != cb) return (int)ca - (int)cb;
  }
  return (int)(unsigned char)tolower((unsigned char)*a) -
         (int)(unsigned char)tolower((unsigned char)*b);
}

static inline int gtool_strncasecmp(const char* a, const char* b, size_t n) {
  unsigned char ca, cb;
  size_t i;
  if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
  for (i = 0; i < n; ++i) {
    ca = (unsigned char)tolower((unsigned char)a[i]);
    cb = (unsigned char)tolower((unsigned char)b[i]);
    if (ca != cb) return (int)ca - (int)cb;
    if (ca == '\0') return 0;
  }
  return 0;
}

/* --- Environment block access --- */
#if defined(_WIN32)
/* _environ declared in <stdlib.h> */
static inline char** gtool_get_environ(void) { return _environ; }
#elif defined(__DOS__) || defined(__MSDOS__)
/* Watcom declares environ in <stdlib.h> */
static inline char** gtool_get_environ(void) { return environ; }
#else
extern char** environ;
static inline char** gtool_get_environ(void) { return environ; }
#endif

/* --- Monotonic high-resolution timer (seconds) --- */
#if defined(__DOS__) || defined(__MSDOS__)
static double gtool_dos_last_sec = 0.0;
static int gtool_dos_timer_inited = 0;
static unsigned long gtool_dos_base_tick = 0;

/* Read BIOS Data Area 18.2065 Hz tick counter (0x46C) + 8254 PIT channel 0
 * latch (1.1931816667 MHz) for ~0.84 us resolution under real DOS and DOSBox.
 * Falls back cleanly to monotonic clamping if PIT counter wraps mid-read.
 */
static inline double gtool_time_sec(void) {
  volatile unsigned long* bda_tick = (volatile unsigned long*)0x0000046CUL;
  unsigned long t1, t2;
  unsigned int lo, hi, cnt;
  unsigned long delta_ticks;
  double pit_ticks;
  double sec;

  t1 = *bda_tick;
  outp(0x43, 0x00);
  lo = (unsigned int)inp(0x40);
  hi = (unsigned int)inp(0x40);
  t2 = *bda_tick;
  if (t2 != t1) {
    /* Tick rolled over during PIT read: re-read once for consistency */
    t1 = t2;
    outp(0x43, 0x00);
    lo = (unsigned int)inp(0x40);
    hi = (unsigned int)inp(0x40);
    t2 = *bda_tick;
  }
  cnt = (hi << 8) | lo;

  if (!gtool_dos_timer_inited) {
    gtool_dos_base_tick = t1;
    gtool_dos_timer_inited = 1;
  }

  delta_ticks = t1 - gtool_dos_base_tick;
  pit_ticks = (double)delta_ticks * 65536.0 + (double)(65536U - cnt);
  sec = pit_ticks / 1193181.6666667;

  if (sec < gtool_dos_last_sec) {
    /* Guard against rare PIT mode-3 half-period or emulator jitter */
    sec = gtool_dos_last_sec;
  } else {
    gtool_dos_last_sec = sec;
  }
  return sec;
}
#elif defined(_WIN32)
static inline double gtool_time_sec(void) {
  static LARGE_INTEGER s_freq = {0};
  static int s_inited = 0;
  LARGE_INTEGER now;
  if (!s_inited) {
    QueryPerformanceFrequency(&s_freq);
    s_inited = 1;
  }
  QueryPerformanceCounter(&now);
  return (s_freq.QuadPart > 0) ? ((double)now.QuadPart / (double)s_freq.QuadPart)
                               : 0.0;
}
#else
static inline double gtool_time_sec(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0.0;
  }
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

/* --- Non-blocking keyboard & pause helpers --- */
#if defined(__DOS__) || defined(__MSDOS__) || defined(_WIN32)
static inline void gtool_console_begin(void) {}
static inline void gtool_console_end(void) {}

static inline int gtool_poll_key(void) {
  if (kbhit()) {
    int ch = getch();
    if (ch == 0 || ch == 0xE0) {
      /* Consume extended scan code */
      (void)getch();
      return -1;
    }
    return ch;
  }
  return 0;
}

static inline void gtool_wait_keypress(void) {
  printf("\nPress any key to continue...");
  fflush(stdout);
  while (kbhit()) {
    (void)getch();
  }
  (void)getch();
  printf("\n");
}
#else
static struct termios gtool_saved_termios;
static int gtool_termios_active = 0;

static inline void gtool_console_begin(void) {
  struct termios raw;
  if (!isatty(STDIN_FILENO) || gtool_termios_active) return;
  if (tcgetattr(STDIN_FILENO, &gtool_saved_termios) == 0) {
    raw = gtool_saved_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
      gtool_termios_active = 1;
    }
  }
}

static inline void gtool_console_end(void) {
  if (gtool_termios_active) {
    tcsetattr(STDIN_FILENO, TCSANOW, &gtool_saved_termios);
    gtool_termios_active = 0;
  }
}

static inline int gtool_poll_key(void) {
  fd_set rfds;
  struct timeval tv;
  unsigned char ch = 0;
  if (!isatty(STDIN_FILENO)) return 0;
  FD_ZERO(&rfds);
  FD_SET(STDIN_FILENO, &rfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0) {
    if (read(STDIN_FILENO, &ch, 1) == 1) {
      return (int)ch;
    }
  }
  return 0;
}

static inline void gtool_wait_keypress(void) {
  if (!isatty(STDIN_FILENO)) return;
  printf("\nPress any key to continue...");
  fflush(stdout);
  gtool_console_begin();
  while (gtool_poll_key() == 0) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 20000000L;
    nanosleep(&ts, NULL);
  }
  gtool_console_end();
  printf("\n");
}
#endif

#endif /* GLIDE_TOOL_PLATFORM_H */
