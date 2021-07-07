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

#include <aws/common/string.h>
#include <aws/testing/aws_test_harness.h>

AWS_TEST_CASE(test_buffer_cat, s_test_buffer_cat_fn)
static int s_test_buffer_cat_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf str1 = aws_byte_buf_from_c_str("testa");
    struct aws_byte_buf str2 = aws_byte_buf_from_c_str(";testb");
    struct aws_byte_buf str3 = aws_byte_buf_from_c_str(";testc");

    const char expected[] = "testa;testb;testc";

    struct aws_byte_buf destination;
    ASSERT_SUCCESS(aws_byte_buf_init(&destination, allocator, str1.len + str2.len + str3.len + 10));
    ASSERT_SUCCESS(aws_byte_buf_cat(&destination, 3, &str1, &str2, &str3));

    ASSERT_INT_EQUALS(strlen(expected), destination.len);
    ASSERT_INT_EQUALS(strlen(expected) + 10, destination.capacity);
    ASSERT_BIN_ARRAYS_EQUALS(expected, strlen(expected), destination.buffer, destination.len);

    destination.len = 0;
    ASSERT_SUCCESS(aws_byte_buf_cat(&destination, 1, &str1));
    ASSERT_INT_EQUALS(str1.len, destination.len);
    ASSERT_BIN_ARRAYS_EQUALS(str1.buffer, str1.len, destination.buffer, destination.len);

    destination.len = 0;
    ASSERT_SUCCESS(aws_byte_buf_cat(&destination, 0));
    ASSERT_INT_EQUALS(0, destination.len);

    aws_byte_buf_clean_up(&destination);

    return 0;
}

AWS_TEST_CASE(test_buffer_cat_dest_too_small, s_test_buffer_cat_dest_too_small_fn)
static int s_test_buffer_cat_dest_too_small_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf str1 = aws_byte_buf_from_c_str("testa");
    struct aws_byte_buf str2 = aws_byte_buf_from_c_str(";testb");
    struct aws_byte_buf str3 = aws_byte_buf_from_c_str(";testc");

    struct aws_byte_buf destination;
    ASSERT_SUCCESS(aws_byte_buf_init(&destination, allocator, str1.len + str2.len));
    ASSERT_INT_EQUALS(0, destination.len);
    ASSERT_INT_EQUALS(str1.len + str2.len, destination.capacity);

    ASSERT_ERROR(AWS_ERROR_DEST_COPY_TOO_SMALL, aws_byte_buf_cat(&destination, 3, &str1, &str2, &str3));

    aws_byte_buf_clean_up(&destination);

    return 0;
}

AWS_TEST_CASE(test_buffer_cpy, s_test_buffer_cpy_fn)
static int s_test_buffer_cpy_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf from_buf = aws_byte_buf_from_c_str("testa");
    struct aws_byte_cursor from = aws_byte_cursor_from_buf(&from_buf);
    struct aws_byte_buf destination;

    ASSERT_SUCCESS(aws_byte_buf_init(&destination, allocator, from.len + 10));
    ASSERT_SUCCESS(aws_byte_buf_append(&destination, &from));

    ASSERT_INT_EQUALS(from.len, destination.len);
    ASSERT_INT_EQUALS(from.len + 10, destination.capacity);

    ASSERT_BIN_ARRAYS_EQUALS(from.ptr, from.len, destination.buffer, destination.len);

    aws_byte_buf_clean_up(&destination);

    return 0;
}

AWS_TEST_CASE(test_buffer_cpy_offsets, s_test_buffer_cpy_offsets_fn)
static int s_test_buffer_cpy_offsets_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf from_buf = aws_byte_buf_from_c_str("testa");
    struct aws_byte_cursor from = aws_byte_cursor_from_buf(&from_buf);
    aws_byte_cursor_advance(&from, 2);
    struct aws_byte_buf destination;

    ASSERT_SUCCESS(aws_byte_buf_init(&destination, allocator, from_buf.len + 10));
    ASSERT_SUCCESS(aws_byte_buf_append(&destination, &from));

    ASSERT_INT_EQUALS(from_buf.len - 2, destination.len);
    ASSERT_INT_EQUALS(from_buf.len + 10, destination.capacity);

    char expected[] = "sta";

    ASSERT_BIN_ARRAYS_EQUALS(expected, strlen(expected), destination.buffer, destination.len);

    aws_byte_buf_clean_up(&destination);

    return 0;
}

