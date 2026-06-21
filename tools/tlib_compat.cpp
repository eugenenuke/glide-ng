#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>

__attribute__((weak)) extern volatile int g_glideWrapperSdlKeyHit;
__attribute__((weak)) extern volatile int g_glideWrapperLastKey;

namespace {
    struct termios save_termdata;
    int init_done = 0;

    void reset_term(void) {
        if (init_done) {
            tcsetattr(STDIN_FILENO, TCSANOW, &save_termdata);
        }
    }

    void do_init(void) {
        struct termios termdata;
        if (tcgetattr(STDIN_FILENO, &termdata) < 0) {
            return;
        }
        save_termdata = termdata;
        
        struct termios raw = termdata;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
            return;
        }
        atexit(reset_term);
        init_done = 1;
    }
}

extern "C" int tlOkToRender(void) {
    return 1;
}

extern "C" int tlKbHit(void) {
    if (&g_glideWrapperSdlKeyHit && g_glideWrapperSdlKeyHit) {
        return 1;
    }

    if (!isatty(STDIN_FILENO)) {
        return 0;
    }

    if (!init_done) do_init();

    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

extern "C" char tlGetCH(void) {
    if (&g_glideWrapperSdlKeyHit && g_glideWrapperSdlKeyHit) {
        g_glideWrapperSdlKeyHit = 0;
        char key = ' ';
        if (&g_glideWrapperLastKey && g_glideWrapperLastKey) {
            key = (char)g_glideWrapperLastKey;
            g_glideWrapperLastKey = 0;
        }
        return key;
    }

    char ch = 0;
    if (!isatty(STDIN_FILENO)) {
        return 0;
    }

    if (!init_done) do_init();

    if (read(STDIN_FILENO, &ch, 1) < 0) {
        ch = 0;
    }
    return ch;
}
