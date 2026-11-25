/*
 * Portable C99 platform helpers for glide-ng diagnostic & benchmark tools.
 * Supports Linux (POSIX), DOS (Open Watcom v2 + CauseWay), and Windows (Win32).
 */
#ifndef GLIDE_TOOL_PLATFORM_H
#define GLIDE_TOOL_PLATFORM_H

/* Pure C testable helpers for DOS CMOS RTC clock-drift detection.
 * Available on all platforms so unit tests can exercise BCD/12h/rollover
 * decoding directly; define GLIDE_TOOL_CMOS_HELPERS_ONLY to include only
 * these helpers in C++ unit tests without pulling POSIX/DOS globals.
 */
static inline unsigned int gtool_cmos_bcd_to_bin(unsigned int v) {
  return ((v >> 4) & 0x0Fu) * 10u + (v & 0x0Fu);
}

static inline int gtool_cmos_decode_time_sec(unsigned int sec_raw,
                                             unsigned int min_raw,
                                             unsigned int hour_raw,
                                             unsigned int status_b) {
  unsigned int is_binary = (status_b & 0x04u) != 0u;
  unsigned int is_24h = (status_b & 0x02u) != 0u;
  unsigned int is_pm = 0u;
  unsigned int sec;
  unsigned int min;
  unsigned int hour;

  if (!is_24h) {
    is_pm = (hour_raw & 0x80u) != 0u;
    hour_raw &= 0x7Fu;
  }

  sec = is_binary ? sec_raw : gtool_cmos_bcd_to_bin(sec_raw);
  min = is_binary ? min_raw : gtool_cmos_bcd_to_bin(min_raw);
  hour = is_binary ? hour_raw : gtool_cmos_bcd_to_bin(hour_raw);

  if (!is_24h) {
    if (hour == 0u || hour > 12u) {
      return -1;
    }
    hour %= 12u;
    if (is_pm) {
      hour += 12u;
    }
  }

  if (sec >= 60u || min >= 60u || hour >= 24u) {
    return -1;
  }
  return (int)(hour * 3600u + min * 60u + sec);
}

static inline int gtool_cmos_delta_sec(int start_sec, int end_sec) {
  int delta;
  if (start_sec < 0 || end_sec < 0) {
    return -1;
  }
  delta = end_sec - start_sec;
  if (delta < 0) {
    delta += 86400;
  }
  return delta;
}

/* Compares emulated PIT duration against 1-second-granularity CMOS wall time.
 * Returns +1 if virtual clock ran significantly slower (FPS inflated),
 * -1 if virtual clock ran significantly faster (FPS deflated), or 0 if normal.
 */
static inline int gtool_check_clock_drift(double virt_s,
                                          int wall_s,
                                          double* out_ratio) {
  double ratio;
  if (out_ratio) {
    *out_ratio = 1.0;
  }
  if (wall_s < 0 || virt_s <= 1e-6) {
    return 0;
  }
  if (virt_s < 2.0 && wall_s < 3) {
    return 0;
  }
  ratio = (double)wall_s / virt_s;
  if (out_ratio) {
    *out_ratio = ratio;
  }
  if ((double)wall_s > virt_s + 2.0 && ratio >= 1.35) {
    return 1;
  }
  if (virt_s >= 4.0 && (double)wall_s < virt_s - 2.0 && ratio <= 0.65) {
    return -1;
  }
  return 0;
}

#ifndef GLIDE_TOOL_CMOS_HELPERS_ONLY

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

static inline unsigned int gtool_dos_cmos_read(unsigned char reg) {
  outp(0x70, (unsigned char)(reg & 0x7Fu));
  return (unsigned int)inp(0x71);
}

/* Read host-backed CMOS RTC wall time in seconds since midnight (0..86399). */
static inline int gtool_dos_cmos_wall_sec(void) {
  int attempt;
  for (attempt = 0; attempt < 8; ++attempt) {
    unsigned int spin = 0u;
    unsigned int sec1, min1, hr1, sb1;
    unsigned int sec2, min2, hr2, sb2;
    while (spin < 1024u && (gtool_dos_cmos_read(0x0Au) & 0x80u) != 0u) {
      ++spin;
    }
    sec1 = gtool_dos_cmos_read(0x00u);
    min1 = gtool_dos_cmos_read(0x02u);
    hr1 = gtool_dos_cmos_read(0x04u);
    sb1 = gtool_dos_cmos_read(0x0Bu);

    sec2 = gtool_dos_cmos_read(0x00u);
    min2 = gtool_dos_cmos_read(0x02u);
    hr2 = gtool_dos_cmos_read(0x04u);
    sb2 = gtool_dos_cmos_read(0x0Bu);

    if (sec1 == sec2 && min1 == min2 && hr1 == hr2 && sb1 == sb2) {
      return gtool_cmos_decode_time_sec(sec1, min1, hr1, sb1);
    }
  }
  return -1;
}
#elif defined(_WIN32)
static inline int gtool_dos_cmos_wall_sec(void) {
  return -1;
}

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
static inline int gtool_dos_cmos_wall_sec(void) {
  return -1;
}

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

#endif /* GLIDE_TOOL_CMOS_HELPERS_ONLY */

#endif /* GLIDE_TOOL_PLATFORM_H */