AWS_TEST_CASE(test_buffer_cpy_dest_too_small, s_test_buffer_cpy_dest_too_small_fn)
static int s_test_buffer_cpy_dest_too_small_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf from_buf = aws_byte_buf_from_c_str("testa");
    struct aws_byte_cursor from = aws_byte_cursor_from_buf(&from_buf);

    struct aws_byte_buf destination;

    ASSERT_SUCCESS(aws_byte_buf_init(&destination, allocator, from.len - 1));
    ASSERT_ERROR(AWS_ERROR_DEST_COPY_TOO_SMALL, aws_byte_buf_append(&destination, &from));
    ASSERT_INT_EQUALS(0, destination.len);

    aws_byte_buf_clean_up(&destination);

    return 0;
}

AWS_TEST_CASE(test_buffer_cpy_offsets_dest_too_small, s_test_buffer_cpy_offsets_dest_too_small_fn)
static int s_test_buffer_cpy_offsets_dest_too_small_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf from_buf = aws_byte_buf_from_c_str("testa");
    struct aws_byte_cursor from = aws_byte_cursor_from_buf(&from_buf);
    struct aws_byte_buf destination;

    ASSERT_SUCCESS(aws_byte_buf_init(&destination, allocator, from.len));
    destination.len = 1;
    ASSERT_ERROR(AWS_ERROR_DEST_COPY_TOO_SMALL, aws_byte_buf_append(&destination, &from));
    ASSERT_INT_EQUALS(1, destination.len);

    aws_byte_buf_clean_up(&destination);

    return 0;
}

AWS_TEST_CASE(test_buffer_eq, s_test_buffer_eq_fn)
static int s_test_buffer_eq_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf b1 = aws_byte_buf_from_c_str("testa");
    struct aws_byte_buf b1_equal = aws_byte_buf_from_c_str("testa");

    struct aws_byte_buf b2 = aws_byte_buf_from_c_str("testb");

    b1.capacity = 5;
    b1_equal.allocator = allocator;

    ASSERT_TRUE(aws_byte_buf_eq(&b1, &b1_equal));
    ASSERT_TRUE(aws_byte_buf_eq(&b1, &b1));

    ASSERT_FALSE(aws_byte_buf_eq(&b1, &b2));
    return 0;
}

AWS_TEST_CASE(test_buffer_eq_same_content_different_len, s_test_buffer_eq_same_content_different_len_fn)
static int s_test_buffer_eq_same_content_different_len_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_byte_buf b1 = aws_byte_buf_from_c_str("testa");
    struct aws_byte_buf b2 = aws_byte_buf_from_c_str("testa");
    b2.len--;

    ASSERT_FALSE(aws_byte_buf_eq(&b1, &b2));

    return 0;
}

AWS_TEST_CASE(test_buffer_eq_null_internal_byte_buffer, s_test_buffer_eq_null_internal_byte_buffer_fn)
static int s_test_buffer_eq_null_internal_byte_buffer_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_byte_buf b1 = aws_byte_buf_from_array(NULL, 0);
    struct aws_byte_buf b2 = aws_byte_buf_from_array(NULL, 0);

    ASSERT_TRUE(aws_byte_buf_eq(&b1, &b2));
    ASSERT_TRUE(aws_byte_buf_eq(&b2, &b1));

    struct aws_byte_buf b3 = aws_byte_buf_from_c_str("abc");
    ASSERT_FALSE(aws_byte_buf_eq(&b1, &b3));
    return 0;
}

AWS_TEST_CASE(test_buffer_init_copy, s_test_buffer_init_copy_fn)
static int s_test_buffer_init_copy_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf src = aws_byte_buf_from_c_str("test_string");
    struct aws_byte_buf dest;

    ASSERT_SUCCESS(aws_byte_buf_init_copy(&dest, allocator, &src));
    ASSERT_TRUE(aws_byte_buf_eq(&src, &dest));
    ASSERT_INT_EQUALS(src.len, dest.capacity);
    ASSERT_PTR_EQUALS(allocator, dest.allocator);
    aws_byte_buf_clean_up(&dest);
    return 0;
}

AWS_TEST_CASE(test_buffer_init_copy_null_buffer, s_test_buffer_init_copy_null_buffer_fn)
static int s_test_buffer_init_copy_null_buffer_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf src;
    AWS_ZERO_STRUCT(src);
    struct aws_byte_buf dest;
    ASSERT_SUCCESS(aws_byte_buf_init_copy(&dest, allocator, &src));

    return 0;
}

