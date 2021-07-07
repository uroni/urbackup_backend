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

#include <aws/common/string.h>

#include <aws/common/hash_table.h>
#include <aws/testing/aws_test_harness.h>

AWS_TEST_CASE(string_tests, s_string_tests_fn);
static int s_string_tests_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    /* Test: static string creation from macro works. */
    AWS_STATIC_STRING_FROM_LITERAL(test_string_1, "foofaraw");
    ASSERT_NULL(test_string_1->allocator, "Static string should have no allocator.");
    ASSERT_INT_EQUALS(test_string_1->len, 8, "Length should have been set correctly.");
    ASSERT_BIN_ARRAYS_EQUALS(
        aws_string_bytes(test_string_1),
        test_string_1->len,
        "foofaraw",
        8,
        "Data bytes should have been set correctly.");
    ASSERT_INT_EQUALS(
        aws_string_bytes(test_string_1)[test_string_1->len], '\0', "Static string should have null byte at end.");

    /* Test: string creation works. */
    struct aws_string *test_string_2 = aws_string_new_from_c_str(allocator, "foofaraw");
    ASSERT_NOT_NULL(test_string_2, "Memory allocation of string should have succeeded.");
    ASSERT_PTR_EQUALS(test_string_2->allocator, allocator, "Allocator should have been set correctly.");
    ASSERT_INT_EQUALS(test_string_2->len, 8, "Length should have been set correctly.");
    ASSERT_BIN_ARRAYS_EQUALS(
        aws_string_bytes(test_string_2),
        test_string_2->len,
        "foofaraw",
        8,
        "Data bytes should have been set correctly.");
    ASSERT_INT_EQUALS(
        aws_string_bytes(test_string_2)[test_string_2->len],
        '\0',
        "String from C-string should have null byte at end.");

    /* Test: strings from first two tests are equal and have same hashes. */
    ASSERT_TRUE(aws_string_eq(test_string_1, test_string_2), "Buffers should be equal.");
    ASSERT_INT_EQUALS(
        aws_hash_string(test_string_1), aws_hash_string(test_string_2), "Hash values of byte buffers should be equal.");

    /* Test: write from string to byte cursor works. */
    uint8_t dest[8] = {0};
    struct aws_byte_buf dest_cur = aws_byte_buf_from_empty_array(dest, sizeof(dest));

    ASSERT_TRUE(
        aws_byte_buf_write_from_whole_string(&dest_cur, test_string_2),
        "Write from whole string should have succeeded.");
    ASSERT_BIN_ARRAYS_EQUALS(dest, 8, "foofaraw", 8);

    /* Test: write from string fails cleanly when byte cursor too short. */
    int8_t short_dest[7] = {0};
    struct aws_byte_buf short_dest_buf = aws_byte_buf_from_empty_array(short_dest, sizeof(short_dest));

    ASSERT_FALSE(
        aws_byte_buf_write_from_whole_string(&short_dest_buf, test_string_2),
        "Write from whole buffer should have failed.");
    ASSERT_INT_EQUALS(short_dest_buf.len, 0, "Destination cursor length should be unchanged.");
    ASSERT_INT_EQUALS(0, short_dest_buf.buffer[0], "Destination cursor should not have received data.");

    /* Test: can duplicate both a static string and an allocated one. */

    struct aws_string *dup_string_1 = aws_string_new_from_string(allocator, test_string_1);
    ASSERT_NOT_NULL(dup_string_1, "Memory allocation of string should have succeeded.");
    ASSERT_TRUE(aws_string_eq(test_string_1, dup_string_1), "Strings should be equal.");

    struct aws_string *dup_string_2 = aws_string_new_from_string(allocator, test_string_2);
    ASSERT_NOT_NULL(dup_string_2, "Memory allocation of string should have succeeded.");
    ASSERT_TRUE(aws_string_eq(test_string_2, dup_string_2), "Strings should be equal.");

    /* Test: can clone_or_reuse both a static string and an allocated one. */
    struct aws_string *clone_string_1 = aws_string_clone_or_reuse(allocator, test_string_1);
    ASSERT_NOT_NULL(clone_string_1, "Memory allocation of string should have succeeded.");
    ASSERT_TRUE(aws_string_eq(test_string_1, clone_string_1), "Strings should be equal.");
    ASSERT_TRUE(test_string_1 == clone_string_1, "Static strings should be reused");

    struct aws_string *clone_string_2 = aws_string_clone_or_reuse(allocator, test_string_2);
    ASSERT_NOT_NULL(clone_string_2, "Memory allocation of string should have succeeded.");
    ASSERT_TRUE(aws_string_eq(test_string_2, clone_string_2), "Strings should be equal.");
    ASSERT_TRUE(test_string_2 != clone_string_2, "Dynamic strings should not be reused");

    /* Test: all allocated memory is deallocated properly. */
    aws_string_destroy(test_string_2);
    aws_string_destroy(dup_string_1);
    aws_string_destroy(dup_string_2);
    aws_string_destroy(clone_string_1);
    aws_string_destroy(clone_string_2);

    return 0;
}

