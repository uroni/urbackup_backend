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

#include "logging_test_utilities.h"

#include "test_logger.h"

#include <aws/common/string.h>
#include <aws/common/uuid.h>

#define TEST_LOGGER_MAX_BUFFER_SIZE 4096

int do_log_test(
    struct aws_allocator *allocator,
    enum aws_log_level level,
    const char *expected_result,
    void (*callback)(enum aws_log_level)) {

    /* Create and attach a logger for testing*/
    struct aws_logger test_logger;
    test_logger_init(&test_logger, allocator, level, TEST_LOGGER_MAX_BUFFER_SIZE);
    aws_logger_set(&test_logger);

    /* Perform logging operations */
    (*callback)(level);

    /* Pull out what was logged before clean up */
    char buffer[TEST_LOGGER_MAX_BUFFER_SIZE];
    test_logger_get_contents(&test_logger, buffer, TEST_LOGGER_MAX_BUFFER_SIZE);

    /* clean up */
    aws_logger_set(NULL);
    aws_logger_clean_up(&test_logger);

    /* Check the test results last */
    ASSERT_SUCCESS(strcmp(buffer, expected_result), "Expected \"%s\" but received \"%s\"", expected_result, buffer);

    return AWS_OP_SUCCESS;
}

struct aws_string *aws_string_new_log_writer_test_filename(struct aws_allocator *allocator) {
    char filename_array[64];
    struct aws_byte_buf filename_buf = aws_byte_buf_from_empty_array(filename_array, sizeof(filename_array));

#ifndef _WIN32
    AWS_FATAL_ASSERT(aws_byte_buf_write_from_whole_cursor(&filename_buf, aws_byte_cursor_from_c_str("./")));
#endif

    AWS_FATAL_ASSERT(
        aws_byte_buf_write_from_whole_cursor(&filename_buf, aws_byte_cursor_from_c_str("aws_log_writer_test_")));

    struct aws_uuid uuid;
    AWS_FATAL_ASSERT(aws_uuid_init(&uuid) == AWS_OP_SUCCESS);
    AWS_FATAL_ASSERT(aws_uuid_to_str(&uuid, &filename_buf) == AWS_OP_SUCCESS);

    AWS_FATAL_ASSERT(aws_byte_buf_write_from_whole_cursor(&filename_buf, aws_byte_cursor_from_c_str(".log")));

    return aws_string_new_from_array(allocator, filename_buf.buffer, filename_buf.len);
}