AWS_TEST_CASE(test_buffer_advance, s_test_buffer_advance)
static int s_test_buffer_advance(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    uint8_t arr[16];
    struct aws_byte_buf src_buf = aws_byte_buf_from_empty_array(arr, sizeof(arr));

    struct aws_byte_buf dst_buf = {0};

    ASSERT_TRUE(aws_byte_buf_advance(&src_buf, &dst_buf, 4));
    ASSERT_NULL(dst_buf.allocator);
    ASSERT_INT_EQUALS(src_buf.len, 4);
    ASSERT_INT_EQUALS(dst_buf.len, 0);
    ASSERT_INT_EQUALS(dst_buf.capacity, 4);
    ASSERT_PTR_EQUALS(src_buf.buffer, arr);
    ASSERT_PTR_EQUALS(dst_buf.buffer, arr);

    ASSERT_TRUE(aws_byte_buf_advance(&src_buf, &dst_buf, 12));
    ASSERT_PTR_EQUALS(dst_buf.buffer, arr + 4);
    ASSERT_INT_EQUALS(src_buf.len, 16);

    src_buf.len = 12;
    ASSERT_FALSE(aws_byte_buf_advance(&src_buf, &dst_buf, 5));
    ASSERT_PTR_EQUALS(dst_buf.buffer, NULL);
    ASSERT_NULL(dst_buf.allocator);
    ASSERT_INT_EQUALS(dst_buf.len, 0);
    ASSERT_INT_EQUALS(dst_buf.capacity, 0);
    ASSERT_INT_EQUALS(src_buf.len, 12);

    return 0;
}

AWS_TEST_CASE(test_buffer_printf, s_test_buffer_printf)
static int s_test_buffer_printf(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    const char src[] = "abcdefg";

    char dst[100] = {0};

    /* check PRInSTR */
    memset(dst, 0, sizeof(dst));
    snprintf(dst, sizeof(dst), PRInSTR, 2, src);
    ASSERT_UINT_EQUALS('a', dst[0]);
    ASSERT_UINT_EQUALS('b', dst[1]);
    ASSERT_UINT_EQUALS(0, dst[2]);

    /* check AWS_BYTE_CURSOR_PRI() */
    struct aws_byte_cursor cursor = aws_byte_cursor_from_array(src, 2);
    memset(dst, 0, sizeof(dst));
    snprintf(dst, sizeof(dst), PRInSTR, AWS_BYTE_CURSOR_PRI(cursor));
    ASSERT_UINT_EQUALS('a', dst[0]);
    ASSERT_UINT_EQUALS('b', dst[1]);
    ASSERT_UINT_EQUALS(0, dst[2]);

    /* check AWS_BYTE_BUF_PRI() */
    struct aws_byte_buf buf = aws_byte_buf_from_array(src, 2);
    memset(dst, 0, sizeof(dst));
    snprintf(dst, sizeof(dst), PRInSTR, AWS_BYTE_BUF_PRI(buf));
    ASSERT_UINT_EQUALS('a', dst[0]);
    ASSERT_UINT_EQUALS('b', dst[1]);
    ASSERT_UINT_EQUALS(0, dst[2]);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_array_eq, s_test_array_eq)
static int s_test_array_eq(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    uint8_t a[] = {1, 2, 3};
    uint8_t b[] = {1, 2, 3};
    uint8_t c[] = {7, 8, 9};
    uint8_t d[] = {1, 2, 3, 4};

    /* Simple */
    ASSERT_TRUE(aws_array_eq(a, 3, b, 3));
    ASSERT_FALSE(aws_array_eq(a, 3, c, 3));
    ASSERT_FALSE(aws_array_eq(a, 3, d, 4));

    /* Comparisons agains self */
    ASSERT_TRUE(aws_array_eq(a, 3, a, 3));
    ASSERT_FALSE(aws_array_eq(a, 3, a, 2));

    /* Different data but size is 0 */
    ASSERT_TRUE(aws_array_eq(a, 0, c, 0));

    /* NULL inputs are OK if length is 0 */
    ASSERT_TRUE(aws_array_eq(NULL, 0, NULL, 0));
    ASSERT_TRUE(aws_array_eq(a, 0, NULL, 0));
    ASSERT_TRUE(aws_array_eq(NULL, 0, b, 0));

    return 0;
}

