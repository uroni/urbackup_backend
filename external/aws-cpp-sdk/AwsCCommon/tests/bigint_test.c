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
#include <aws/common/bigint.h>

#include <aws/testing/aws_test_harness.h>

struct bigint_uint64_init_test {
    uint64_t value;
    const char *expected_hex_serialization;
};

static struct bigint_uint64_init_test s_uint64_init_cases[] = {
    {
        .value = 0,
        .expected_hex_serialization = "0",
    },
    {
        .value = 1,
        .expected_hex_serialization = "1",
    },
    {
        .value = 128,
        .expected_hex_serialization = "80",
    },
    {
        .value = 255,
        .expected_hex_serialization = "ff",
    },
    {
        .value = UINT32_MAX,
        .expected_hex_serialization = "ffffffff",
    },
    {
        .value = (uint64_t)(UINT32_MAX) + 1,
        .expected_hex_serialization = "100000000",
    },
    {
        .value = UINT64_MAX,
        .expected_hex_serialization = "ffffffffffffffff",
    },
    {
        .value = 18364758544493064720ULL,
        .expected_hex_serialization = "fedcba9876543210",
    },
};

static int s_test_bigint_from_uint64(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_uint64_init_cases); ++i) {
        struct aws_byte_buf buffer;
        aws_byte_buf_init(&buffer, allocator, 1);

        struct bigint_uint64_init_test *testcase = &s_uint64_init_cases[i];
        size_t expected_length = strlen(testcase->expected_hex_serialization);

        struct aws_bigint *test = aws_bigint_new_from_uint64(allocator, testcase->value);
        ASSERT_NOT_NULL(test);
        ASSERT_TRUE(aws_bigint_is_positive(test) == (testcase->value > 0));
        ASSERT_FALSE(aws_bigint_is_negative(test));
        ASSERT_TRUE(aws_bigint_is_zero(test) == (testcase->value == 0));
        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(test, &buffer));
        ASSERT_TRUE(buffer.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(testcase->expected_hex_serialization, expected_length, buffer.buffer, buffer.len);

        aws_bigint_destroy(test);
        aws_byte_buf_clean_up(&buffer);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_from_uint64, s_test_bigint_from_uint64)

struct bigint_int64_init_test {
    int64_t value;
    const char *expected_hex_serialization;
};

static struct bigint_int64_init_test s_int64_init_cases[] = {
    {
        .value = 0,
        .expected_hex_serialization = "0",
    },
    {
        .value = 1,
        .expected_hex_serialization = "1",
    },
    {
        .value = -1,
        .expected_hex_serialization = "-1",
    },
    {
        .value = 128,
        .expected_hex_serialization = "80",
    },
    {
        .value = -128,
        .expected_hex_serialization = "-80",
    },
    {
        .value = 255,
        .expected_hex_serialization = "ff",
    },
    {
        .value = -255,
        .expected_hex_serialization = "-ff",
    },
    {
        .value = UINT32_MAX,
        .expected_hex_serialization = "ffffffff",
    },
    {
        .value = INT32_MAX,
        .expected_hex_serialization = "7fffffff",
    },
    {
        .value = INT32_MIN,
        .expected_hex_serialization = "-80000000",
    },
    {
        .value = (uint64_t)(UINT32_MAX) + 1,
        .expected_hex_serialization = "100000000",
    },
    {
        .value = INT64_MAX,
        .expected_hex_serialization = "7fffffffffffffff",
    },
    {
        .value = INT64_MIN,
        .expected_hex_serialization = "-8000000000000000",
    },
    {
        .value = 81985529216486895,
        .expected_hex_serialization = "123456789abcdef",
    },
};

static int s_test_bigint_from_int64(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_int64_init_cases); ++i) {
        struct aws_byte_buf buffer;
        aws_byte_buf_init(&buffer, allocator, 1);

        struct bigint_int64_init_test *testcase = &s_int64_init_cases[i];
        size_t expected_length = strlen(testcase->expected_hex_serialization);

        struct aws_bigint *test = aws_bigint_new_from_int64(allocator, testcase->value);
        ASSERT_NOT_NULL(test);
        ASSERT_TRUE(aws_bigint_is_positive(test) == (testcase->value > 0));
        ASSERT_TRUE(aws_bigint_is_negative(test) == (testcase->value < 0));
        ASSERT_TRUE(aws_bigint_is_zero(test) == (testcase->value == 0));
        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(test, &buffer));
        ASSERT_TRUE(buffer.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(testcase->expected_hex_serialization, expected_length, buffer.buffer, buffer.len);

        aws_bigint_destroy(test);
        aws_byte_buf_clean_up(&buffer);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_from_int64, s_test_bigint_from_int64)

struct bigint_string_init_success_test {
    const char *input_hex_value;
    const char *expected_hex_serialization;
    bool zero;
};

static struct bigint_string_init_success_test s_string_init_success_cases[] = {
    {
        .input_hex_value = "0",
        .expected_hex_serialization = "0",
        .zero = true,
    },
    {
        .input_hex_value = "0000000",
        .expected_hex_serialization = "0",
        .zero = true,
    },
    {
        .input_hex_value = "0x0000000",
        .expected_hex_serialization = "0",
        .zero = true,
    },
    {
        .input_hex_value = "0x00000001",
        .expected_hex_serialization = "1",
    },
    {
        .input_hex_value = "0x00000000000000000000000000000000000000000000000000000000000000001",
        .expected_hex_serialization = "1",
    },
    {
        .input_hex_value = "0x01000000000000000000000000000000000000000000000000000000000000001",
        .expected_hex_serialization = "1000000000000000000000000000000000000000000000000000000000000001",
    },
    {
        .input_hex_value = "0x07fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe",
        .expected_hex_serialization = "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe",
    },
    {
        .input_hex_value = "0x0abcdefABCDefabcdefabcdefabcdefabcdefabcdefabcdEFabcdefabcdefabcdefabcdEFAbcdef"
                           "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdef",
        .expected_hex_serialization = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdef"
                                      "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdef",
    },
    {
        .input_hex_value = "1234567890123456789012345678901234567890123456789012345678901234567890AbCFFDe",
        .expected_hex_serialization = "1234567890123456789012345678901234567890123456789012345678901234567890abcffde",
    },
};

static int s_test_bigint_from_hex_success(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_string_init_success_cases); ++i) {
        struct aws_byte_buf buffer;
        aws_byte_buf_init(&buffer, allocator, 1);

        struct bigint_string_init_success_test *testcase = &s_string_init_success_cases[i];
        size_t expected_length = strlen(testcase->expected_hex_serialization);

        struct aws_bigint *test =
            aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->input_hex_value));

        ASSERT_NOT_NULL(test);
        ASSERT_TRUE(aws_bigint_is_positive(test) == !testcase->zero);
        ASSERT_FALSE(aws_bigint_is_negative(test));
        ASSERT_TRUE(aws_bigint_is_zero(test) == testcase->zero);
        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(test, &buffer));
        ASSERT_TRUE(buffer.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(testcase->expected_hex_serialization, expected_length, buffer.buffer, buffer.len);

        aws_bigint_destroy(test);
        aws_byte_buf_clean_up(&buffer);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_from_hex_success, s_test_bigint_from_hex_success)

static const char *s_string_init_failure_cases[] = {
    "000000AFG",
    "xcde",
    "120xff",
    "#56",
    "-800", // debatable if we should allow negative prefix
    "0xx7f",
    "0000x00000",
};

