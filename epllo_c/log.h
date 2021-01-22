#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/timeb.h>

#define L_BLUE      "\033[1;35m"

#define log_time(color) \
    do\
    {\
        struct tm *t;\
        struct timeb stTimeb;\
        ftime(&stTimeb);\
        t = localtime(&stTimeb.time); \
        printf(color "[%4d%02d%02d-%02d:%02d:%02d:%03d]", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, stTimeb.millitm); \
        fflush(stdout);\
        printf("\033[0m"); \
    } while (0);

#define LOG_INFO(format, ...) \
    do\
    {\
        log_time(L_BLUE);\
        printf(L_BLUE "[INFO][%s][%s][%d]" format "\r\n", __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        printf("\033[0m");\
    } while (0);
    