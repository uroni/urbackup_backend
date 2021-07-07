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

#include <aws/common/byte_buf.h>
#include <aws/common/resource_name.h>
#include <aws/testing/aws_test_harness.h>

AWS_TEST_CASE(parse_resource_name_test, s_test_parse_resource_name)
static int s_test_parse_resource_name(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_byte_cursor arn_string_01 =
        AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("arn:aws-us-gov:iam::123456789012:user:ooo");
    struct aws_resource_name arn_01;
    AWS_ZERO_STRUCT(arn_01);
    ASSERT_SUCCESS(aws_resource_name_init_from_cur(&arn_01, &arn_string_01));
    ASSERT_BIN_ARRAYS_EQUALS("aws-us-gov", strlen("aws-us-gov"), arn_01.partition.ptr, arn_01.partition.len);
    ASSERT_BIN_ARRAYS_EQUALS("iam", strlen("iam"), arn_01.service.ptr, arn_01.service.len);
    ASSERT_BIN_ARRAYS_EQUALS("", strlen(""), arn_01.region.ptr, arn_01.region.len);
    ASSERT_BIN_ARRAYS_EQUALS("123456789012", strlen("123456789012"), arn_01.account_id.ptr, arn_01.account_id.len);
    ASSERT_BIN_ARRAYS_EQUALS("user:ooo", strlen("user:ooo"), arn_01.resource_id.ptr, arn_01.resource_id.len);

    struct aws_byte_cursor arn_string_02 =
        AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("arn:aws:cloudformation:us-east-1:1234567890:stack/FooBar");
    struct aws_resource_name arn_02;
    AWS_ZERO_STRUCT(arn_02);
    ASSERT_SUCCESS(aws_resource_name_init_from_cur(&arn_02, &arn_string_02));
    ASSERT_TRUE(aws_byte_cursor_eq_c_str(&arn_02.partition, "aws"));
    ASSERT_TRUE(aws_byte_cursor_eq_c_str(&arn_02.service, "cloudformation"));
    ASSERT_TRUE(aws_byte_cursor_eq_c_str(&arn_02.region, "us-east-1"));
    ASSERT_TRUE(aws_byte_cursor_eq_c_str(&arn_02.account_id, "1234567890"));
    ASSERT_TRUE(aws_byte_cursor_eq_c_str(&arn_02.resource_id, "stack/FooBar"));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(parse_resource_name_failures_test, s_test_parse_resource_name_failures)
static int s_test_parse_resource_name_failures(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_byte_cursor arn_string_01 = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("arn:aws-us-gov:iam::123456789012");
    struct aws_resource_name arn_01;
    AWS_ZERO_STRUCT(arn_01);
    /* arn has no resource id */
    ASSERT_ERROR(AWS_ERROR_MALFORMED_INPUT_STRING, aws_resource_name_init_from_cur(&arn_01, &arn_string_01));

    struct aws_byte_cursor arn_string_02 = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("arn:aws-us-gov:iam:");
    struct aws_resource_name arn_02;
    AWS_ZERO_STRUCT(arn_02);
    /* arn has no account id */
    ASSERT_ERROR(AWS_ERROR_MALFORMED_INPUT_STRING, aws_resource_name_init_from_cur(&arn_02, &arn_string_02));

    struct aws_byte_cursor arn_string_03 = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("arn:aws-us-gov:iam");
    struct aws_resource_name arn_03;
    AWS_ZERO_STRUCT(arn_03);
    /* arn has no region */
    ASSERT_ERROR(AWS_ERROR_MALFORMED_INPUT_STRING, aws_resource_name_init_from_cur(&arn_03, &arn_string_03));

    struct aws_byte_cursor arn_string_04 = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("arn:aws-us-gov");
    struct aws_resource_name arn_04;
    AWS_ZERO_STRUCT(arn_04);
    /* arn has no partition */
    ASSERT_ERROR(AWS_ERROR_MALFORMED_INPUT_STRING, aws_resource_name_init_from_cur(&arn_04, &arn_string_04));

    struct aws_byte_cursor arn_string_05 = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("arn");
    struct aws_resource_name arn_05;
    AWS_ZERO_STRUCT(arn_05);
    /* arn cannot parse arn prefix (must end with :) */
    ASSERT_ERROR(AWS_ERROR_MALFORMED_INPUT_STRING, aws_resource_name_init_from_cur(&arn_05, &arn_string_05));

    struct aws_byte_cursor arn_string_06 =
        AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("ar:aws:cloudformation:us-east-1:1234567890:stack/FooBar");
    struct aws_resource_name arn_06;
    AWS_ZERO_STRUCT(arn_06);
    /* arn prefix isn't present/correct */
    ASSERT_ERROR(AWS_ERROR_MALFORMED_INPUT_STRING, aws_resource_name_init_from_cur(&arn_06, &arn_string_06));

    struct aws_byte_cursor arn_string_07 =
        AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("arn:aws:cloudformation:us-east-1::stack/FooBar");
    struct aws_resource_name arn_07;
    AWS_ZERO_STRUCT(arn_07);
    /* account ID is an empty string */
    ASSERT_ERROR(AWS_ERROR_MALFORMED_INPUT_STRING, aws_resource_name_init_from_cur(&arn_07, &arn_string_07));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(resource_name_tostring_test, s_test_resource_name_tostring)
static int s_test_resource_name_tostring(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf buffer;
    AWS_ZERO_STRUCT(buffer);
    ASSERT_SUCCESS(aws_byte_buf_init(&buffer, allocator, 1600));

    struct aws_resource_name arn_01 = {.partition = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("aws-us-gov"),
                                       .service = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("iam"),
                                       .region = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL(""),
                                       .account_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("123456789"),
                                       .resource_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("group/crt")};
    ASSERT_SUCCESS(aws_byte_buf_append_resource_name(&buffer, &arn_01));
    ASSERT_BIN_ARRAYS_EQUALS(
        "arn:aws-us-gov:iam::123456789:group/crt",
        strlen("arn:aws-us-gov:iam::123456789:group/crt"),
        buffer.buffer,
        buffer.len);

    aws_byte_buf_reset(&buffer, false);
    struct aws_resource_name arn_02 = {.partition = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("aws"),
                                       .service = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("cloudformation"),
                                       .region = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("us-west-2"),
                                       .account_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("12345678910"),
                                       .resource_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("stack/MyStack")};
    ASSERT_SUCCESS(aws_byte_buf_append_resource_name(&buffer, &arn_02));
    ASSERT_BIN_ARRAYS_EQUALS(
        "arn:aws:cloudformation:us-west-2:12345678910:stack/MyStack",
        strlen("arn:aws:cloudformation:us-west-2:12345678910:stack/MyStack"),
        buffer.buffer,
        buffer.len);
    aws_byte_buf_clean_up(&buffer);

    uint8_t static_space[120];
    struct aws_byte_buf static_buffer = {.len = 0, .buffer = static_space, .capacity = 120, .allocator = NULL};
    struct aws_resource_name arn_03 = {.partition = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("aws"),
                                       .service = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("s3"),
                                       .region = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL(""),
                                       .account_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("123456789"),
                                       .resource_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("bucket/key")};
    ASSERT_SUCCESS(aws_byte_buf_append_resource_name(&static_buffer, &arn_03));
    ASSERT_BIN_ARRAYS_EQUALS(
        "arn:aws:s3::123456789:bucket/key",
        strlen("arn:aws:s3::123456789:bucket/key"),
        static_buffer.buffer,
        static_buffer.len);
    aws_byte_buf_clean_up(&static_buffer);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(resource_name_tostring_failure_test, s_test_resource_name_tostring_failure)
static int s_test_resource_name_tostring_failure(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf too_small_buffer;
    AWS_ZERO_STRUCT(too_small_buffer);
    ASSERT_SUCCESS(aws_byte_buf_init(&too_small_buffer, allocator, 16));
    struct aws_resource_name arn_01 = {.partition = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("aws-cn"),
                                       .service = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("dynamodb"),
                                       .region = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("cn-northwest-1"),
                                       .account_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("123456789"),
                                       .resource_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("Table/Books")};
    ASSERT_ERROR(AWS_ERROR_DEST_COPY_TOO_SMALL, aws_byte_buf_append_resource_name(&too_small_buffer, &arn_01));
    aws_byte_buf_clean_up(&too_small_buffer);

    uint8_t static_space[16];
    struct aws_byte_buf static_buffer = {.len = 0, .buffer = static_space, .capacity = 16, .allocator = NULL};
    struct aws_resource_name arn_02 = {.partition = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("aws"),
                                       .service = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("s3"),
                                       .region = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL(""),
                                       .account_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("123456789"),
                                       .resource_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("bucket/key")};
    ASSERT_ERROR(AWS_ERROR_DEST_COPY_TOO_SMALL, aws_byte_buf_append_resource_name(&static_buffer, &arn_02));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(resource_name_length_test, s_test_resource_name_length)
static int s_test_resource_name_length(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    size_t arn_length;
    struct aws_byte_buf buffer;
    AWS_ZERO_STRUCT(buffer);
    ASSERT_SUCCESS(aws_byte_buf_init(&buffer, allocator, 1600));

    struct aws_resource_name arn_01 = {.partition = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("aws-us-gov"),
                                       .service = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("iam"),
                                       .region = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL(""),
                                       .account_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("123456789"),
                                       .resource_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("group:crt")};
    ASSERT_SUCCESS(aws_resource_name_length(&arn_01, &arn_length));
    ASSERT_UINT_EQUALS(strlen("arn:aws-us-gov:iam::123456789:group:crt"), arn_length);

    aws_byte_buf_reset(&buffer, false);
    struct aws_resource_name arn_02 = {.partition = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("aws"),
                                       .service = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("cloudformation"),
                                       .region = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("us-west-2"),
                                       .account_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("12345678910"),
                                       .resource_id = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("stack/MyStack")};
    ASSERT_SUCCESS(aws_resource_name_length(&arn_02, &arn_length));
    ASSERT_UINT_EQUALS(strlen("arn:aws:cloudformation:us-west-2:12345678910:stack/MyStack"), arn_length);

    aws_byte_buf_clean_up(&buffer);
    return AWS_OP_SUCCESS;
}