static int s_test_bigint_from_hex_failure(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_string_init_failure_cases); ++i) {
        struct aws_byte_buf buffer;
        aws_byte_buf_init(&buffer, allocator, 1);

        const char *testcase = s_string_init_failure_cases[i];

        struct aws_bigint *test = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase));
        ASSERT_NULL(test);

        aws_byte_buf_clean_up(&buffer);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_from_hex_failure, s_test_bigint_from_hex_failure)

struct bigint_cursor_init_test {
    const char *input;
    const char *expected_hex_serialization;
};

static struct bigint_cursor_init_test s_cursor_cases[] = {
    {
        .input = "\x0a",
        .expected_hex_serialization = "a",
    },
    {
        .input = "\x01\x02\x03\x04",
        .expected_hex_serialization = "1020304",
    },
    {
        .input = "\xab\xcd\xef\x03\x01",
        .expected_hex_serialization = "abcdef0301",
    },
    {
        .input = "\xff\xda\x1f\x20\x01\xaa\x94\x37\xfe",
        .expected_hex_serialization = "ffda1f2001aa9437fe",
    },
};

static int s_test_bigint_from_cursor(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_cursor_cases); ++i) {
        struct aws_byte_buf buffer;
        aws_byte_buf_init(&buffer, allocator, 1);

        struct bigint_cursor_init_test *testcase = &s_cursor_cases[i];

        size_t expected_length = strlen(testcase->expected_hex_serialization);

        struct aws_bigint *test = aws_bigint_new_from_cursor(allocator, aws_byte_cursor_from_c_str(testcase->input));
        ASSERT_NOT_NULL(test);

        ASSERT_FALSE(aws_bigint_is_negative(test));

        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(test, &buffer));
        ASSERT_TRUE(buffer.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(testcase->expected_hex_serialization, expected_length, buffer.buffer, buffer.len);

        aws_bigint_destroy(test);
        aws_byte_buf_clean_up(&buffer);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_from_cursor, s_test_bigint_from_cursor)

struct bigint_comparison_test {
    const char *value1;
    const char *value2;
    bool is_negative1;
    bool is_negative2;
};

static struct bigint_comparison_test s_equality_cases[] = {
    {
        .value1 = "0",
        .value2 = "0x00000",
    },
    {
        .value1 = "0FF",
        .value2 = "0x00FF",
    },
    {
        .value1 = "A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .value2 = "000000000000A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
    },
    {
        .value1 = "A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .value2 = "000000000000A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .is_negative1 = true,
        .is_negative2 = true,
    },
};

static int s_test_bigint_equality(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_equality_cases); ++i) {
        struct bigint_comparison_test *testcase = &s_equality_cases[i];

        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);
        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        struct aws_bigint *value2 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value2));
        ASSERT_NOT_NULL(value2);
        if (testcase->is_negative2) {
            aws_bigint_negate(value2);
        }

        ASSERT_TRUE(aws_bigint_equals(value1, value2));
        ASSERT_TRUE(aws_bigint_equals(value2, value1));
        ASSERT_FALSE(aws_bigint_not_equals(value1, value2));
        ASSERT_FALSE(aws_bigint_not_equals(value2, value1));

        if (!aws_bigint_is_zero(value1)) {
            aws_bigint_negate(value1);

            ASSERT_FALSE(aws_bigint_equals(value1, value2));
            ASSERT_FALSE(aws_bigint_equals(value2, value1));

            aws_bigint_negate(value2);

            ASSERT_TRUE(aws_bigint_equals(value1, value2));
            ASSERT_TRUE(aws_bigint_equals(value2, value1));
        }

        aws_bigint_destroy(value2);
        aws_bigint_destroy(value1);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_equality, s_test_bigint_equality)

static struct bigint_comparison_test s_inequality_cases[] = {
    {
        .value1 = "0",
        .value2 = "0x00001",
    },
    {
        .value1 = "1",
        .value2 = "0x00001",
        .is_negative2 = true,
    },
    {
        .value1 = "0FF",
        .value2 = "0x00FF",
        .is_negative1 = true,
    },
    {
        .value1 = "0xabcdef987654321",
        .value2 = "accdef987654321",
        .is_negative1 = true,
        .is_negative2 = true,
    },
    {
        .value1 = "B9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .value2 = "000000000000A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
    },
    {
        .value1 = "FFFFFFFFFFFFFFFF",
        .value2 = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    },
};

static int s_test_bigint_inequality(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_inequality_cases); ++i) {
        struct bigint_comparison_test *testcase = &s_inequality_cases[i];

        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);
        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        struct aws_bigint *value2 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value2));
        ASSERT_NOT_NULL(value2);
        if (testcase->is_negative2) {
            aws_bigint_negate(value2);
        }

        ASSERT_FALSE(aws_bigint_equals(value1, value2));
        ASSERT_FALSE(aws_bigint_equals(value2, value1));
        ASSERT_TRUE(aws_bigint_not_equals(value1, value2));
        ASSERT_TRUE(aws_bigint_not_equals(value2, value1));

        aws_bigint_negate(value1);
        aws_bigint_negate(value2);

        ASSERT_FALSE(aws_bigint_equals(value1, value2));
        ASSERT_FALSE(aws_bigint_equals(value2, value1));
        ASSERT_TRUE(aws_bigint_not_equals(value1, value2));
        ASSERT_TRUE(aws_bigint_not_equals(value2, value1));

        aws_bigint_destroy(value2);
        aws_bigint_destroy(value1);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_inequality, s_test_bigint_inequality)

static struct bigint_comparison_test s_less_than_cases[] = {
    {
        .value1 = "0",
        .value2 = "0x00001",
    },
    {
        .value1 = "1",
        .value2 = "0x100000000000000000000000000000000001",
    },
    {
        .value1 = "0x00002",
        .value2 = "1",
        .is_negative1 = true,
    },
    {
        .value1 = "0FF",
        .value2 = "0x00FF",
        .is_negative1 = true,
    },
    {
        .value1 = "0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
        .value2 = "0x00FF",
        .is_negative1 = true,
        .is_negative2 = true,
    },
    {
        .value1 = "0xabcdef987654321",
        .value2 = "accdef987654321",
    },
    {
        .value1 = "000000000000A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .value2 = "B9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
    },
    {
        .value1 = "000000000000B9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .value2 = "A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .is_negative1 = true,
        .is_negative2 = true,
    },
    {
        .value1 = "FFFFFFFFFFFFFFFF",
        .value2 = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
    },
    {
        .value1 = "00000001FFFFFFFF",
        .value2 = "FFFFFFFF00000001",
    },
};

static int s_test_bigint_less_than(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_less_than_cases); ++i) {
        struct bigint_comparison_test *testcase = &s_less_than_cases[i];

        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);
        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        struct aws_bigint *value2 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value2));
        ASSERT_NOT_NULL(value2);
        if (testcase->is_negative2) {
            aws_bigint_negate(value2);
        }

        /* a < b */
        ASSERT_TRUE(aws_bigint_less_than(value1, value2));
        ASSERT_FALSE(aws_bigint_greater_than_or_equals(value1, value2));

        aws_bigint_negate(value1);
        aws_bigint_negate(value2);

        /* !(-a < -b) */
        ASSERT_FALSE(aws_bigint_less_than(value1, value2));
        ASSERT_TRUE(aws_bigint_greater_than_or_equals(value1, value2));

        aws_bigint_destroy(value2);
        aws_bigint_destroy(value1);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_less_than, s_test_bigint_less_than)

