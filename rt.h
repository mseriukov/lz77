#pragma once
#ifndef rt_h
#define rt_h

// nano runtime to make debugging, life, universe and everything a bit easier

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define null ((void*)NULL)

#define rt_countof(a) (sizeof(a) / sizeof((a)[0]))

static void rt_output_line(const char* text) {
    #ifdef _WINDOWS_
        OutputDebugStringA(text);
    #endif
    fprintf(stderr, "%s", text);
}

static int32_t rt_print_line(const char* fn, int32_t ln, const char* fmt, ...) {
    char text[1024];
    va_list va;
    va_start(va, fmt);
    int32_t n = vsnprintf(text, sizeof(text) / sizeof(text[0]) - 2, fmt, va);
    va_end(va);
    if (n < 0) {
        rt_output_line("printf format error\n");
    } else { // n >= 0
        text[n + 0] = '\n';
        text[n + 1] = 0x00;
        size_t k = strlen(fn);
        char output[2048];
        // INT32_MAX = 2,147,483,647 - 10 decimal digits long
        // "(2147483647): " - 14 characters wide
        if (k < sizeof(output) / sizeof(output[0]) - k - 16) {
            snprintf(output, sizeof(output) / sizeof(output[0]),
                     "%s(%d): %s", fn, ln, text);
            rt_output_line(output);
        } else {
            rt_output_line("buffer overflow");
        }
    }
    return 0;
}

static int32_t rt_abort(int exit_code) {
    #ifdef _WINDOWS_
        DebugBreak();
        ExitProcess(exit_code);
    #else
        exit(exit_code);
    #endif
    return 0;
}

#define rt_println(...) rt_print_line(__FILE__, __LINE__, "" __VA_ARGS__)

#define rt_swear(b, ...) ((void)                         \
    ((b) ? 0 : rt_print_line(__FILE__, __LINE__,         \
                             #b " false " __VA_ARGS__) + \
               rt_abort(1)))

#if defined(DEBUG) || defined(_DEBUG)
#define rt_assert(b, ...) rt_swear(b, __VA_ARGS__)
#else
#define rt_assert(b, ...) ((void)(0))
#endif

#endif // rt_h