AWS_TEST_CASE(binary_string_test, s_binary_string_test_fn);
static int s_binary_string_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    uint8_t test_array[] = {0x86, 0x75, 0x30, 0x90, 0x00, 0xde, 0xad, 0xbe, 0xef};
    size_t len = sizeof(test_array);
    struct aws_string *binary_string = aws_string_new_from_array(allocator, test_array, len);

    ASSERT_NOT_NULL(binary_string, "Memory allocation of string should have succeeded.");
    ASSERT_PTR_EQUALS(allocator, binary_string->allocator, "Allocator should have been set correctly.");
    ASSERT_BIN_ARRAYS_EQUALS(
        test_array,
        len,
        aws_string_bytes(binary_string),
        binary_string->len,
        "Binary string bytes should be same as source array.");
    ASSERT_INT_EQUALS(
        aws_string_bytes(binary_string)[binary_string->len],
        0x00,
        "String from binary array should have null byte at end");
    aws_string_destroy(binary_string);
    return 0;
}

AWS_TEST_CASE(string_compare_test, s_string_compare_test_fn);
static int s_string_compare_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    AWS_STATIC_STRING_FROM_LITERAL(empty, "");
    AWS_STATIC_STRING_FROM_LITERAL(foo, "foo");
    AWS_STATIC_STRING_FROM_LITERAL(bar, "bar");
    AWS_STATIC_STRING_FROM_LITERAL(foobar, "foobar");
    AWS_STATIC_STRING_FROM_LITERAL(foo2, "foo");
    AWS_STATIC_STRING_FROM_LITERAL(foobaz, "foobaz");
    AWS_STATIC_STRING_FROM_LITERAL(bar_food, "bar food");
    AWS_STATIC_STRING_FROM_LITERAL(bar_null_food, "bar\0food");
    AWS_STATIC_STRING_FROM_LITERAL(bar_null_back, "bar\0back");

    ASSERT_TRUE(aws_string_compare(empty, bar) < 0);
    ASSERT_TRUE(aws_string_compare(foo, bar) > 0);
    ASSERT_TRUE(aws_string_compare(bar, foo) < 0);
    ASSERT_TRUE(aws_string_compare(foo, foobar) < 0);
    ASSERT_TRUE(aws_string_compare(foo, foo2) == 0);
    ASSERT_TRUE(aws_string_compare(foobar, foobaz) < 0);
    ASSERT_TRUE(aws_string_compare(foobaz, empty) > 0);
    ASSERT_TRUE(aws_string_compare(empty, empty) == 0);
    ASSERT_TRUE(aws_string_compare(foo, bar_food) > 0);
    ASSERT_TRUE(aws_string_compare(bar_food, bar) > 0);
    ASSERT_TRUE(aws_string_compare(bar_null_food, bar) > 0);
    ASSERT_TRUE(aws_string_compare(bar_null_food, bar_food) < 0);
    ASSERT_TRUE(aws_string_compare(bar_null_food, bar_null_back) > 0);

    /* Test that bytes are being compared as unsigned integers. */
    AWS_STATIC_STRING_FROM_LITERAL(x80, "\x80");
    AWS_STATIC_STRING_FROM_LITERAL(x7f, "\x79");
    ASSERT_TRUE(aws_string_compare(x80, x7f) > 0);
    ASSERT_TRUE(aws_string_compare(x7f, x80) < 0);
    return 0;
}

AWS_TEST_CASE(string_destroy_secure_test, string_destroy_secure_test_fn);
static int string_destroy_secure_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    /* Just verifies all memory was freed. */
    struct aws_string *empty = aws_string_new_from_c_str(allocator, "");
    struct aws_string *logorrhea = aws_string_new_from_c_str(allocator, "logorrhea");
    const uint8_t bytes[] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x86, 0x75, 0x30, 0x90};
    struct aws_string *deadbeef = aws_string_new_from_array(allocator, bytes, sizeof(bytes));
    ASSERT_NOT_NULL(empty, "Memory allocation of string should have succeeded.");
    ASSERT_NOT_NULL(logorrhea, "Memory allocation of string should have succeeded.");
    ASSERT_NOT_NULL(deadbeef, "Memory allocation of string should have succeeded.");
    aws_string_destroy_secure(empty);
    aws_string_destroy_secure(logorrhea);
    aws_string_destroy_secure(deadbeef);
    return 0;
}

static int secure_strlen_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    size_t str_len = 0;
    const char test_string[] = "HelloWorld!";
    ASSERT_SUCCESS(aws_secure_strlen(test_string, sizeof(test_string), &str_len));
    ASSERT_UINT_EQUALS(sizeof(test_string) - 1, str_len);

    ASSERT_ERROR(AWS_ERROR_INVALID_ARGUMENT, aws_secure_strlen(NULL, sizeof(test_string), &str_len));
    ASSERT_ERROR(AWS_ERROR_INVALID_ARGUMENT, aws_secure_strlen(test_string, sizeof(test_string), NULL));
    ASSERT_ERROR(
        AWS_ERROR_C_STRING_BUFFER_NOT_NULL_TERMINATED,
        aws_secure_strlen(test_string, sizeof(test_string) - 1, &str_len));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(secure_strlen_test, secure_strlen_test_fn)
