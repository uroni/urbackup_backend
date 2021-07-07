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

#include <aws/common/log_formatter.h>

#include <aws/common/string.h>
#include <aws/common/thread.h>
#include <aws/testing/aws_test_harness.h>

#include <inttypes.h>
#include <stdarg.h>

#define TEST_FORMATTER_MAX_BUFFER_SIZE 4096

typedef int(log_formatter_test_fn)(struct aws_log_formatter *formatter, struct aws_string **output);

int do_default_log_formatter_test(
    struct aws_allocator *allocator,
    log_formatter_test_fn *test_fn,
    const char *expected_user_output,
    enum aws_log_level log_level,
    enum aws_date_format date_format) {
    /* Initialize a default formatter*/
    struct aws_log_formatter_standard_options options = {.date_format = date_format};

    struct aws_log_formatter formatter;
    aws_log_formatter_init_default(&formatter, allocator, &options);

    struct aws_date_time test_time;
    aws_date_time_init_now(&test_time);

    /* Output something via the callback */
    struct aws_string *output = NULL;
    int result = (*test_fn)(&formatter, &output);

    aws_log_formatter_clean_up(&formatter);

    char buffer[TEST_FORMATTER_MAX_BUFFER_SIZE];
    snprintf(buffer, TEST_FORMATTER_MAX_BUFFER_SIZE, "%s", aws_string_c_str(output));

    aws_string_destroy(output);

    /* Check that the format call was successful */
    ASSERT_TRUE(result == AWS_OP_SUCCESS, "Formatting operation failed");

    /*
     * Make sure there's an endline as the final character(s).
     * Move the end-of-string marker on top of the endline.
     * Otherwise our failure outputs include the endline and become confusing.
     */
    size_t line_length = strlen(buffer);
    ASSERT_TRUE(line_length >= 2, "Log line \"%s\" is too short", buffer);

    ASSERT_TRUE(buffer[line_length - 1] == '\n', "Log line did not end with a newline");
    buffer[line_length - 1] = 0;

    /*
     * Check that the log level appears properly
     */
    const char *log_level_start = strstr(buffer, "[");
    ASSERT_TRUE(log_level_start != NULL, "Could not find start of log level in output line \"%s\"", buffer);

    const char *level_string = NULL;
    ASSERT_SUCCESS(
        aws_log_level_to_string(log_level, &level_string), "Failed to convert log level %d to string", (int)log_level);
    ASSERT_TRUE(
        strncmp(log_level_start + 1, level_string, strlen(level_string)) == 0,
        "Incorrect value for log level in output line \"%s\"",
        buffer);

    /**
     * Find the timestamp substring.
     */
    const char *time_start = strstr(log_level_start + 1, "[");
    ASSERT_TRUE(time_start != NULL, "Could not find start of timestamp in output line \"%s\"", buffer);
    time_start += 1;

    const char *time_end = strstr(time_start, "]");
    ASSERT_TRUE(time_end != NULL, "Could not find end of timestamp in output line \"%s\"", buffer);

    size_t time_length = time_end - time_start;

    /*
     * Fake a buffer pointing to the logged timestamp string; convert it to a date time
     */
    struct aws_byte_buf timestamp_buffer;
    timestamp_buffer.allocator = formatter.allocator;
    timestamp_buffer.buffer = (uint8_t *)time_start;
    timestamp_buffer.capacity = time_length;
    timestamp_buffer.len = time_length;

    struct aws_date_time log_time;
    ASSERT_SUCCESS(
        aws_date_time_init_from_str(&log_time, &timestamp_buffer, date_format),
        "Could not parse timestamp value starting at \"%s\"",
        time_start);

    /*
     * Check that the timestamp, when converted back, is close to the current time.
     */
    time_t time_diff = aws_date_time_diff(&log_time, &test_time);
    ASSERT_TRUE(time_diff <= 1, "Log timestamp deviated too far from test timestamp: %d seconds", (int)time_diff);

    /*
     * Find the thread id substring
     */
    const char *thread_id_start = strstr(time_end + 1, "[");
    ASSERT_TRUE(thread_id_start != NULL, "Could not find start of thread id in output line \"%s\"", buffer);
    thread_id_start += 1;

    char *thread_id_end = strstr(thread_id_start, "]");
    ASSERT_TRUE(thread_id_end != NULL, "Could not find end of thread id in output line \"%s\"", buffer);

    ASSERT_TRUE((thread_id_end - thread_id_start + 1) == AWS_THREAD_ID_T_REPR_BUFSZ, "Unexpected thread id length");
    aws_thread_id_t current_thread_id = aws_thread_current_thread_id();
    char repr[AWS_THREAD_ID_T_REPR_BUFSZ];
    ASSERT_SUCCESS(
        aws_thread_id_t_to_string(current_thread_id, repr, AWS_THREAD_ID_T_REPR_BUFSZ),
        "Could not convert aws_thread_id_t to string repr");
    char logged_id[AWS_THREAD_ID_T_REPR_BUFSZ];
    memcpy(logged_id, thread_id_start, AWS_THREAD_ID_T_REPR_BUFSZ - 1);
    logged_id[AWS_THREAD_ID_T_REPR_BUFSZ - 1] = '\0';
    ASSERT_SUCCESS(
        strncmp(repr, logged_id, AWS_THREAD_ID_T_REPR_BUFSZ),
        "Expected logged thread id to be \"%s\" but it was actually \"%s\"",
        repr,
        logged_id);

    /*
     * Check that the user content is what was expected
     */
    const char *separator = strstr(thread_id_end, " - ");
    ASSERT_TRUE(separator != NULL, "");

    const char *user_content = separator + 3;
    size_t expected_user_content_length = strlen(expected_user_output);
    ASSERT_SUCCESS(
        strncmp(user_content, expected_user_output, expected_user_content_length),
        "Expected user content \"%s\" but received \"%s\"",
        expected_user_output,
        user_content);

    return AWS_OP_SUCCESS;
}

