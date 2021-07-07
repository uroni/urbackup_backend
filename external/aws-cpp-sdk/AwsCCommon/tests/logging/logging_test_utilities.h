/* NOLINTNEXTLINE(llvm-header-guard) */
#ifndef AWS_COMMON_LOGGING_TEST_UTILITIES_H
#define AWS_COMMON_LOGGING_TEST_UTILITIES_H

/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/common/logging.h>

#include <aws/testing/aws_test_harness.h>

/**
 * A staging function for basic logging tests.  It
 *   (1) Initializes and globally attaches a test logger
 *   (2) Invokes the supplied callback, which should perform the logging operations under test
 *   (3) Detaches and cleans up the test logger
 *   (4) Checks if what was recorded by the test logger matches what the test expected.
 */
int do_log_test(
    struct aws_allocator *allocator,
    enum aws_log_level level,
    const char *expected_result,
    void (*callback)(enum aws_log_level));

/**
 * A macro capable of defining simple logging tests that follow the do_log_test function pattern
 */
#define TEST_LEVEL_FILTER(log_level, expected, action_fn)                                                              \
    static int s_logging_filter_at_##log_level##_##action_fn(struct aws_allocator *allocator, void *ctx) {             \
        (void)ctx;                                                                                                     \
        return do_log_test(allocator, log_level, expected, action_fn);                                                 \
    }                                                                                                                  \
    AWS_TEST_CASE(test_logging_filter_at_##log_level##_##action_fn, s_logging_filter_at_##log_level##_##action_fn);

/**
 * A macro that defines a function that invokes all 6 LOGF_<level> variants
 *
 * Needs to be a macro and not just a function because the compile-time filtering tests require a private implementation
 * that is compiled with AWS_STATIC_LOG_LEVEL at the level to be tested.  There's no way to shared a single definition
 * that does so.
 */
#define DECLARE_LOGF_ALL_LEVELS_FUNCTION(fn_name)                                                                      \
    static void fn_name(enum aws_log_level level) {                                                                    \
        (void)level;                                                                                                   \
        AWS_LOGF_FATAL(AWS_LS_COMMON_GENERAL, "%d", (int)AWS_LL_FATAL);                                                \
        AWS_LOGF_ERROR(AWS_LS_COMMON_GENERAL, "%d", (int)AWS_LL_ERROR);                                                \
        AWS_LOGF_WARN(AWS_LS_COMMON_GENERAL, "%d", (int)AWS_LL_WARN);                                                  \
        AWS_LOGF_INFO(AWS_LS_COMMON_GENERAL, "%d", (int)AWS_LL_INFO);                                                  \
        AWS_LOGF_DEBUG(AWS_LS_COMMON_GENERAL, "%d", (int)AWS_LL_DEBUG);                                                \
        AWS_LOGF_TRACE(AWS_LS_COMMON_GENERAL, "%d", (int)AWS_LL_TRACE);                                                \
    }

/**
 * Return new string with format "./aws_log_writer_test_{UUID}.log"
 * This function cannot fail.
 */
struct aws_string *aws_string_new_log_writer_test_filename(struct aws_allocator *allocator);

#endif /* AWS_COMMON_LOGGING_TEST_UTILITIES_H */
