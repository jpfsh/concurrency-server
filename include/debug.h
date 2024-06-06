#ifndef DEBUG_H
#define DEBUG_H
#include <stdio.h>
#include <stdlib.h>

#define KNRM "\x1B[0m"
#define KRED "\x1B[1;31m"
#define KGRN "\x1B[1;32m"
#define KYEL "\x1B[1;33m"
#define KBLU "\x1B[1;34m"
#define KMAG "\x1B[1;35m"
#define KCYN "\x1B[1;36m"
#define KWHT "\x1B[1;37m"
#define KBWN "\x1B[0;33m"

#ifdef DEBUG
#define fatal(S, ...)                                                       \
    fprintf(stderr, KRED "FATAL: %s:%s:%d " KNRM S, __FILE__, __FUNCTION__, \
            __LINE__, ##__VA_ARGS__); exit(EXIT_FAILURE);
#define debug(S, ...)                                                       \
    fprintf(stderr, KCYN "DEBUG: %s:%s:%d " KNRM S, __FILE__, __FUNCTION__, \
            __LINE__, ##__VA_ARGS__)
#define error(S, ...)                                                       \
    fprintf(stderr, KRED "ERROR: %s:%s:%d " KNRM S, __FILE__, __FUNCTION__, \
            __LINE__, ##__VA_ARGS__)
#define warning(S, ...)                                                    \
    fprintf(stderr, KYEL "WARN: %s:%s:%d " KNRM S, __FILE__, __FUNCTION__, \
            __LINE__, ##__VA_ARGS__)
#define info(S, ...)                                                       \
    fprintf(stderr, KBLU "INFO: %s:%s:%d " KNRM S, __FILE__, __FUNCTION__, \
            __LINE__, ##__VA_ARGS__)
#define success(S, ...)                                                       \
    fprintf(stderr, KGRN "SUCCESS: %s:%s:%d " KNRM S, __FILE__, __FUNCTION__, \
            __LINE__, ##__VA_ARGS__)
#define  fprintf(stderr, KMAG "ENTR: %s\n" KNRM, __FUNCTION__ );
#define  fprintf(stderr, KMAG "EXIT: %s\n" KNRM, __FUNCTION__ );
#else
#define debug(S, ...)
#define fatal(S, ...)                                                        \
    fprintf(stderr,  KRED "FATAL: " KNRM S, ##__VA_ARGS__); \
    exit(EXIT_FAILURE)
#define error(S, ...) fprintf(stderr,  KRED "ERROR: " KNRM S, ##__VA_ARGS__)
#define warning(S, ...) fprintf(stderr,  KYEL "WARNING: " KNRM S, ##__VA_ARGS__)
#define info(S, ...) fprintf(stderr,  KBLU "INFO: " KNRM S, ##__VA_ARGS__)
#define success(S, ...) fprintf(stderr,  KGRN "SUCCESS: " KNRM S, ##__VA_ARGS__)
#endif

#endif /* DEBUG_H */
