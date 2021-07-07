#ifndef TEST_MACROS_H
#define TEST_MACROS_H
/*
 * Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Some macros used in test cases...
/** Prints a message to stdout using printf format that appends the function, file and line number. */
static void cunitMessage(const char *function, const char *file, int line, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    char buffer1[4096];
    vsnprintf(buffer1, sizeof(buffer1), format, ap);
    buffer1[sizeof(buffer1) - 1] = 0;
    va_end(ap);

    char buffer2[4096];
    snprintf(buffer2, sizeof(buffer2), " [%s():%s@#%d]", function, file, line);
    buffer2[sizeof(buffer2) - 1] = 0;

    printf("%s%s\n", buffer1, buffer2);
}

static int total_failures;

#define SUCCESS 0
#define FAILURE (-1)

#define RETURN_SUCCESS(format, ...)                                                                                    \
    do {                                                                                                               \
        cunitMessage(__FUNCTION__, __FILE__, __LINE__, format, ##__VA_ARGS__);                                         \
        return SUCCESS;                                                                                                \
    } while (0)
#define FAIL(format, ...)                                                                                              \
    do {                                                                                                               \
        cunitMessage(__FUNCTION__, __FILE__, __LINE__, "***FAILURE*** " format, ##__VA_ARGS__);                        \
        total_failures++;                                                                                              \
        goto failure;                                                                                                  \
    } while (0)
#define ASSERT_TRUE(condition, format, ...)                                                                            \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            FAIL(format, ##__VA_ARGS__);                                                                               \
        }                                                                                                              \
    } while (0)
#define ASSERT_INT_EQUALS(expected, got, message, ...)                                                                 \
    do {                                                                                                               \
        long long a = (long long)(expected);                                                                           \
        long long b = (long long)(got);                                                                                \
        if (a != b) {                                                                                                  \
            FAIL("Expected:%lld got:%lld - " message, a, b, ##__VA_ARGS__);                                            \
        }                                                                                                              \
    } while (0)
#define ASSERT_BYTE_HEX_EQUALS(expected, got, message, ...)                                                            \
    do {                                                                                                               \
        uint8_t a = (uint8_t)(expected);                                                                               \
        uint8_t b = (uint8_t)(got);                                                                                    \
        if (a != b) {                                                                                                  \
            FAIL("Expected:%x got:%x - " message, a, b, ##__VA_ARGS__);                                                \
        }                                                                                                              \
    } while (0)
#define ASSERT_HEX_EQUALS(expected, got, message, ...)                                                                 \
    do {                                                                                                               \
        long long a = (long long)(expected);                                                                           \
        long long b = (long long)(got);                                                                                \
        if (a != b) {                                                                                                  \
            FAIL("Expected:%llX got:%llX - " message, a, b, ##__VA_ARGS__);                                            \
        }                                                                                                              \
    } while (0)
#define ASSERT_BIN_ARRAYS_EQUALS(expected, expected_size, got, got_size, message, ...)                                 \
    for (size_t i = 0; i < expected_size; ++i) {                                                                       \
        if (expected[i] != got[i]) {                                                                                   \
            FAIL("Expected:%02x got:%02x - " message, expected[i], got[i], ##__VA_ARGS__);                             \
        }                                                                                                              \
    }
#endif /* TEST_MACROS_H */