AWS_TEST_CASE(test_array_eq_ignore_case, s_test_array_eq_ignore_case)
static int s_test_array_eq_ignore_case(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;
    {
        uint8_t a[] = {'a', 'B', 'c', 'D', '1'};
        uint8_t b[] = {'a', 'b', 'C', 'D', '1'}; /* same as a */
        uint8_t c[] = {'a', 'b', 'c', 'd', '9'}; /* different */

        /* Simple */
        ASSERT_TRUE(aws_array_eq_ignore_case(a, 5, b, 5));
        ASSERT_FALSE(aws_array_eq_ignore_case(a, 5, c, 5));
        ASSERT_FALSE(aws_array_eq_ignore_case(a, 5, b, 3));

        /* Comparisons against self */
        ASSERT_TRUE(aws_array_eq_ignore_case(a, 5, a, 5));
        ASSERT_FALSE(aws_array_eq_ignore_case(a, 5, a, 4));

        /* Different data but size is 0 */
        ASSERT_TRUE(aws_array_eq_ignore_case(a, 0, c, 0));

        /* NULL inputs are OK if length is 0 */
        ASSERT_TRUE(aws_array_eq_ignore_case(NULL, 0, NULL, 0));
        ASSERT_TRUE(aws_array_eq_ignore_case(a, 0, NULL, 0));
        ASSERT_TRUE(aws_array_eq_ignore_case(NULL, 0, b, 0));
    }

    {
        /* Comparison should continue beyond null-terminator */
        uint8_t a[] = {'a', 0, 'b'};
        uint8_t b[] = {'a', 0, 'c'};
        uint8_t c[] = {'a', 0, 'b'};
        ASSERT_FALSE(aws_array_eq_ignore_case(&a, 3, &b, 3));
        ASSERT_TRUE(aws_array_eq_ignore_case(&a, 3, &c, 3));
    }

    {
        /* Compare every possible uint8_t value, then lower against upper, then upper against lower.
         * Ex:
         * a_src = {0 ... 255, 'a' ... 'z', 'A' ... 'Z'};
         * b_src = {0 ... 255, 'A' ... 'Z', 'a' ... 'z'};
         */
        uint8_t a[256 + 26 + 26];
        uint8_t b[256 + 26 + 26];
        for (size_t i = 0; i < 256; ++i) {
            a[i] = (uint8_t)i;
            b[i] = (uint8_t)i;
        }
        for (size_t i = 0, c = 'a'; c <= 'z'; ++i, ++c) {
            a[256 + i] = (uint8_t)c;
            b[256 + 26 + i] = (uint8_t)c;
        }
        for (size_t i = 0, c = 'A'; c <= 'Z'; ++i, ++c) {
            a[256 + 26 + i] = (uint8_t)c;
            b[256 + i] = (uint8_t)c;
        }

        ASSERT_TRUE(aws_array_eq_ignore_case(&a, sizeof(a), &b, sizeof(b)));
    }

    return 0;
}

AWS_TEST_CASE(test_array_eq_c_str, s_test_array_eq_c_str)
static int s_test_array_eq_c_str(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    {
        uint8_t arr_a[] = {'a', 'b', 'c'};
        const char *str_a = "abc";
        const char *str_b = "xyz";
        const char *str_c = "abcd";
        const char *empty = "";

        /* Simple */
        ASSERT_TRUE(aws_array_eq_c_str(arr_a, 3, str_a));
        ASSERT_FALSE(aws_array_eq_c_str(arr_a, 3, str_b));
        ASSERT_FALSE(aws_array_eq_c_str(arr_a, 3, str_c));

        /* Referencing self */
        ASSERT_TRUE(aws_array_eq_c_str(str_a, 3, str_a));
        ASSERT_FALSE(aws_array_eq_c_str(str_a, 2, str_a));

        /* Check length 0 */
        ASSERT_TRUE(aws_array_eq_c_str(arr_a, 0, empty));
        ASSERT_FALSE(aws_array_eq_c_str(arr_a, 0, str_a));

        /* NULL array is OK if length is 0 */
        ASSERT_TRUE(aws_array_eq_c_str(NULL, 0, empty));
        ASSERT_FALSE(aws_array_eq_c_str(NULL, 0, str_a));
    }

    {
        /* Array is not expected to contain null-terminator */
        uint8_t arr_a[] = {'a', 'b', 'c', 0};
        const char *str_a = "abc";
        ASSERT_FALSE(aws_array_eq_c_str(arr_a, 4, str_a));
    }

    return 0;
}