#define DEFINE_LOG_FORMATTER_TEST(test_function, log_level, date_format, expected_user_string)                         \
    static int s_log_formatter_##test_function(struct aws_allocator *allocator, void *ctx) {                           \
        (void)ctx;                                                                                                     \
        return do_default_log_formatter_test(allocator, test_function, expected_user_string, log_level, date_format);  \
    }                                                                                                                  \
    AWS_TEST_CASE(test_log_formatter_##test_function, s_log_formatter_##test_function);

static int invoke_formatter(
    struct aws_log_formatter *formatter,
    struct aws_string **output,
    enum aws_log_level log_level,
    const char *format,
    ...) {
    va_list args;
    va_start(args, format);

    int result = formatter->vtable->format(formatter, output, log_level, AWS_LS_COMMON_GENERAL, format, args);

    va_end(args);

    return result;
}

/*
 * Tests
 */

/*
 * Empty string case
 */
static int s_formatter_empty_case(struct aws_log_formatter *formatter, struct aws_string **output) {
    return invoke_formatter(formatter, output, AWS_LL_WARN, "");
}

DEFINE_LOG_FORMATTER_TEST(s_formatter_empty_case, AWS_LL_WARN, AWS_DATE_FORMAT_RFC822, "")

/*
 * Simple string
 */
static int s_formatter_simple_case(struct aws_log_formatter *formatter, struct aws_string **output) {
    return invoke_formatter(formatter, output, AWS_LL_DEBUG, "Sample log output");
}

DEFINE_LOG_FORMATTER_TEST(s_formatter_simple_case, AWS_LL_DEBUG, AWS_DATE_FORMAT_ISO_8601, "Sample log output")

/*
 * Format string with numbers
 */
static int s_formatter_number_case(struct aws_log_formatter *formatter, struct aws_string **output) {
    return invoke_formatter(
        formatter, output, AWS_LL_FATAL, "%d bottles of milk on the wall. Take %.4f bottles down.", 99, .9999f);
}

DEFINE_LOG_FORMATTER_TEST(
    s_formatter_number_case,
    AWS_LL_FATAL,
    AWS_DATE_FORMAT_RFC822,
    "99 bottles of milk on the wall. Take 0.9999 bottles down.")

/*
 * Format string with strings
 */
static int s_formatter_string_case(struct aws_log_formatter *formatter, struct aws_string **output) {
    return invoke_formatter(
        formatter,
        output,
        AWS_LL_INFO,
        "Once there was, if %s there was, and just perhaps there %s was",
        "ever",
        "never");
}

DEFINE_LOG_FORMATTER_TEST(
    s_formatter_string_case,
    AWS_LL_INFO,
    AWS_DATE_FORMAT_ISO_8601,
    "Once there was, if ever there was, and just perhaps there never was")

/*
 * Format string with newlines
 */
static int s_formatter_newline_case(struct aws_log_formatter *formatter, struct aws_string **output) {
    return invoke_formatter(formatter, output, AWS_LL_TRACE, "\nMaking sure \nnewlines don't mess things\nup");
}

DEFINE_LOG_FORMATTER_TEST(
    s_formatter_newline_case,
    AWS_LL_TRACE,
    AWS_DATE_FORMAT_RFC822,
    "\nMaking sure \nnewlines don't mess things\nup")