static struct bigint_comparison_test s_greater_than_cases[] = {
    {
        .value1 = "0x56",
        .value2 = "0x00001",
    },
    {
        .value1 = "0x56",
        .value2 = "0x00001",
        .is_negative2 = true,
    },
    {
        .value1 = "0x100000000000000000000000000000000001",
        .value2 = "1",
    },
    {
        .value1 = "0FF",
        .value2 = "0x00FFFF",
        .is_negative1 = true,
        .is_negative2 = true,
    },
    {
        .value1 = "0FF",
        .value2 = "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
        .is_negative1 = true,
        .is_negative2 = true,
    },
    {
        .value1 = "accdef987654321",
        .value2 = "0xabcdef987654321",
    },
    {
        .value1 = "B9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .value2 = "000000000000A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
    },
    {
        .value1 = "A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .value2 = "000000000000B9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4A9B8C7D6E5F4",
        .is_negative1 = true,
        .is_negative2 = true,

    },
    {
        .value1 = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
        .value2 = "FFFFFFFFFFFFFFFF",
    },
    {
        .value1 = "ABCDEF9800000002",
        .value2 = "1000000212345678",
    },
};

static int s_test_bigint_greater_than(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_greater_than_cases); ++i) {
        struct bigint_comparison_test *testcase = &s_greater_than_cases[i];

        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);
        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        struct aws_bigint *value2 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value2));
        ASSERT_NOT_NULL(value2);
        if (testcase->is_negative2) {
            aws_bigint_negate(value2);
        }

        /* a < b */
        ASSERT_TRUE(aws_bigint_greater_than(value1, value2));
        ASSERT_FALSE(aws_bigint_less_than_or_equals(value1, value2));

        aws_bigint_negate(value1);
        aws_bigint_negate(value2);

        /* !(-a < -b) */
        ASSERT_FALSE(aws_bigint_greater_than(value1, value2));
        ASSERT_TRUE(aws_bigint_less_than_or_equals(value1, value2));

        aws_bigint_destroy(value2);
        aws_bigint_destroy(value1);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_greater_than, s_test_bigint_greater_than)

struct bigint_arithmetic_test {
    const char *value1;
    const char *value2;
    const char *expected_result;
    bool is_negative1;
    bool is_negative2;
};

/*
 * Checks (val1 + val2), (val2 + val1) against expected result as a string
 * Checks (-val1 + -val2), (-val2 + -val1) against -(val1 + val2)
 */
static int s_do_addition_test(
    struct aws_allocator *allocator,
    struct bigint_arithmetic_test *test_cases,
    size_t test_case_count) {

    struct aws_byte_buf serialized_sum;
    aws_byte_buf_init(&serialized_sum, allocator, 0);

    for (size_t i = 0; i < test_case_count; ++i) {
        struct bigint_arithmetic_test *testcase = &test_cases[i];

        /* init operands */
        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);
        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        struct aws_bigint *value2 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value2));
        ASSERT_NOT_NULL(value2);
        if (testcase->is_negative2) {
            aws_bigint_negate(value2);
        }

        /* add and test val1 + val2 */
        struct aws_bigint *sum = aws_bigint_new_from_uint64(allocator, 0);

        ASSERT_SUCCESS(aws_bigint_add(sum, value1, value2));

        serialized_sum.len = 0;
        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(sum, &serialized_sum));

        size_t expected_length = strlen(testcase->expected_result);
        ASSERT_TRUE(serialized_sum.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(testcase->expected_result, expected_length, serialized_sum.buffer, serialized_sum.len);

        aws_bigint_destroy(sum);

        /* add and test val2 + val1 */
        sum = aws_bigint_new_from_uint64(allocator, 0);

        ASSERT_SUCCESS(aws_bigint_add(sum, value2, value1));

        serialized_sum.len = 0;
        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(sum, &serialized_sum));

        ASSERT_TRUE(serialized_sum.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(testcase->expected_result, expected_length, serialized_sum.buffer, serialized_sum.len);

        /* aliasing tests*/

        /* test val1 += val2 */
        struct aws_bigint *value1_copy = aws_bigint_new_from_copy(value1);

        ASSERT_SUCCESS(aws_bigint_add(value1_copy, value1_copy, value2));
        ASSERT_TRUE(aws_bigint_equals(value1_copy, sum));

        /* test val2 += val1 */
        struct aws_bigint *value2_copy = aws_bigint_new_from_copy(value2);

        ASSERT_SUCCESS(aws_bigint_add(value2_copy, value1, value2_copy));
        ASSERT_TRUE(aws_bigint_equals(value2_copy, sum));

        /* negation tests */
        struct aws_bigint *negated_sum = aws_bigint_new_from_copy(sum);
        aws_bigint_negate(negated_sum);

        aws_bigint_negate(value1);
        aws_bigint_negate(value2);

        /* add and test -val1 + -val2 */
        struct aws_bigint *sum_of_negations = aws_bigint_new_from_uint64(allocator, 0);

        ASSERT_SUCCESS(aws_bigint_add(sum_of_negations, value1, value2));
        ASSERT_TRUE(aws_bigint_equals(sum_of_negations, negated_sum));

        /* add and test -val2 + -val1 */
        aws_bigint_destroy(sum_of_negations);
        sum_of_negations = aws_bigint_new_from_uint64(allocator, 0);

        ASSERT_SUCCESS(aws_bigint_add(sum_of_negations, value2, value1));
        ASSERT_TRUE(aws_bigint_equals(sum_of_negations, negated_sum));

        aws_bigint_destroy(value1_copy);
        aws_bigint_destroy(value2_copy);
        aws_bigint_destroy(sum_of_negations);
        aws_bigint_destroy(negated_sum);
        aws_bigint_destroy(sum);
        aws_bigint_destroy(value2);
        aws_bigint_destroy(value1);
    }

    aws_byte_buf_clean_up(&serialized_sum);

    return AWS_OP_SUCCESS;
}

/* clang-format off */
static struct bigint_arithmetic_test s_add_zero_test_cases[] = {
    {
        .value1 =       "0x00",
        .value2 =          "0",
        .expected_result = "0",
    },
    {
        .value1 =         "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .value2 =                                                                      "0",
        .expected_result = "-ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .is_negative1 = true,
    },
    {
        .value1 =        "0xabcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef012",
        .value2 =                                                                                                  "0",
        .expected_result = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef012",
    },
};
/* clang-format on */

static int s_test_bigint_add_zero(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_addition_test(allocator, s_add_zero_test_cases, AWS_ARRAY_SIZE(s_add_zero_test_cases));
}

AWS_TEST_CASE(test_bigint_add_zero, s_test_bigint_add_zero)

/* clang-format off */
static struct bigint_arithmetic_test s_add_positive_test_cases[] = {
    {
        .value1 =       "0x01",
        .value2 =          "1",
        .expected_result = "2",
    },
    {
        .value1 =        "0x76543210765432107654321076543210765432107654321076543210",
        .value2 =          "3557799b3557799b3557799b3557799b3557799b3557799b3557799b",
        .expected_result = "abababababababababababababababababababababababababababab",
    },
    {
        .value1 =         "0xffffffff",
        .value2 =                  "1",
        .expected_result = "100000000",
    },
    {
        .value1 =         "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .value2 =                                                                      "1",
        .expected_result = "1000000000000000000000000000000000000000000000000000000000000",
    },
    {
        .value1 =         "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .value2 =                                                              "1FFFFFFFF",
        .expected_result = "10000000000000000000000000000000000000000000000000001fffffffe",
    },
    {
        .value1 =         "0x8000000080000000800000008000000080000000",
        .value2 =         "0x8000000080000000800000008000000080000000",
        .expected_result = "10000000100000001000000010000000100000000",
    },
};
/* clang-format on */

