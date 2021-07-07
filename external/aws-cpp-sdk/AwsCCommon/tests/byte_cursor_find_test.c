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

#include <aws/common/byte_buf.h>
#include <aws/testing/aws_test_harness.h>

static int s_test_byte_cursor_find_str_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    const char *string_with_match = "This is a string and we want to find a substring of it.";
    const char *to_find = "and we want";

    struct aws_byte_cursor string_with_match_cur = aws_byte_cursor_from_c_str(string_with_match);
    struct aws_byte_cursor to_find_cur = aws_byte_cursor_from_c_str(to_find);

    struct aws_byte_cursor find_res;
    AWS_ZERO_STRUCT(find_res);

    ASSERT_SUCCESS(aws_byte_cursor_find_exact(&string_with_match_cur, &to_find_cur, &find_res));
    ASSERT_BIN_ARRAYS_EQUALS(to_find_cur.ptr, to_find_cur.len, find_res.ptr, to_find_cur.len);
    ASSERT_UINT_EQUALS(string_with_match_cur.len - (find_res.ptr - string_with_match_cur.ptr), find_res.len);
    ASSERT_PTR_EQUALS(string_with_match_cur.ptr + (find_res.ptr - string_with_match_cur.ptr), find_res.ptr);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_byte_cursor_find_str, s_test_byte_cursor_find_str_fn)

static int s_test_byte_cursor_find_str_not_found_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    const char *string_with_match = "This is a string and we want to find a substring of it.";
    const char *to_find = "and we went";

    struct aws_byte_cursor string_with_match_cur = aws_byte_cursor_from_c_str(string_with_match);
    struct aws_byte_cursor to_find_cur = aws_byte_cursor_from_c_str(to_find);

    struct aws_byte_cursor find_res;
    AWS_ZERO_STRUCT(find_res);

    ASSERT_ERROR(
        AWS_ERROR_STRING_MATCH_NOT_FOUND, aws_byte_cursor_find_exact(&string_with_match_cur, &to_find_cur, &find_res));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_byte_cursor_find_str_not_found, s_test_byte_cursor_find_str_not_found_fn)

static int s_test_byte_cursor_find_str_longer_than_input_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    const char *string_with_match = "This ";
    const char *to_find = "and we want";

    struct aws_byte_cursor string_with_match_cur = aws_byte_cursor_from_c_str(string_with_match);
    struct aws_byte_cursor to_find_cur = aws_byte_cursor_from_c_str(to_find);

    struct aws_byte_cursor find_res;
    AWS_ZERO_STRUCT(find_res);

    ASSERT_ERROR(
        AWS_ERROR_STRING_MATCH_NOT_FOUND, aws_byte_cursor_find_exact(&string_with_match_cur, &to_find_cur, &find_res));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_byte_cursor_find_str_longer_than_input, s_test_byte_cursor_find_str_longer_than_input_fn)