AWS_TEST_CASE(test_array_eq_c_str_ignore_case, s_test_array_eq_c_str_ignore_case)
static int s_test_array_eq_c_str_ignore_case(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    {
        uint8_t arr_a[] = {'a', 'B', 'c'};
        const char *str_a = "Abc";
        const char *str_b = "xyz";
        const char *str_c = "aBcd";
        const char *empty = "";

        /* Simple */
        ASSERT_TRUE(aws_array_eq_c_str_ignore_case(arr_a, 3, str_a));
        ASSERT_FALSE(aws_array_eq_c_str_ignore_case(arr_a, 3, str_b));
        ASSERT_FALSE(aws_array_eq_c_str_ignore_case(arr_a, 3, str_c));

        /* Referencing self */
        ASSERT_TRUE(aws_array_eq_c_str_ignore_case(str_a, 3, str_a));
        ASSERT_FALSE(aws_array_eq_c_str_ignore_case(str_a, 2, str_a));

        /* Check length 0 */
        ASSERT_TRUE(aws_array_eq_c_str_ignore_case(arr_a, 0, empty));
        ASSERT_FALSE(aws_array_eq_c_str_ignore_case(arr_a, 0, str_a));

        /* NULL array is OK if length is 0 */
        ASSERT_TRUE(aws_array_eq_c_str_ignore_case(NULL, 0, empty));
        ASSERT_FALSE(aws_array_eq_c_str_ignore_case(NULL, 0, str_a));
    }

    {
        /* Array is not expected to contain null-terminator */
        uint8_t arr_a[] = {'a', 'b', 'c', 0};
        const char *str_a = "abc";
        ASSERT_FALSE(aws_array_eq_c_str_ignore_case(arr_a, 4, str_a));
    }

    return 0;
}

AWS_TEST_CASE(test_array_hash_ignore_case, s_test_array_hash_ignore_case)
static int s_test_array_hash_ignore_case(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    {
        /* Check against known FNV-1A values */
        uint8_t a[] = {'a', 'b', 'c'};
        ASSERT_UINT_EQUALS(0xe71fa2190541574bULL, aws_hash_array_ignore_case(a, 3));

        uint8_t b[] = {'A', 'B', 'C'};
        ASSERT_UINT_EQUALS(0xe71fa2190541574bULL, aws_hash_array_ignore_case(&b, 3));
    }

    {
        uint8_t a[] = {'a', 'B', 'c', 1, 2, 3};
        uint8_t b[] = {'A', 'b', 'c', 1, 2, 3};
        ASSERT_TRUE(aws_hash_array_ignore_case(a, 6) == aws_hash_array_ignore_case(b, 6));
    }

    {
        uint8_t a[] = {'a', 'b', 'c'};
        uint8_t b[] = {'x', 'y', 'z'};
        ASSERT_FALSE(aws_hash_array_ignore_case(a, 3) == aws_hash_array_ignore_case(b, 3));
    }

    return 0;
}

static int s_do_append_dynamic_test(
    struct aws_allocator *allocator,
    size_t starting_size,
    size_t append_size,
    size_t iterations) {
    struct aws_byte_buf accum_buf;
    aws_byte_buf_init(&accum_buf, allocator, starting_size);
    memset(accum_buf.buffer, 0, starting_size);
    accum_buf.len = starting_size;

    struct aws_byte_buf append_buf;
    aws_byte_buf_init(&append_buf, allocator, append_size);
    append_buf.len = append_size;

    struct aws_byte_cursor append_cursor = aws_byte_cursor_from_buf(&append_buf);

    for (size_t i = 0; i < iterations; ++i) {

        /*
         * Initialize the source and dest buffers to different, easily recognizable byte blocks
         */
        memset(append_buf.buffer, 255, append_buf.capacity);
        memset(accum_buf.buffer, 0, accum_buf.capacity);

        size_t before_size = accum_buf.len;
        ASSERT_TRUE(aws_byte_buf_append_dynamic(&accum_buf, &append_cursor) == AWS_OP_SUCCESS);
        size_t after_size = accum_buf.len;

        size_t expected_len = starting_size + (i + 1) * append_size;
        ASSERT_TRUE(accum_buf.capacity >= expected_len);
        ASSERT_TRUE(after_size == expected_len);

        /*
         * Verify post-append contents.
         *
         * Check that the result has the right number of 0s followed by the right number of
         * 255s.
         */
        for (size_t bi = 0; bi < before_size; ++bi) {
            ASSERT_TRUE(accum_buf.buffer[bi] == 0);
        }

        for (size_t ai = before_size; ai < after_size; ++ai) {
            ASSERT_TRUE(accum_buf.buffer[ai] == 255);
        }
    }

    aws_byte_buf_clean_up(&accum_buf);
    aws_byte_buf_clean_up(&append_buf);

    return AWS_OP_SUCCESS;
}