static int s_test_bigint_add_positive(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_addition_test(allocator, s_add_positive_test_cases, AWS_ARRAY_SIZE(s_add_positive_test_cases));
}

AWS_TEST_CASE(test_bigint_add_positive, s_test_bigint_add_positive)

/* clang-format off */
static struct bigint_arithmetic_test s_add_negative_test_cases[] = {
    {
        .value1 =        "0x01",
        .value2 =           "1",
        .expected_result = "-2",
        .is_negative1 = true,
        .is_negative2 = true,
    },
    {
        .value1 =          "0xfffffff0ffffffff",
        .value2 =                  "1100000001",
        .expected_result = "-10000000200000000",
        .is_negative1 = true,
        .is_negative2 = true,
    },
    {
        .value1 =         "0x11111111111111222222222222333333333344444444555555666677",
        .value2 =           "123456789abcde23456789abcd3456789abc456789ab56789a678978",
        .expected_result = "-23456789abcdef456789abcdef6789abcdef89abcdefabcdefcdefef",
        .is_negative1 = true,
        .is_negative2 = true,
    },
};
/* clang-format on */

static int s_test_bigint_add_negative(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_addition_test(allocator, s_add_negative_test_cases, AWS_ARRAY_SIZE(s_add_negative_test_cases));
}

AWS_TEST_CASE(test_bigint_add_negative, s_test_bigint_add_negative)

/* clang-format off */
static struct bigint_arithmetic_test s_add_mixed_test_cases[] = {
    {
        .value1 =       "0x01",
        .value2 =          "1",
        .expected_result = "0",
        .is_negative1 = true,
    },
    {
        .value1 = "0xabcdef0123456789abcdef0123456789abcdef0123456789",
        .value2 =   "abcdef0123456789abcdef0123456789abcdef0123456789",
        .expected_result =                                         "0",
        .is_negative2 = true,
    },
    {
        .value1 =          "1000000000000000000000000000000000000000000000000000000000000000000000000000",
        .value2 =           "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .expected_result =                                                                            "1",
        .is_negative2 = true,
    },
    {
        .value1 =          "1000000000000000000000000000000000000000000000000000000000000000000000000000",
        .value2 =                                                                                     "1",
        .expected_result =  "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .is_negative2 = true,
    },
    {
        .value1 =          "100000000000000000000000000000000000000000000000000000000000000000000000",
        .value2 =                                                                                 "1",
        .expected_result =  "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .is_negative2 = true,
    },
    {
        .value1 =          "9999999999999999999999999999999999999999999999999997",
        .value2 =          "9999999999999999999999999999999999999999999999999999",
        .expected_result =                                                    "2",
        .is_negative1 = true,
    },
    {
        .value1 =          "ddddddddddddddeeeeeeeeeeeeeeeffffffffffffffff",
        .value2 =          "0123456789abcd0123456789abcde0123456789abcdef",
        .expected_result = "dcba9876543210edcba9876543210fedcba9876543210",
        .is_negative2 = true,
    },
    {
        .value1 =          "10123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789a",
        .value2 =           "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        .expected_result =   "123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789b",
        .is_negative2 = true,
    },
    {
        .value1 =       "0x040",
        .value2 =          "42",
        .expected_result = "-2",
        .is_negative2 = true,
    },
};
/* clang-format on */

static int s_test_bigint_add_mixed_sign(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_addition_test(allocator, s_add_mixed_test_cases, AWS_ARRAY_SIZE(s_add_mixed_test_cases));
}

AWS_TEST_CASE(test_bigint_add_mixed_sign, s_test_bigint_add_mixed_sign)

/*
 * Checks (val1 - val2) against expected result as a string
 * Checks (val2 - val1), (-val1 - -val2), (-val2 - -val1) against +/-(val1 - val2)
 */
static int s_do_subtraction_test(
    struct aws_allocator *allocator,
    struct bigint_arithmetic_test *test_cases,
    size_t test_case_count) {

    struct aws_byte_buf serialized_diff;
    aws_byte_buf_init(&serialized_diff, allocator, 0);

    for (size_t i = 0; i < test_case_count; ++i) {
        struct bigint_arithmetic_test *testcase = &test_cases[i];

        /* init operands */
        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);
        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        struct aws_bigint *value2 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value2));
        ASSERT_NOT_NULL(value2);
        if (testcase->is_negative2) {
            aws_bigint_negate(value2);
        }

        /* test val1 - val2 */
        struct aws_bigint *diff = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(diff);

        ASSERT_SUCCESS(aws_bigint_subtract(diff, value1, value2));

        serialized_diff.len = 0;
        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(diff, &serialized_diff));

        size_t expected_length = strlen(testcase->expected_result);
        ASSERT_TRUE(serialized_diff.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(
            testcase->expected_result, expected_length, serialized_diff.buffer, serialized_diff.len);

        struct aws_bigint *negated_diff = aws_bigint_new_from_copy(diff);
        ASSERT_NOT_NULL(negated_diff);

        aws_bigint_negate(negated_diff);

        /* test val2 - val1 */
        struct aws_bigint *result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_subtract(result, value2, value1));

        ASSERT_TRUE(aws_bigint_equals(result, negated_diff));

        /* aliasing tests*/

        /* test val1 -= val2 */
        struct aws_bigint *value1_copy = aws_bigint_new_from_copy(value1);
        ASSERT_NOT_NULL(value1_copy);

        ASSERT_SUCCESS(aws_bigint_subtract(value1_copy, value1_copy, value2));
        ASSERT_TRUE(aws_bigint_equals(value1_copy, diff));

        /* test val2 = val1 - val2 */
        struct aws_bigint *value2_copy = aws_bigint_new_from_copy(value2);
        ASSERT_NOT_NULL(value2_copy);

        ASSERT_SUCCESS(aws_bigint_subtract(value2_copy, value1, value2_copy));
        ASSERT_TRUE(aws_bigint_equals(value2_copy, diff));

        /* negation tests */
        aws_bigint_negate(value1);
        aws_bigint_negate(value2);

        /* test -val1 - -val2 */
        aws_bigint_destroy(result);
        result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_subtract(result, value1, value2));
        ASSERT_TRUE(aws_bigint_equals(result, negated_diff));

        /* test -val2 - -val1 */
        aws_bigint_destroy(result);
        result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_subtract(result, value2, value1));
        ASSERT_TRUE(aws_bigint_equals(result, diff));

        aws_bigint_destroy(value1_copy);
        aws_bigint_destroy(value2_copy);
        aws_bigint_destroy(result);
        aws_bigint_destroy(negated_diff);
        aws_bigint_destroy(diff);
        aws_bigint_destroy(value2);
        aws_bigint_destroy(value1);
    }

    aws_byte_buf_clean_up(&serialized_diff);

    return AWS_OP_SUCCESS;
}

/* clang-format off */
static struct bigint_arithmetic_test s_subtract_zero_test_cases[] = {
    {
        .value1 =       "0x00",
        .value2 =          "0",
        .expected_result = "0",
    },
    {
        .value1 =         "0x111122223333445566789aaaaabbbbbbcccccddddddeeeeef",
        .value2 =                                                           "0",
        .expected_result = "-111122223333445566789aaaaabbbbbbcccccddddddeeeeef",
        .is_negative1 = true,
    },
    {
        .value1 =        "0xabcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef012",
        .value2 =                                                                                                  "0",
        .expected_result = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef012",
    },
};
/* clang-format on */

