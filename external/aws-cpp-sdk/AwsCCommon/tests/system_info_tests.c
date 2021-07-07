/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/common/system_info.h>

#include "logging/test_logger.h"
#include <aws/testing/aws_test_harness.h>

static int s_test_cpu_count_at_least_works_superficially_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    size_t processor_count = aws_system_info_processor_count();
    /* I think this is a fairly reasonable assumption given the circumstances
     * (you know this test is part of a program
     * that must be running on at least one core).... */
    ASSERT_TRUE(processor_count > 0);

    return 0;
}

AWS_TEST_CASE(test_cpu_count_at_least_works_superficially, s_test_cpu_count_at_least_works_superficially_fn)

#if defined(_WIN32)
#    include <io.h>
#    define DIRSEP "\\"
#else
#    define DIRSEP "/"
#endif

static int s_test_stack_trace_decoding(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_logger test_log;
    test_logger_init(&test_log, allocator, AWS_LL_TRACE, 0);
    aws_logger_set(&test_log);

    int line = 0;                           /* captured on line of aws_backtrace_log call to match call site */
    (void)line;                             /* may not be used if debug info is unavailable */
    aws_backtrace_log(), (line = __LINE__); /* NOLINT */

    struct test_logger_impl *log = test_log.p_impl;
    ASSERT_NOT_NULL(log);

    struct aws_byte_buf *buffer = &log->log_buffer;
    (void)buffer;

#if defined(AWS_BACKTRACE_STACKS_AVAILABLE) && defined(DEBUG_BUILD)
    /* ensure that this file/function is found */
    char *file = __FILE__;
    char *next = strstr(file, DIRSEP);
    /* strip path info, just filename will be found */
    while (next) {
        file = next + 1;
        next = strstr(file, DIRSEP);
    }

    struct aws_byte_cursor null_term = aws_byte_cursor_from_array("", 1);
    aws_byte_buf_append_dynamic(buffer, &null_term);
    ASSERT_NOT_NULL(strstr((const char *)buffer->buffer, __func__));
#    if !defined(__APPLE__) /* apple doesn't always find file info */
    /* if this is not a debug build, there may not be symbols, so the test cannot
     * verify if a best effort was made */
    ASSERT_NOT_NULL(strstr((const char *)buffer->buffer, file));
    /* check for the call site of aws_backtrace_print. Note that line numbers are off by one
     * in both directions depending on compiler, so we check a range around the call site __LINE__
     * The line number can also be ? on old compilers
     */
    char fileline[4096];
    uint32_t found_file_line = 0;
    for (int lineno = line - 1; lineno <= line + 1; ++lineno) {
        snprintf(fileline, sizeof(fileline), "%s:%d", file, lineno);
        found_file_line |= strstr((const char *)buffer->buffer, fileline) != NULL;
        if (found_file_line) {
            break;
        }
    }
    if (!found_file_line) {
        snprintf(fileline, sizeof(fileline), "%s:?", file);
        found_file_line = strstr((const char *)buffer->buffer, fileline) != NULL;
    }

    ASSERT_TRUE(found_file_line);
#    endif /* __APPLE__ */
#endif

    aws_logger_clean_up(&test_log);
    return 0;
}

AWS_TEST_CASE(test_stack_trace_decoding, s_test_stack_trace_decoding);