static int s_test_byte_buf_append_dynamic(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    /*
     * Throw a small sample of different growth request profiles at the function
     */
    ASSERT_TRUE(s_do_append_dynamic_test(allocator, 1, 10000, 1) == AWS_OP_SUCCESS);
    ASSERT_TRUE(s_do_append_dynamic_test(allocator, 1, 1, 1000) == AWS_OP_SUCCESS);
    ASSERT_TRUE(s_do_append_dynamic_test(allocator, 10000, 1, 2) == AWS_OP_SUCCESS);
    ASSERT_TRUE(s_do_append_dynamic_test(allocator, 100, 10, 100) == AWS_OP_SUCCESS);

    return 0;
}
AWS_TEST_CASE(test_byte_buf_append_dynamic, s_test_byte_buf_append_dynamic)

AWS_STATIC_STRING_FROM_LITERAL(s_to_lower_test, "UPPerANdLowercASE");

static int s_test_byte_buf_append_lookup_success(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf buffer;
    aws_byte_buf_init(&buffer, allocator, s_to_lower_test->len);

    struct aws_byte_cursor to_lower_cursor = aws_byte_cursor_from_c_str((char *)s_to_lower_test->bytes);

    ASSERT_TRUE(
        aws_byte_buf_append_with_lookup(&buffer, &to_lower_cursor, aws_lookup_table_to_lower_get()) == AWS_OP_SUCCESS);
    ASSERT_TRUE(buffer.len == s_to_lower_test->len);
    for (size_t i = 0; i < s_to_lower_test->len; ++i) {
        uint8_t value = buffer.buffer[i];
        ASSERT_TRUE(value > 'Z' || value < 'A');
    }

    aws_byte_buf_clean_up(&buffer);

    return 0;
}
AWS_TEST_CASE(test_byte_buf_append_lookup_success, s_test_byte_buf_append_lookup_success)

static int s_test_reset_body(struct aws_byte_buf *buffer) {
    struct aws_byte_cursor to_lower_cursor = aws_byte_cursor_from_c_str((char *)s_to_lower_test->bytes);

    ASSERT_TRUE(
        aws_byte_buf_append_with_lookup(buffer, &to_lower_cursor, aws_lookup_table_to_lower_get()) == AWS_OP_SUCCESS);
    ASSERT_TRUE(buffer->len == s_to_lower_test->len);
    for (size_t i = 0; i < s_to_lower_test->len; ++i) {
        uint8_t value = buffer->buffer[i];
        ASSERT_TRUE(value > 'Z' || value < 'A');
    }
    return 0;
}
static int s_test_byte_buf_reset(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf buffer;
    aws_byte_buf_init(&buffer, allocator, s_to_lower_test->len);
    ASSERT_SUCCESS(s_test_reset_body(&buffer));

    size_t old_cap = buffer.capacity;
    aws_byte_buf_reset(&buffer, false);
    ASSERT_TRUE(buffer.len == 0);
    ASSERT_TRUE(buffer.capacity == old_cap);
    ASSERT_SUCCESS(s_test_reset_body(&buffer));

    old_cap = buffer.capacity;
    aws_byte_buf_reset(&buffer, true);
    ASSERT_TRUE(buffer.len == 0);
    ASSERT_TRUE(buffer.capacity == old_cap);
    for (size_t i = 0; i < buffer.capacity; i++) {
        ASSERT_TRUE(buffer.buffer[i] == 0);
    }
    ASSERT_SUCCESS(s_test_reset_body(&buffer));

    aws_byte_buf_clean_up(&buffer);
    /* check that reset succeeds even on an empty buffer */
    aws_byte_buf_reset(&buffer, true);
    return 0;
}
AWS_TEST_CASE(test_byte_buf_reset, s_test_byte_buf_reset)