static int s_test_bigint_subtract_zero(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_subtraction_test(allocator, s_subtract_zero_test_cases, AWS_ARRAY_SIZE(s_subtract_zero_test_cases));
}

AWS_TEST_CASE(test_bigint_subtract_zero, s_test_bigint_subtract_zero)

/* clang-format off */
static struct bigint_arithmetic_test s_subtract_positive_result_test_cases[] = {
    {
        .value1 =       "0x06",
        .value2 =          "1",
        .expected_result = "5",
    },
    {
        .value1 =       "0x01",
        .value2 =          "6",
        .expected_result = "7",
        .is_negative2 = true,
    },
    {
        .value1 =       "0x01",
        .value2 =          "6",
        .expected_result = "5",
        .is_negative1 = true,
        .is_negative2 = true,
    },
    {
        .value1 =        "0x345634563456789876543456789",
        .value2 =          "111111112222222333333332222",
        .expected_result = "234523451234567543210124567",
    },
    {
        .value1 =        "0x111111111111111111111111111111111111111111111111111111111111111",
        .value2 =           "23456789123456789123456789123456789123456789123456789123456789",
        .expected_result =  "edcba987fedcba987fedcba987fedcba987fedcba987fedcba987fedcba988",
    },
    {
        .value1 =        "0x10000000000000000000000000000000000000000000000000000000000000000",
        .value2 =                                                                          "1",
        .expected_result =  "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
    },
};
/* clang-format on */

static int s_test_bigint_subtract_positive_result(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_subtraction_test(
        allocator, s_subtract_positive_result_test_cases, AWS_ARRAY_SIZE(s_subtract_positive_result_test_cases));
}

AWS_TEST_CASE(test_bigint_subtract_positive_result, s_test_bigint_subtract_positive_result)

/* clang-format off */
static struct bigint_arithmetic_test s_subtract_negative_result_test_cases[] = {
    {
        .value1 =                "0x00",
        .value2 =           "fffffffff",
        .expected_result = "-fffffffff",
    },
    {
        .value1 =         "0xaaaaaaaaaaa",
        .value2 =           "bbbbbbbbbbb",
        .expected_result = "-11111111111",
    },
    {
        .value1 =         "0x123123123123123",
        .value2 =           "321321321321321",
        .expected_result = "-444444444444444",
        .is_negative1 = true,
    },
    {
        .value1 =         "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        .value2 =           "5454545454545454545454545454545",
        .expected_result = "-5656565656565656565656565656565",
        .is_negative1 = true,
        .is_negative2 = true,
    },
};
/* clang-format on */

static int s_test_bigint_subtract_negative_result(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_subtraction_test(
        allocator, s_subtract_negative_result_test_cases, AWS_ARRAY_SIZE(s_subtract_negative_result_test_cases));
}

AWS_TEST_CASE(test_bigint_subtract_negative_result, s_test_bigint_subtract_negative_result)

/*
 * Tests (val1 x val2 ) against expected result
 * Tests (-val1 x val2), (val1 x -val2), (-val1 x -val2) against +/-(val1 x val2)
 * Tests (val2 x val1), (-val2 x val1), (val2 x -val1), (-val2 x -val1) against +/-(val1 x val2)
 * Tests aliased multiplication
 */
static int s_do_multiplication_test(
    struct aws_allocator *allocator,
    struct bigint_arithmetic_test *test_cases,
    size_t test_case_count) {

    struct aws_byte_buf serialized_product;
    aws_byte_buf_init(&serialized_product, allocator, 0);

    for (size_t i = 0; i < test_case_count; ++i) {
        struct bigint_arithmetic_test *testcase = &test_cases[i];

        /* init operands */
        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);
        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        struct aws_bigint *value2 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value2));
        ASSERT_NOT_NULL(value2);
        if (testcase->is_negative2) {
            aws_bigint_negate(value2);
        }

        /* test val1 x val2 */
        struct aws_bigint *product = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(product);

        ASSERT_SUCCESS(aws_bigint_multiply(product, value1, value2));

        serialized_product.len = 0;
        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(product, &serialized_product));

        size_t expected_length = strlen(testcase->expected_result);
        ASSERT_TRUE(serialized_product.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(
            testcase->expected_result, expected_length, serialized_product.buffer, serialized_product.len);

        struct aws_bigint *negated_product = aws_bigint_new_from_copy(product);
        aws_bigint_negate(negated_product);

        /* test val2 x val1 */
        struct aws_bigint *result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_multiply(result, value2, value1));
        ASSERT_TRUE(aws_bigint_equals(result, product));

        /* aliasing tests*/

        /* test val1 *= val2 */
        struct aws_bigint *value1_copy = aws_bigint_new_from_copy(value1);
        ASSERT_NOT_NULL(value1_copy);

        ASSERT_SUCCESS(aws_bigint_multiply(value1_copy, value1_copy, value2));
        ASSERT_TRUE(aws_bigint_equals(value1_copy, product));

        /* test val2 *= val1 */
        struct aws_bigint *value2_copy = aws_bigint_new_from_copy(value2);
        ASSERT_NOT_NULL(value2_copy);

        ASSERT_SUCCESS(aws_bigint_multiply(value2_copy, value1, value2_copy));
        ASSERT_TRUE(aws_bigint_equals(value2_copy, product));

        /* negation tests */
        aws_bigint_negate(value1);

        /* test -val1 x val2 */
        aws_bigint_destroy(result);
        result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_multiply(result, value1, value2));
        ASSERT_TRUE(aws_bigint_equals(result, negated_product));

        /* test val2 x -val1 */
        aws_bigint_destroy(result);
        result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_multiply(result, value2, value1));
        ASSERT_TRUE(aws_bigint_equals(result, negated_product));

        aws_bigint_negate(value2);

        /* test -val1 x -val2 */
        aws_bigint_destroy(result);
        result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_multiply(result, value1, value2));
        ASSERT_TRUE(aws_bigint_equals(result, product));

        /* test -val2 x -val1 */
        aws_bigint_destroy(result);
        result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_multiply(result, value2, value1));
        ASSERT_TRUE(aws_bigint_equals(result, product));

        aws_bigint_negate(value1);

        /* test val1 x -val2 */
        aws_bigint_destroy(result);
        result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_multiply(result, value1, value2));
        ASSERT_TRUE(aws_bigint_equals(result, negated_product));

        /* test -val2 x val1 */
        aws_bigint_destroy(result);
        result = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(result);

        ASSERT_SUCCESS(aws_bigint_multiply(result, value2, value1));
        ASSERT_TRUE(aws_bigint_equals(result, negated_product));

        aws_bigint_destroy(value1_copy);
        aws_bigint_destroy(value2_copy);
        aws_bigint_destroy(result);
        aws_bigint_destroy(negated_product);
        aws_bigint_destroy(product);
        aws_bigint_destroy(value2);
        aws_bigint_destroy(value1);
    }

    aws_byte_buf_clean_up(&serialized_product);

    return AWS_OP_SUCCESS;
}

static struct bigint_arithmetic_test s_multiply_one_and_zero_test_cases[] = {
    {
        .value1 = "0x00",
        .value2 = "0",
        .expected_result = "0",
    },
    {
        .value1 = "0x00",
        .value2 = "15",
        .expected_result = "0",
    },
    {
        .value1 = "19578923468972567982384578923547abcdeffffffffffffffffffff",
        .value2 = "0x00",
        .expected_result = "0",
    },
    {
        .value1 = "0x01",
        .value2 = "1",
        .expected_result = "1",
    },
    {
        .value1 = "0x0123457698badceffedbc467825354298badceffedbc4678253542",
        .value2 = "1",
        .expected_result = "123457698badceffedbc467825354298badceffedbc4678253542",
    },
    {
        .value1 = "0x5278967893465879032467094302895678ababdf5789345795",
        .value2 = "1",
        .expected_result = "5278967893465879032467094302895678ababdf5789345795",
        .is_negative1 = true,
        .is_negative2 = true,
    },
};

