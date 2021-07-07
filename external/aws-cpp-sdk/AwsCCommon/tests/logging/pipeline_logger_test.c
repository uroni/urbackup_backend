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

#include "logging_test_utilities.h"

#include <aws/common/logging.h>
#include <aws/common/string.h>

#include <aws/testing/aws_test_harness.h>

#include <errno.h>
#include <stdio.h>

#ifdef _MSC_VER
#    pragma warning(disable : 4996) /* Disable warnings about fopen() being insecure */
#endif                              /* _MSC_VER */

#define TEST_PIPELINE_MAX_BUFFER_SIZE 4096

typedef void(log_test_fn)(void);

int do_pipeline_logger_test(
    struct aws_allocator *allocator,
    log_test_fn *log_fn,
    const char **expected_user_content,
    size_t user_content_count) {

    struct aws_string *test_file_str = aws_string_new_log_writer_test_filename(allocator);
    const char *test_file_cstr = aws_string_c_str(test_file_str);

    remove(test_file_cstr);

    struct aws_logger_standard_options options = {.level = AWS_LL_TRACE, .filename = test_file_cstr};

    struct aws_logger logger;
    if (aws_logger_init_standard(&logger, allocator, &options)) {
        return AWS_OP_ERR;
    }

    aws_logger_set(&logger);

    (*log_fn)();

    aws_logger_set(NULL);

    aws_logger_clean_up(&logger);

    char buffer[TEST_PIPELINE_MAX_BUFFER_SIZE];
    FILE *file = fopen(test_file_cstr, "r");
    int open_error = errno;
    size_t bytes_read = 0;

    if (file != NULL) {
        bytes_read = fread(buffer, 1, TEST_PIPELINE_MAX_BUFFER_SIZE - 1, file);
        fclose(file);
    }

    remove(test_file_cstr);

    /*
     * Check the file was read successfully
     */
    ASSERT_TRUE(
        file != NULL, "Unable to open log file \"%s\" to verify contents. Error: %d", test_file_str, open_error);
    ASSERT_TRUE(bytes_read >= 0, "Failed to read log file \"%s\"", test_file_str);

    /*
     * add end of string marker
     */
    buffer[bytes_read] = 0;

    /*
     * Timestamps prevent us from doing simple string comparisons to check the log file and writing a parser to pull out
     * log lines in the face of multi-line arbitrary content seems overkill.  Since we've already validated
     * the formatter via the formatter tests, the main thing to do here is just verify that the user part of the log
     * lines is making it to the log file.
     */
    const char *buffer_ptr = buffer;
    for (size_t i = 0; i < user_content_count; ++i) {
        buffer_ptr = strstr(buffer_ptr, expected_user_content[i]);
        ASSERT_TRUE(
            buffer_ptr != NULL,
            "Expected to find \"%s\" in log file but could not.  Content is either missing or out-of-order.",
            expected_user_content[i]);
    }

    aws_string_destroy(test_file_str);

    return AWS_OP_SUCCESS;
}

static void s_unformatted_pipeline_logger_test_callback(void) {
    AWS_LOGF_TRACE(AWS_LS_COMMON_GENERAL, "trace log call");
    AWS_LOGF_DEBUG(AWS_LS_COMMON_GENERAL, "debug log call");
    AWS_LOGF_INFO(AWS_LS_COMMON_GENERAL, "info log call");
    AWS_LOGF_WARN(AWS_LS_COMMON_GENERAL, "warn log call");
    AWS_LOGF_ERROR(AWS_LS_COMMON_GENERAL, "error log call");
    AWS_LOGF_FATAL(AWS_LS_COMMON_GENERAL, "fatal log call");
}

static void s_formatted_pipeline_logger_test_callback(void) {
    AWS_LOGF_TRACE(AWS_LS_COMMON_GENERAL, "%s log call", "trace");
    AWS_LOGF_DEBUG(AWS_LS_COMMON_GENERAL, "%s log call", "debug");
    AWS_LOGF_INFO(AWS_LS_COMMON_GENERAL, "%s log call", "info");
    AWS_LOGF_WARN(AWS_LS_COMMON_GENERAL, "%s log call", "warn");
    AWS_LOGF_ERROR(AWS_LS_COMMON_GENERAL, "%s log call", "error");
    AWS_LOGF_FATAL(AWS_LS_COMMON_GENERAL, "%s log call", "fatal");
}

static const char *expected_test_user_content[] =
    {"trace log call", "debug log call", "info log call", "warn log call", "error log call", "fatal log call"};

#define DEFINE_PIPELINE_LOGGER_TEST(test_name, callback_function)                                                      \
    static int s_pipeline_logger_##test_name(struct aws_allocator *allocator, void *ctx) {                             \
        (void)ctx;                                                                                                     \
        return do_pipeline_logger_test(                                                                                \
            allocator,                                                                                                 \
            callback_function,                                                                                         \
            expected_test_user_content,                                                                                \
            sizeof(expected_test_user_content) / sizeof(expected_test_user_content[0]));                               \
    }                                                                                                                  \
    AWS_TEST_CASE(test_pipeline_logger_##test_name, s_pipeline_logger_##test_name);

DEFINE_PIPELINE_LOGGER_TEST(unformatted_test, s_unformatted_pipeline_logger_test_callback)
DEFINE_PIPELINE_LOGGER_TEST(formatted_test, s_formatted_pipeline_logger_test_callback)
