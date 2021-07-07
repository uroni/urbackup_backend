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

#include <aws/common/log_channel.h>

#include <aws/common/string.h>
#include <aws/common/thread.h>

#include <aws/common/log_writer.h>

#include <aws/testing/aws_test_harness.h>

#include <inttypes.h>

#ifdef _MSC_VER
#    pragma warning(disable : 4996) /* Disable warnings about fopen() being insecure */
#endif                              /* _MSC_VER */

/*
 * Mock writer to capture what gets passed through
 */
struct mock_log_writer_impl {
    struct aws_array_list log_lines;
};

static int s_mock_log_writer_write(struct aws_log_writer *writer, const struct aws_string *output) {
    struct mock_log_writer_impl *impl = (struct mock_log_writer_impl *)writer->impl;

    struct aws_string *output_copy = aws_string_new_from_string(writer->allocator, output);
    if (output_copy == NULL) {
        return AWS_OP_ERR;
    }

    aws_array_list_push_back(&impl->log_lines, &output_copy);

    return AWS_OP_SUCCESS;
}

static void s_mock_log_writer_clean_up(struct aws_log_writer *writer) {
    struct mock_log_writer_impl *impl = (struct mock_log_writer_impl *)writer->impl;

    size_t line_count = aws_array_list_length(&impl->log_lines);
    for (size_t i = 0; i < line_count; ++i) {
        struct aws_string *line = NULL;
        if (aws_array_list_get_at(&impl->log_lines, &line, i)) {
            continue;
        }

        aws_string_destroy(line);
    }

    aws_array_list_clean_up(&impl->log_lines);

    aws_mem_release(writer->allocator, impl);
}

static struct aws_log_writer_vtable s_mock_writer_vtable = {.write = s_mock_log_writer_write,
                                                            .clean_up = s_mock_log_writer_clean_up};

static int s_aws_mock_log_writer_init(struct aws_log_writer *writer, struct aws_allocator *allocator) {
    struct mock_log_writer_impl *impl =
        (struct mock_log_writer_impl *)aws_mem_acquire(allocator, sizeof(struct mock_log_writer_impl));
    if (impl == NULL) {
        return AWS_OP_ERR;
    }

    if (aws_array_list_init_dynamic(&impl->log_lines, allocator, 10, sizeof(struct aws_string *))) {
        aws_mem_release(allocator, impl);
        return AWS_OP_ERR;
    }

    writer->vtable = &s_mock_writer_vtable;
    writer->allocator = allocator;
    writer->impl = impl;

    return AWS_OP_SUCCESS;
}

/*
 * Test utilities
 */
static char s_test_error_message[4096];

static bool s_verify_mock_equal(
    struct aws_log_writer *writer,
    const struct aws_string ***test_lines,
    size_t array_length) {
    struct mock_log_writer_impl *impl = (struct mock_log_writer_impl *)writer->impl;

    size_t line_count = aws_array_list_length(&impl->log_lines);
    if (line_count != array_length) {
        sprintf(
            s_test_error_message,
            "Expected %" PRIu64 " lines, but received %" PRIu64 "",
            (uint64_t)array_length,
            (uint64_t)line_count);
        return false;
    }

    for (size_t i = 0; i < array_length; ++i) {
        struct aws_string *captured = NULL;
        if (aws_array_list_get_at(&impl->log_lines, &captured, i)) {
            sprintf(s_test_error_message, "Unable to fetch log line from array list");
            return false;
        }

        const struct aws_string *original = *(test_lines[i]);
        if (!aws_string_eq(original, captured)) {
            sprintf(
                s_test_error_message,
                "Expected log line:\n%s\nbut received log line:\n%s",
                (char *)original->bytes,
                (char *)captured->bytes);
            return false;
        }
    }

    return true;
}

typedef int (
    *init_channel_fn)(struct aws_log_channel *channel, struct aws_allocator *allocator, struct aws_log_writer *writer);

static int s_do_channel_test(
    struct aws_allocator *allocator,
    init_channel_fn init_fn,
    const struct aws_string ***test_lines,
    size_t test_lines_length,
    int *sleep_times) {

    struct aws_log_writer mock_writer;
    if (s_aws_mock_log_writer_init(&mock_writer, allocator)) {
        return AWS_OP_ERR;
    }

    struct aws_log_channel log_channel;
    if (init_fn(&log_channel, allocator, &mock_writer)) {
        return AWS_OP_ERR;
    }

    int result = AWS_OP_SUCCESS;

    for (size_t i = 0; i < test_lines_length; ++i) {
        struct aws_string *test_line_copy = aws_string_new_from_string(log_channel.allocator, *(test_lines[i]));

        if ((log_channel.vtable->send)(&log_channel, test_line_copy)) {
            sprintf(s_test_error_message, "Failed call to log channel send");
            result = AWS_OP_ERR;
        }

        /*
         * For background case, optionally sleep to vary the push timing
         */
        if (sleep_times && sleep_times[i] > 0) {
            aws_thread_current_sleep(sleep_times[i]);
        }
    }

    aws_log_channel_clean_up(&log_channel);

    if (!s_verify_mock_equal(log_channel.writer, test_lines, test_lines_length)) {
        result = AWS_OP_ERR;
    }

    aws_log_writer_clean_up(&mock_writer);

    return result;
}

