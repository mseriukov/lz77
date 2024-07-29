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
#include <sys/stat.h>

#define null ((void*)NULL)

#define rt_countof(a) (sizeof(a) / sizeof((a)[0]))

static void rt_output_line(const char* text) {
    // "full path filename(line):" is useful for MSVC single click
    // navigation to the source of println() call:
    #ifdef _WINDOWS_
        OutputDebugStringA(text);
    #endif
    // remove full path from filename on stderr:
    const char* c = strstr(text, "):");
    if (c != null) {
        while (c > text && *c != '\\') { c--; }
        if (c != text) { text = c + 1; }
    }
    fprintf(stderr, "%s", text);
}

static int32_t rt_print_line(const char* filename, int32_t line,
        const char* function, const char* format, ...) {
    char text[1024];
    va_list va;
    va_start(va, format);
    int32_t n = vsnprintf(text, sizeof(text) - 2, format, va);
    va_end(va);
    if (n < 0) {
        rt_output_line("printf format error\n");
    } else { // n >= 0
        text[n + 0] = '\n';
        text[n + 1] = 0x00;
        char prefix[1024];
        snprintf(prefix, sizeof(prefix) - 1, "%s(%d):", filename, line);
        prefix[sizeof(prefix) - 1] = 0x00;
        static size_t pw; // max prefix width for output
        pw = pw < strlen(prefix) ? strlen(prefix) : pw;
        char output[2048];
        static size_t fw; // max function width for output
        fw = fw < strlen(function) ? strlen(function) : fw;
        snprintf(output, sizeof(output) - 1, "%-*s %-*s %s",
            (unsigned int)pw, prefix, (unsigned int)fw, function, text);
        output[sizeof(output) - 1] = 0x00;
        rt_output_line(output);
    }
    return 0;
}

#define rt_println(...) rt_print_line(__FILE__, __LINE__, __func__, \
                                      "" __VA_ARGS__)

#define rt_swear(b, ...) ((void)                            \
    ((b) ? 0 : rt_print_line(__FILE__, __LINE__, __func__,  \
                             #b " false " __VA_ARGS__) +    \
               rt_exit(1)))

#if defined(DEBUG) || defined(_DEBUG)
#define rt_assert(b, ...) rt_swear(b, __VA_ARGS__)
#else
#define rt_assert(b, ...) ((void)(0))
#endif

static int32_t rt_exit(int exit_code) {
    // assert or swear here will recurse
    if (exit_code == 0) { rt_println("exit code must not be zero"); }
    if (exit_code != 0) {
        #ifdef _WINDOWS_
            DebugBreak();
            ExitProcess(exit_code);
        #else
            exit(exit_code);
        #endif
    }
    return 0;
}

#endif // rt_h