static int s_test_bigint_multiply_one_and_zero(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_multiplication_test(
        allocator, s_multiply_one_and_zero_test_cases, AWS_ARRAY_SIZE(s_multiply_one_and_zero_test_cases));
}

AWS_TEST_CASE(test_bigint_multiply_one_and_zero, s_test_bigint_multiply_one_and_zero)

static struct bigint_arithmetic_test s_multiply_test_cases[] = {
    {
        .value1 = "0x02",
        .value2 = "2",
        .expected_result = "4",
    },
    {
        .value1 = "0x02",
        .value2 = "80000000",
        .expected_result = "100000000",
    },
    {
        .value1 = "ffffffff",
        .value2 = "ffffffff",
        .expected_result = "fffffffe00000001",
    },
    {
        .value1 = "ffffffffffffffff",
        .value2 = "ffffffffffffffff",
        .expected_result = "fffffffffffffffe0000000000000001",
    },
    {
        .value1 = "ffffffffffffffffffffffff",
        .value2 = "ffffffffffffffffffffffff",
        .expected_result = "fffffffffffffffffffffffe000000000000000000000001",
    },
    {
        .value1 = "789abcdef789abcdef789abcdef789abcdef789abcdef789abcdef",
        .value2 = "1234565432100000000000000000000000000000056af563",
        .expected_result =
            "8938961b08098ec33d7098ec33d7098ec33d7099150a6cde2acd04b1aa16b54ba49c09c7ca49c09c7ca49c09c7ca4997c5e6d",
    },
};

static int s_test_bigint_multiply(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_multiplication_test(allocator, s_multiply_test_cases, AWS_ARRAY_SIZE(s_multiply_test_cases));
}

AWS_TEST_CASE(test_bigint_multiply, s_test_bigint_multiply)

struct aws_bigint_shift_test {
    const char *value1;
    const char *expected_result;
    size_t shift_amount;
    bool is_negative1;
};

static int s_do_right_shift_test(
    struct aws_allocator *allocator,
    struct aws_bigint_shift_test *test_cases,
    size_t test_case_count) {

    struct aws_byte_buf serialized_shift;
    aws_byte_buf_init(&serialized_shift, allocator, 0);

    for (size_t i = 0; i < test_case_count; ++i) {
        struct aws_bigint_shift_test *testcase = &test_cases[i];

        /* init operands */
        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);
        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        aws_bigint_shift_right(value1, testcase->shift_amount);

        serialized_shift.len = 0;
        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(value1, &serialized_shift));

        size_t expected_length = strlen(testcase->expected_result);
        ASSERT_TRUE(serialized_shift.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(
            testcase->expected_result, expected_length, serialized_shift.buffer, serialized_shift.len);

        aws_bigint_destroy(value1);
    }

    aws_byte_buf_clean_up(&serialized_shift);

    return AWS_OP_SUCCESS;
}

static struct aws_bigint_shift_test s_shift_right_test_cases[] = {
    {
        .value1 = "0x00",
        .expected_result = "0",
        .shift_amount = 0,
    },
    {
        .value1 = "0xFF",
        .expected_result = "ff",
        .shift_amount = 0,
    },
    {
        .value1 = "0xfedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210",
        .expected_result = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210",
        .shift_amount = 0,
    },
    {
        .value1 = "0x02",
        .expected_result = "1",
        .shift_amount = 1,
    },
    {
        .value1 = "0x7f7f7f7f",
        .expected_result = "3fbfbfbf",
        .shift_amount = 1,
    },
    {
        .value1 = "0x7f7f7f7f",
        .expected_result = "7f7f7f7",
        .shift_amount = 4,
    },
    {
        .value1 = "0x7f7f7f7f",
        .expected_result = "7f7f7",
        .shift_amount = 12,
    },
    {
        .value1 = "0x7f7f7f7f",
        .expected_result = "7",
        .shift_amount = 28,
    },
    {
        .value1 = "0x7f7f7f7f",
        .expected_result = "1",
        .shift_amount = 30,
    },
    {
        .value1 = "0x7f7f7f7f",
        .expected_result = "0",
        .shift_amount = 31,
    },
    {
        .value1 = "0x7f7f7f7f",
        .expected_result = "0",
        .shift_amount = 32,
    },
    {
        .value1 = "0x7f7f7f7f",
        .expected_result = "0",
        .shift_amount = 128,
    },
    {
        .value1 = "0x7f7f7f7f",
        .expected_result = "0",
        .shift_amount = 65537,
    },
    {
        .value1 = "0x842108421084210",
        .expected_result = "421084210842108",
        .shift_amount = 1,
    },
    {
        .value1 = "0x842108421084210",
        .expected_result = "210842108421084",
        .shift_amount = 2,
    },
    {
        .value1 = "0x842108421084210",
        .expected_result = "108421084210842",
        .shift_amount = 3,
    },
    {
        .value1 = "0x842108421084210",
        .expected_result = "10842108",
        .shift_amount = 31,
    },
    {
        .value1 = "0x842108421084210",
        .expected_result = "8421084",
        .shift_amount = 32,
    },
    {
        .value1 = "0x842108421084210",
        .expected_result = "4210842",
        .shift_amount = 33,
    },
    {
        .value1 = "0x842108421084210",
        .expected_result = "2108421",
        .shift_amount = 34,
    },
    {
        .value1 = "0x842108421084210842108421",
        .expected_result = "8421084210842108",
        .shift_amount = 32,
    },
    {
        .value1 = "0x842108421084210842108421",
        .expected_result = "84210842",
        .shift_amount = 64,
    },
    {
        .value1 = "0x842108421084210842108421",
        .expected_result = "42108421",
        .shift_amount = 65,
    },
    {
        .value1 = "0x842108421084210842108421",
        .expected_result = "21084210",
        .shift_amount = 66,
    },
    {
        .value1 = "0x842108421084210842108421",
        .expected_result = "10842108",
        .shift_amount = 67,
    },
};

static int s_test_bigint_right_shift(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_right_shift_test(allocator, s_shift_right_test_cases, AWS_ARRAY_SIZE(s_shift_right_test_cases));
}

AWS_TEST_CASE(test_bigint_right_shift, s_test_bigint_right_shift)

static int s_do_left_shift_test(
    struct aws_allocator *allocator,
    struct aws_bigint_shift_test *test_cases,
    size_t test_case_count) {

    struct aws_byte_buf serialized_shift;
    aws_byte_buf_init(&serialized_shift, allocator, 0);

    for (size_t i = 0; i < test_case_count; ++i) {
        struct aws_bigint_shift_test *testcase = &test_cases[i];

        /* init operands */
        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);

        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        aws_bigint_shift_left(value1, testcase->shift_amount);

        serialized_shift.len = 0;
        ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(value1, &serialized_shift));

        size_t expected_length = strlen(testcase->expected_result);
        ASSERT_TRUE(serialized_shift.len == expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(
            testcase->expected_result, expected_length, serialized_shift.buffer, serialized_shift.len);

        aws_bigint_destroy(value1);
    }

    aws_byte_buf_clean_up(&serialized_shift);

    return AWS_OP_SUCCESS;
}