/*
 * Test body helper macros
 */
#define DEFINE_FOREGROUND_LOG_CHANNEL_TEST(test_name, string_array_name)                                               \
    static int s_foreground_log_channel_##test_name(struct aws_allocator *allocator, void *ctx) {                      \
        (void)ctx;                                                                                                     \
        return s_do_channel_test(                                                                                      \
            allocator,                                                                                                 \
            aws_log_channel_init_foreground,                                                                           \
            string_array_name,                                                                                         \
            sizeof(string_array_name) / sizeof(struct aws_string **),                                                  \
            NULL);                                                                                                     \
    }                                                                                                                  \
    AWS_TEST_CASE(test_foreground_log_channel_##test_name, s_foreground_log_channel_##test_name);

#define DEFINE_BACKGROUND_LOG_CHANNEL_TEST(test_name, string_array_name, sleep_times)                                  \
    static int s_background_log_channel_##test_name(struct aws_allocator *allocator, void *ctx) {                      \
        (void)ctx;                                                                                                     \
        return s_do_channel_test(                                                                                      \
            allocator,                                                                                                 \
            aws_log_channel_init_background,                                                                           \
            string_array_name,                                                                                         \
            sizeof(string_array_name) / sizeof(struct aws_string **),                                                  \
            sleep_times);                                                                                              \
    }                                                                                                                  \
    AWS_TEST_CASE(test_background_log_channel_##test_name, s_background_log_channel_##test_name);

/*
 * Test data
 */
AWS_STATIC_STRING_FROM_LITERAL(s_log_line_1, "1");
AWS_STATIC_STRING_FROM_LITERAL(s_log_line_2, "2");
AWS_STATIC_STRING_FROM_LITERAL(s_log_line_3, "3");
AWS_STATIC_STRING_FROM_LITERAL(s_log_line_4, "4");

AWS_STATIC_STRING_FROM_LITERAL(s_log_line_simple, "A simple line.\n");
AWS_STATIC_STRING_FROM_LITERAL(s_log_line_multiline, "There's\na lot\n\tof snow outside.\n");
AWS_STATIC_STRING_FROM_LITERAL(s_log_line_fake, "[DEBUG] [??] [1234567] - Time to crash\n");

const struct aws_string **s_channel_test_one_line[] = {&s_log_line_1};

const struct aws_string **s_channel_test_numbers[] = {&s_log_line_1, &s_log_line_2, &s_log_line_3, &s_log_line_4};

const struct aws_string **s_channel_test_words[] = {&s_log_line_simple, &s_log_line_multiline, &s_log_line_fake};

const struct aws_string **s_channel_test_all[] = {
    &s_log_line_1,
    &s_log_line_2,
    &s_log_line_3,
    &s_log_line_4,
    &s_log_line_simple,
    &s_log_line_multiline,
    &s_log_line_fake,
};

static int s_background_sleep_times_ns[] = {0, 100000, 0, 0, 1000000, 0, 1000000};

/*
 * Foreground channel tests
 */
DEFINE_FOREGROUND_LOG_CHANNEL_TEST(single_line, s_channel_test_one_line)

DEFINE_FOREGROUND_LOG_CHANNEL_TEST(numbers, s_channel_test_numbers)

DEFINE_FOREGROUND_LOG_CHANNEL_TEST(words, s_channel_test_words)

DEFINE_FOREGROUND_LOG_CHANNEL_TEST(all, s_channel_test_all)

/*
 * Background channel tests
 */
DEFINE_BACKGROUND_LOG_CHANNEL_TEST(single_line, s_channel_test_one_line, NULL)

DEFINE_BACKGROUND_LOG_CHANNEL_TEST(numbers, s_channel_test_numbers, s_background_sleep_times_ns)

DEFINE_BACKGROUND_LOG_CHANNEL_TEST(words, s_channel_test_words, s_background_sleep_times_ns)

DEFINE_BACKGROUND_LOG_CHANNEL_TEST(all, s_channel_test_all, s_background_sleep_times_ns)