static int s_test_byte_buf_append_lookup_failure(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf buffer;
    aws_byte_buf_init(&buffer, allocator, 3);

    struct aws_byte_cursor to_lower_cursor = aws_byte_cursor_from_c_str((char *)s_to_lower_test->bytes);

    ASSERT_TRUE(
        aws_byte_buf_append_with_lookup(&buffer, &to_lower_cursor, aws_lookup_table_to_lower_get()) == AWS_OP_ERR);

    aws_byte_buf_clean_up(&buffer);

    return 0;
}
AWS_TEST_CASE(test_byte_buf_append_lookup_failure, s_test_byte_buf_append_lookup_failure)

AWS_STATIC_STRING_FROM_LITERAL(s_reserve_test_suffix, "ReserveTest");
AWS_STATIC_STRING_FROM_LITERAL(s_reserve_test_prefix, "Successful");
AWS_STATIC_STRING_FROM_LITERAL(s_reserve_test_prefix_concatenated, "SuccessfulReserveTest");

static int s_test_byte_buf_reserve(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    struct aws_byte_buf buffer;
    aws_byte_buf_init(&buffer, allocator, s_reserve_test_prefix->len);

    struct aws_byte_cursor prefix_cursor = aws_byte_cursor_from_string(s_reserve_test_prefix);
    ASSERT_TRUE(aws_byte_buf_append(&buffer, &prefix_cursor) == AWS_OP_SUCCESS);

    struct aws_byte_cursor suffix_cursor = aws_byte_cursor_from_string(s_reserve_test_suffix);
    ASSERT_TRUE(aws_byte_buf_append(&buffer, &suffix_cursor) == AWS_OP_ERR);

    aws_byte_buf_reserve(&buffer, s_reserve_test_prefix_concatenated->len);
    ASSERT_TRUE(aws_byte_buf_append(&buffer, &suffix_cursor) == AWS_OP_SUCCESS);

    ASSERT_TRUE(aws_byte_buf_eq_c_str(&buffer, (char *)s_reserve_test_prefix_concatenated->bytes));

    aws_byte_buf_clean_up(&buffer);

    return 0;
}
AWS_TEST_CASE(test_byte_buf_reserve, s_test_byte_buf_reserve)

static int s_test_byte_buf_reserve_relative(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    struct aws_byte_buf buffer;
    aws_byte_buf_init(&buffer, allocator, 1);

    struct aws_byte_cursor prefix_cursor = aws_byte_cursor_from_string(s_reserve_test_prefix);

    ASSERT_TRUE(aws_byte_buf_reserve_relative(&buffer, prefix_cursor.len) == AWS_OP_SUCCESS);
    ASSERT_TRUE(aws_byte_buf_append(&buffer, &prefix_cursor) == AWS_OP_SUCCESS);

    struct aws_byte_cursor suffix_cursor = aws_byte_cursor_from_string(s_reserve_test_suffix);
    ASSERT_TRUE(aws_byte_buf_reserve_relative(&buffer, suffix_cursor.len) == AWS_OP_SUCCESS);
    ASSERT_TRUE(aws_byte_buf_append(&buffer, &suffix_cursor) == AWS_OP_SUCCESS);

    ASSERT_TRUE(aws_byte_buf_eq_c_str(&buffer, (char *)s_reserve_test_prefix_concatenated->bytes));

    aws_byte_buf_clean_up(&buffer);

    return 0;
}
AWS_TEST_CASE(test_byte_buf_reserve_relative, s_test_byte_buf_reserve_relative)

static int s_test_byte_cursor_compare_lexical(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    struct aws_byte_cursor test_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("test");
    struct aws_byte_cursor test_cursor2 = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("test");
    struct aws_byte_cursor test1_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("test1");
    struct aws_byte_cursor test2_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("test2");
    struct aws_byte_cursor abc_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("abc");

    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&test_cursor, &test_cursor2) == 0);

    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&test_cursor, &abc_cursor) > 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&abc_cursor, &test_cursor) < 0);

    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&test_cursor, &test2_cursor) < 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&test2_cursor, &test_cursor) > 0);

    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&test1_cursor, &test2_cursor) < 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&test2_cursor, &test1_cursor) > 0);

    struct aws_byte_cursor ff_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("\xFF\xFF");
    struct aws_byte_cursor one_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("\x01\x01");

    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&ff_cursor, &one_cursor) > 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&one_cursor, &ff_cursor) < 0);

    struct aws_byte_cursor Test_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("Test");
    struct aws_byte_cursor tesT_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("tesT");

    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&Test_cursor, &tesT_cursor) < 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&tesT_cursor, &Test_cursor) > 0);

    return 0;
}
AWS_TEST_CASE(test_byte_cursor_compare_lexical, s_test_byte_cursor_compare_lexical)