static struct aws_bigint_shift_test s_shift_left_test_cases[] = {
    {
        .value1 = "0x00",
        .expected_result = "0",
        .shift_amount = 0,
    },
    {
        .value1 = "0x1F",
        .expected_result = "1f",
        .shift_amount = 0,
    },
    {
        .value1 = "0xfedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210",
        .expected_result = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210",
        .shift_amount = 0,
    },
    {
        .value1 = "0x01",
        .expected_result = "2",
        .shift_amount = 1,
    },
    {
        .value1 = "0x01",
        .expected_result = "80000000",
        .shift_amount = 31,
    },
    {
        .value1 = "0x01",
        .expected_result = "10000000000000000",
        .shift_amount = 64,
    },
    {
        .value1 = "0x01",
        .expected_result = "20000000000000000",
        .shift_amount = 65,
    },
    {
        .value1 = "0x84210842108421084210",
        .expected_result = "108421084210842108420",
        .shift_amount = 1,
    },
    {
        .value1 = "0x84210842108421084210",
        .expected_result = "210842108421084210840",
        .shift_amount = 2,
    },
    {
        .value1 = "0x84210842108421084210",
        .expected_result = "4210842108421084210800000000",
        .shift_amount = 31,
    },
    {
        .value1 = "0x84210842108421084210",
        .expected_result = "8421084210842108421000000000",
        .shift_amount = 32,
    },
    {
        .value1 = "0x84210842108421084210",
        .expected_result = "842108421084210842100000000000000000",
        .shift_amount = 64,
    },
    {
        .value1 = "0x84210842108421084210",
        .expected_result = "84210842108421084210000000000000000000000000",
        .shift_amount = 96,
    },
    {
        .value1 = "0x84210842108421084210",
        .expected_result = "108421084210842108420000000000000000000000000",
        .shift_amount = 97,
    },
    {
        .value1 = "0x84210842108421084210",
        .expected_result = "4210842108421084210800000000000000000000000000000000",
        .shift_amount = 127,
    },
};

static int s_test_bigint_left_shift(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_left_shift_test(allocator, s_shift_left_test_cases, AWS_ARRAY_SIZE(s_shift_left_test_cases));
}

AWS_TEST_CASE(test_bigint_left_shift, s_test_bigint_left_shift)

struct aws_bigint_divide_test {
    const char *value1;
    const char *value2;
    const char *expected_quotient;
    const char *expected_remainder;
    int expected_error;
    bool is_negative1;
    bool is_negative2;
};

static int s_do_divide_test(
    struct aws_allocator *allocator,
    struct aws_bigint_divide_test *test_cases,
    size_t test_case_count) {

    struct aws_byte_buf serialized_quotient;
    aws_byte_buf_init(&serialized_quotient, allocator, 0);

    struct aws_byte_buf serialized_remainder;
    aws_byte_buf_init(&serialized_remainder, allocator, 0);

    for (size_t i = 0; i < test_case_count; ++i) {
        struct aws_bigint_divide_test *testcase = &test_cases[i];

        /* init operands */
        struct aws_bigint *value1 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value1));
        ASSERT_NOT_NULL(value1);
        if (testcase->is_negative1) {
            aws_bigint_negate(value1);
        }

        struct aws_bigint *value2 = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->value2));
        ASSERT_NOT_NULL(value2);
        if (testcase->is_negative2) {
            aws_bigint_negate(value2);
        }

        struct aws_bigint *quotient = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(quotient);

        struct aws_bigint *remainder = aws_bigint_new_from_uint64(allocator, 0);
        ASSERT_NOT_NULL(remainder);

        int result = aws_bigint_divide(quotient, remainder, value1, value2);
        if (testcase->expected_error > 0) {
            ASSERT_FAILS(result);
            ASSERT_TRUE(aws_last_error() == testcase->expected_error);
        } else {
            ASSERT_SUCCESS(result);

            /* check quotient */
            serialized_quotient.len = 0;
            ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(quotient, &serialized_quotient));

            size_t expected_length = strlen(testcase->expected_quotient);
            ASSERT_TRUE(serialized_quotient.len == expected_length);
            ASSERT_BIN_ARRAYS_EQUALS(
                testcase->expected_quotient, expected_length, serialized_quotient.buffer, serialized_quotient.len);

            /* check remainder */
            serialized_remainder.len = 0;
            ASSERT_SUCCESS(aws_bigint_bytebuf_debug_output(remainder, &serialized_remainder));

            expected_length = strlen(testcase->expected_remainder);
            ASSERT_TRUE(serialized_remainder.len == expected_length);
            ASSERT_BIN_ARRAYS_EQUALS(
                testcase->expected_remainder, expected_length, serialized_remainder.buffer, serialized_remainder.len);
        }

        aws_bigint_destroy(value1);
        aws_bigint_destroy(value2);
        aws_bigint_destroy(quotient);
        aws_bigint_destroy(remainder);
    }

    aws_byte_buf_clean_up(&serialized_quotient);
    aws_byte_buf_clean_up(&serialized_remainder);

    return AWS_OP_SUCCESS;
}

/* verifies behavior for a variety of error cases within the divide function */
static struct aws_bigint_divide_test s_divide_error_test_cases[] = {
    {
        .value1 = "0x00",
        .value2 = "0",
        .expected_error = AWS_ERROR_DIVIDE_BY_ZERO,
    },
    {
        .value1 = "0x01",
        .value2 = "1",
        .expected_error = AWS_ERROR_INVALID_ARGUMENT,
        .is_negative1 = true,
    },
    {
        .value1 = "0x0a",
        .value2 = "ffffffffffffffffffffffffffffffffffffff",
        .expected_error = AWS_ERROR_INVALID_ARGUMENT,
        .is_negative2 = true,
    },
};

static int s_test_bigint_divide_error(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_divide_test(allocator, s_divide_error_test_cases, AWS_ARRAY_SIZE(s_divide_error_test_cases));
}

AWS_TEST_CASE(test_bigint_divide_error, s_test_bigint_divide_error)

/* verifies behavior for a variety of edge cases within the divide function */
static struct aws_bigint_divide_test s_divide_edge_test_cases[] = {
    {
        .value1 = "0x00",
        .value2 = "fffffffffffffffffffeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeffffffffffffffffffffffffffffffffffff",
        .expected_quotient = "0",
        .expected_remainder = "0",
    },
    {
        .value1 = "0xab",
        .value2 = "cccccccccccccccccccccccccccccccccccccccccccc",
        .expected_quotient = "0",
        .expected_remainder = "ab",
    },
    {
        .value1 = "0xcccccccccccccccccccccccccccccccccccccccccccb",
        .value2 = "cccccccccccccccccccccccccccccccccccccccccccc",
        .expected_quotient = "0",
        .expected_remainder = "cccccccccccccccccccccccccccccccccccccccccccb",
    },
};

static int s_test_bigint_divide_edge(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_divide_test(allocator, s_divide_edge_test_cases, AWS_ARRAY_SIZE(s_divide_edge_test_cases));
}

AWS_TEST_CASE(test_bigint_divide_edge, s_test_bigint_divide_edge)

/*
 * Single-digit divisors are a special case of our divide implementation (primarily because the general
 * algorithm requires at least a two digit divisor to work properly), so we test them separately.
 */
