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
#include <aws/common/uuid.h>

#include <aws/common/byte_buf.h>

#include <aws/testing/aws_test_harness.h>

static int s_uuid_string_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_uuid uuid;
    ASSERT_SUCCESS(aws_uuid_init(&uuid));

    uint8_t uuid_array[AWS_UUID_STR_LEN] = {0};
    struct aws_byte_buf uuid_buf = aws_byte_buf_from_array(uuid_array, sizeof(uuid_array));
    uuid_buf.len = 0;

    ASSERT_SUCCESS(aws_uuid_to_str(&uuid, &uuid_buf));
    uint8_t zerod_buf[AWS_UUID_STR_LEN] = {0};
    ASSERT_UINT_EQUALS(AWS_UUID_STR_LEN - 1, uuid_buf.len);
    ASSERT_FALSE(0 == memcmp(zerod_buf, uuid_array, sizeof(uuid_array)));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(uuid_string, s_uuid_string_fn)

static int s_prefilled_uuid_string_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_uuid uuid = {
        .uuid_data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},
    };

    uint8_t uuid_array[AWS_UUID_STR_LEN] = {0};
    struct aws_byte_buf uuid_buf = aws_byte_buf_from_array(uuid_array, sizeof(uuid_array));
    uuid_buf.len = 0;

    ASSERT_SUCCESS(aws_uuid_to_str(&uuid, &uuid_buf));

    const char *expected_str = "01020304-0506-0708-090a-0b0c0d0e0f10";
    struct aws_byte_buf expected = aws_byte_buf_from_c_str(expected_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected.buffer, expected.len, uuid_buf.buffer, uuid_buf.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(prefilled_uuid_string, s_prefilled_uuid_string_fn)

static int s_uuid_string_short_buffer_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_uuid uuid;
    ASSERT_SUCCESS(aws_uuid_init(&uuid));

    uint8_t uuid_array[AWS_UUID_STR_LEN - 2] = {0};
    struct aws_byte_buf uuid_buf = aws_byte_buf_from_array(uuid_array, sizeof(uuid_array));
    uuid_buf.len = 0;

    ASSERT_ERROR(AWS_ERROR_SHORT_BUFFER, aws_uuid_to_str(&uuid, &uuid_buf));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(uuid_string_short_buffer, s_uuid_string_short_buffer_fn)

static int s_uuid_string_parse_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    uint8_t expected_uuid[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    const char *uuid_str = "01020304-0506-0708-090a-0b0c0d0e0f10";
    struct aws_byte_buf uuid_buf = aws_byte_buf_from_c_str(uuid_str);
    struct aws_byte_cursor uuid_cur = aws_byte_cursor_from_buf(&uuid_buf);

    struct aws_uuid uuid;
    ASSERT_SUCCESS(aws_uuid_init_from_str(&uuid, &uuid_cur));
    ASSERT_BIN_ARRAYS_EQUALS(expected_uuid, sizeof(expected_uuid), uuid.uuid_data, sizeof(uuid.uuid_data));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(uuid_string_parse, s_uuid_string_parse_fn)

static int s_uuid_string_parse_too_short_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    const char *uuid_str = "01020304-0506-0708-090a-0b0c0d0e0f1";
    struct aws_byte_buf uuid_buf = aws_byte_buf_from_c_str(uuid_str);
    struct aws_byte_cursor uuid_cur = aws_byte_cursor_from_buf(&uuid_buf);

    struct aws_uuid uuid;
    ASSERT_ERROR(AWS_ERROR_INVALID_BUFFER_SIZE, aws_uuid_init_from_str(&uuid, &uuid_cur));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(uuid_string_parse_too_short, s_uuid_string_parse_too_short_fn)

static int s_uuid_string_parse_malformed_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    const char *uuid_str = "010203-040506-0708-090a-0b0c0d0e0f10";
    struct aws_byte_buf uuid_buf = aws_byte_buf_from_c_str(uuid_str);
    struct aws_byte_cursor uuid_cur = aws_byte_cursor_from_buf(&uuid_buf);

    struct aws_uuid uuid;
    ASSERT_ERROR(AWS_ERROR_MALFORMED_INPUT_STRING, aws_uuid_init_from_str(&uuid, &uuid_cur));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(uuid_string_parse_malformed, s_uuid_string_parse_malformed_fn)