static int s_test_byte_cursor_compare_lookup(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    struct aws_byte_cursor Test_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("Test");
    struct aws_byte_cursor tesT_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("tesT");

    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&Test_cursor, &tesT_cursor, aws_lookup_table_to_lower_get()) == 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&tesT_cursor, &Test_cursor, aws_lookup_table_to_lower_get()) == 0);

    struct aws_byte_cursor ABC_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("ABC");
    struct aws_byte_cursor abc_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("abc");

    ASSERT_TRUE(aws_byte_cursor_compare_lexical(&ABC_cursor, &abc_cursor) < 0);

    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&ABC_cursor, &abc_cursor, aws_lookup_table_to_lower_get()) == 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&abc_cursor, &ABC_cursor, aws_lookup_table_to_lower_get()) == 0);

    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&ABC_cursor, &tesT_cursor, aws_lookup_table_to_lower_get()) < 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&tesT_cursor, &ABC_cursor, aws_lookup_table_to_lower_get()) > 0);

    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&abc_cursor, &tesT_cursor, aws_lookup_table_to_lower_get()) < 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&tesT_cursor, &abc_cursor, aws_lookup_table_to_lower_get()) > 0);

    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&abc_cursor, &Test_cursor, aws_lookup_table_to_lower_get()) < 0);
    ASSERT_TRUE(aws_byte_cursor_compare_lookup(&Test_cursor, &abc_cursor, aws_lookup_table_to_lower_get()) > 0);

    return 0;
}
AWS_TEST_CASE(test_byte_cursor_compare_lookup, s_test_byte_cursor_compare_lookup)

static int s_test_byte_buf_append_and_update_fail(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    struct aws_byte_buf buffer;
    aws_byte_buf_init(&buffer, allocator, 10);

    struct aws_byte_cursor test_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("TOOOOOOOO LONG");
    struct aws_byte_cursor test_cursor_copy = test_cursor;

    ASSERT_FAILS(aws_byte_buf_append_and_update(&buffer, &test_cursor));
    ASSERT_TRUE((test_cursor.ptr == test_cursor_copy.ptr) && (test_cursor.len == test_cursor_copy.len));

    aws_byte_buf_clean_up(&buffer);

    return 0;
}
AWS_TEST_CASE(test_byte_buf_append_and_update_fail, s_test_byte_buf_append_and_update_fail)

static int s_test_byte_buf_append_and_update_success(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    struct aws_byte_buf buffer;
    aws_byte_buf_init(&buffer, allocator, 12);

    struct aws_byte_cursor test_cursor = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("SHORT");
    struct aws_byte_cursor test_cursor_copy = test_cursor;

    ASSERT_SUCCESS(aws_byte_buf_append_and_update(&buffer, &test_cursor));
    ASSERT_TRUE(test_cursor.ptr == buffer.buffer);
    ASSERT_TRUE(aws_byte_cursor_eq(&test_cursor, &test_cursor_copy));

    struct aws_byte_cursor test_cursor2 = AWS_BYTE_CUR_INIT_FROM_STRING_LITERAL("STOP");
    struct aws_byte_cursor test_cursor_copy2 = test_cursor2;

    ASSERT_SUCCESS(aws_byte_buf_append_and_update(&buffer, &test_cursor2));
    ASSERT_TRUE(test_cursor2.ptr == buffer.buffer + test_cursor.len);
    ASSERT_TRUE(aws_byte_cursor_eq(&test_cursor2, &test_cursor_copy2));

    ASSERT_TRUE(buffer.len == test_cursor.len + test_cursor2.len);

    aws_byte_buf_clean_up(&buffer);

    return 0;
}
AWS_TEST_CASE(test_byte_buf_append_and_update_success, s_test_byte_buf_append_and_update_success)