static struct aws_bigint_divide_test s_divide_single_digit_divisor_test_cases[] = {
    {
        .value1 = "0x00",
        .value2 = "1",
        .expected_quotient = "0",
        .expected_remainder = "0",
    },
    {
        .value1 = "0xff",
        .value2 = "1",
        .expected_quotient = "ff",
        .expected_remainder = "0",
    },
    {
        .value1 = "0x1034780fab4289fca96da5e3bae201",
        .value2 = "1",
        .expected_quotient = "1034780fab4289fca96da5e3bae201",
        .expected_remainder = "0",
    },
    {
        .value1 = "0x10000000000000000000000000000000000000000000000000000001",
        .value2 = "2",
        .expected_quotient = "8000000000000000000000000000000000000000000000000000000",
        .expected_remainder = "1",
    },
    {
        .value1 = "0x1034780fab4289fca96da5e3bae20e",
        .value2 = "10",
        .expected_quotient = "1034780fab4289fca96da5e3bae20",
        .expected_remainder = "e",
    },
    {
        .value1 = "25c50e8de2be44d8aecf6e4b90606bbdb49",
        .value2 = "5195e5",
        .expected_quotient = "7683ad81ecc4b5e95f9e1557a5354",
        .expected_remainder = "3b6d25",
    },
};

static int s_test_bigint_divide_single_digit_divisor(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_divide_test(
        allocator, s_divide_single_digit_divisor_test_cases, AWS_ARRAY_SIZE(s_divide_single_digit_divisor_test_cases));
}

AWS_TEST_CASE(test_bigint_divide_single_digit_divisor, s_test_bigint_divide_single_digit_divisor)

/*
 * General divide testing - requires at least a two digit divisor
 */
static struct aws_bigint_divide_test s_divide_general_test_cases[] = {
    {
        .value1 = "0x100000000",
        .value2 = "100000000",
        .expected_quotient = "1",
        .expected_remainder = "0",
    },
    {
        .value1 = "0x200000000",
        .value2 = "100000000",
        .expected_quotient = "2",
        .expected_remainder = "0",
    },
    {
        .value1 = "0xa00000001",
        .value2 = "100000000",
        .expected_quotient = "a",
        .expected_remainder = "1",
    },
    {
        .value1 = "0xa0000000100000001",
        .value2 = "100000000",
        .expected_quotient = "a00000001",
        .expected_remainder = "1",
    },
    {
        .value1 = "0x555555555555555556",
        .value2 = "111111111",
        .expected_quotient = "5000000005",
        .expected_remainder = "1",
    },
    {
        /* This is a test case where the q_guess calculation gives an overestimate */
        .value1 = "0x70000000eeeeeeee00000000",
        .value2 = "80000000ffffffff",
        .expected_quotient = "e0000000",
        .expected_remainder = "eeeeeeee0000000",
    },
    {
        /*
         * This is a test case where the q_guess calculation ends up being off by one, leading to
         * a borrow during the subtract step, which in turn forces us to do the add-back step.
         *
         * The numbers were an educated guess based on the Base 2^16 test case that be found in the
         * solution to exercise 22 at the end of AoCP 4.3.1.
         */
        .value1 = "0x7fffffff800000010000000000000000",
        .value2 = "800000008000000200000005",
        .expected_quotient = "fffffffd",
        .expected_remainder = "80000000800000010000000f",
    },
    {
        .value1 = "4798235789a34fb324c004725beef89672538932278979abc468dd6fb4c90a",
        .value2 = "956789fbba44de8d28bcc73985df2a8b99cd253737bda",
        .expected_quotient = "7aacb5a90d6a42294",
        .expected_remainder = "7b7406e383c6b7466ef3a1325759acc3820c46d63b02",
    },
    {
        .value1 = "a9442278e660dac9a076cdd4163c251de2034e6f3c4ad9923cd0aa23d17170cd7412af2a6b7341124b973ec605f416ad6ef9"
                  "d8cb75b553f2a",
        .value2 = "124abcd719e0828465275fd0f855d46287142a1961be5dab0332785874d",
        .expected_quotient = "940eb4747e656dd3f0c2679dca69c64baf7ea6fbf65eef46ae6cf0f",
        .expected_remainder = "90fa7757fd0154ceb2ad96c4f315bd43fca4643d6583918561a0ed0ea7",
    },
};

static int s_test_bigint_divide_general(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_do_divide_test(allocator, s_divide_general_test_cases, AWS_ARRAY_SIZE(s_divide_general_test_cases));
}

AWS_TEST_CASE(test_bigint_divide_general, s_test_bigint_divide_general)

struct bigint_append_binary_test {
    const char *input_hex;
    size_t minimum_length;
    const char *expected_binary_data;
    size_t expected_length;
};

static struct bigint_append_binary_test s_append_binary_cases[] = {
    {
        .input_hex = "0x0",
        .minimum_length = 0,
        .expected_binary_data = "\x00",
        .expected_length = 1,
    },
    {
        .input_hex = "0xff",
        .minimum_length = 0,
        .expected_binary_data = "\xFF",
        .expected_length = 1,
    },
    {
        .input_hex = "0x3aff",
        .minimum_length = 0,
        .expected_binary_data = "\x3a\xFF",
        .expected_length = 2,
    },
    {
        .input_hex = "0x3a78ff3483637",
        .minimum_length = 0,
        .expected_binary_data = "\x03\xa7\x8f\xf3\x48\x36\x37",
        .expected_length = 7,
    },
    {
        .input_hex = "fd3758b8a20010baa583fde3e7bb8532f4abd",
        .minimum_length = 0,
        .expected_binary_data = "\x0f\xd3\x75\x8b\x8a\x20\x01\x0b\xaa\x58\x3f\xde\x3e\x7b\xb8\x53\x2f\x4a\xbd",
        .expected_length = 19,
    },
    {
        .input_hex = "0xff",
        .minimum_length = 5,
        .expected_binary_data = "\x00\x00\x00\x00\xFF",
        .expected_length = 5,
    },
    {
        .input_hex = "0xffaabb88",
        .minimum_length = 5,
        .expected_binary_data = "\x00\xff\xaa\xbb\x88",
        .expected_length = 5,
    },
    {
        .input_hex = "940eb4747e656dd3f0c2679dca69c64baf7ea",
        .minimum_length = 32,
        .expected_binary_data = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x09\x40\xeb\x47\x47\xe6\x56\xdd"
                                "\x3f\x0c\x26\x79\xdc\xa6\x9c\x64\xba\xf7\xea",
        .expected_length = 32,
    },
    {
        .input_hex = "10000000000000000000000000000000000000001",
        .minimum_length = 32,
        .expected_binary_data = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                                "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
        .expected_length = 32,
    },
};

static int s_test_bigint_append_binary(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    for (size_t i = 0; i < AWS_ARRAY_SIZE(s_append_binary_cases); ++i) {
        struct aws_byte_buf buffer;
        aws_byte_buf_init(&buffer, allocator, 1);

        struct bigint_append_binary_test *testcase = &s_append_binary_cases[i];

        struct aws_bigint *test = aws_bigint_new_from_hex(allocator, aws_byte_cursor_from_c_str(testcase->input_hex));
        ASSERT_NOT_NULL(test);

        ASSERT_SUCCESS(aws_bigint_bytebuf_append_as_big_endian(test, &buffer, testcase->minimum_length));
        ASSERT_TRUE(buffer.len == testcase->expected_length);
        ASSERT_BIN_ARRAYS_EQUALS(testcase->expected_binary_data, testcase->expected_length, buffer.buffer, buffer.len);

        aws_bigint_destroy(test);
        aws_byte_buf_clean_up(&buffer);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(test_bigint_append_binary, s_test_bigint_append_binary)
