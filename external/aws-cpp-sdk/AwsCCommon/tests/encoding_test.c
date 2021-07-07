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

#include <aws/common/encoding.h>

#include <aws/common/string.h>

#include <aws/testing/aws_test_harness.h>

/* Test cases from rfc4648 for Base 16 Encoding */

static int s_run_hex_encoding_test_case(
    struct aws_allocator *allocator,
    const char *test_str,
    size_t test_str_size,
    const char *expected,
    size_t expected_size) {
    size_t output_size = 0;

    ASSERT_SUCCESS(
        aws_hex_compute_encoded_len(test_str_size - 1, &output_size),
        "compute hex encoded len failed with error %d",
        aws_last_error());
    ASSERT_INT_EQUALS(expected_size, output_size, "Output size on string should be %d", expected_size);

    struct aws_byte_cursor to_encode = aws_byte_cursor_from_array(test_str, test_str_size - 1);

    struct aws_byte_buf allocation;
    ASSERT_SUCCESS(aws_byte_buf_init(&allocation, allocator, output_size + 2));
    memset(allocation.buffer, 0xdd, allocation.capacity);

    struct aws_byte_buf output = aws_byte_buf_from_empty_array(allocation.buffer + 1, output_size);

    ASSERT_SUCCESS(aws_hex_encode(&to_encode, &output), "encode call should have succeeded");

    ASSERT_BIN_ARRAYS_EQUALS(
        expected,
        expected_size,
        output.buffer,
        output_size,
        "Encode output should have been {%s}, was {%s}.",
        expected,
        output.buffer);
    ASSERT_INT_EQUALS(output_size, output.len);
    ASSERT_INT_EQUALS(
        (unsigned char)*(allocation.buffer),
        (unsigned char)0xdd,
        "Write should not have occurred before the start of the buffer.");
    ASSERT_INT_EQUALS(
        (unsigned char)*(allocation.buffer + output_size + 1),
        (unsigned char)0xdd,
        "Write should not have occurred after the start of the buffer.");

    ASSERT_SUCCESS(
        aws_hex_compute_decoded_len(expected_size - 1, &output_size),
        "compute hex decoded len failed with error %d",
        aws_last_error());
    memset(allocation.buffer, 0xdd, allocation.capacity);

    ASSERT_INT_EQUALS(test_str_size - 1, output_size, "Output size on string should be %d", test_str_size - 1);
    aws_byte_buf_reset(&output, false);

    struct aws_byte_cursor expected_buf = aws_byte_cursor_from_array(expected, expected_size - 1);
    ASSERT_SUCCESS(aws_hex_decode(&expected_buf, &output), "decode call should have succeeded");

    ASSERT_BIN_ARRAYS_EQUALS(
        test_str, test_str_size - 1, output.buffer, output_size, "Decode output should have been %s.", test_str);
    ASSERT_INT_EQUALS(output_size, output.len);
    ASSERT_INT_EQUALS(
        (unsigned char)*(allocation.buffer),
        (unsigned char)0xdd,
        "Write should not have occurred before the start of the buffer.");
    ASSERT_INT_EQUALS(
        (unsigned char)*(allocation.buffer + output_size + 1),
        (unsigned char)0xdd,
        "Write should not have occurred after the start of the buffer.");

    aws_byte_buf_clean_up(&allocation);
    return 0;
}

static int s_hex_encoding_test_case_empty(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "";
    char expected[] = "";

    return s_run_hex_encoding_test_case(allocator, test_data, sizeof(test_data), expected, sizeof(expected));
}

AWS_TEST_CASE(hex_encoding_test_case_empty_test, s_hex_encoding_test_case_empty)

static int s_hex_encoding_test_case_f(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "f";
    char expected[] = "66";

    return s_run_hex_encoding_test_case(allocator, test_data, sizeof(test_data), expected, sizeof(expected));
}

AWS_TEST_CASE(hex_encoding_test_case_f_test, s_hex_encoding_test_case_f)

static int s_hex_encoding_test_case_fo(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "fo";
    char expected[] = "666f";

    return s_run_hex_encoding_test_case(allocator, test_data, sizeof(test_data), expected, sizeof(expected));
}

AWS_TEST_CASE(hex_encoding_test_case_fo_test, s_hex_encoding_test_case_fo)

static int s_hex_encoding_test_case_foo(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "foo";
    char expected[] = "666f6f";

    return s_run_hex_encoding_test_case(allocator, test_data, sizeof(test_data), expected, sizeof(expected));
}

AWS_TEST_CASE(hex_encoding_test_case_foo_test, s_hex_encoding_test_case_foo)

static int s_hex_encoding_test_case_foob(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "foob";
    char expected[] = "666f6f62";

    return s_run_hex_encoding_test_case(allocator, test_data, sizeof(test_data), expected, sizeof(expected));
}

AWS_TEST_CASE(hex_encoding_test_case_foob_test, s_hex_encoding_test_case_foob)

static int s_hex_encoding_test_case_fooba(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "fooba";
    char expected[] = "666f6f6261";

    return s_run_hex_encoding_test_case(allocator, test_data, sizeof(test_data), expected, sizeof(expected));
}

AWS_TEST_CASE(hex_encoding_test_case_fooba_test, s_hex_encoding_test_case_fooba)

static int s_hex_encoding_test_case_foobar(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "foobar";
    char expected[] = "666f6f626172";

    return s_run_hex_encoding_test_case(allocator, test_data, sizeof(test_data), expected, sizeof(expected));
}

AWS_TEST_CASE(hex_encoding_test_case_foobar_test, s_hex_encoding_test_case_foobar)

static int s_hex_encoding_append_test_case(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "foobar";
    char expected[] = "666f6f626172";

    return s_run_hex_encoding_test_case(allocator, test_data, sizeof(test_data), expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(hex_encoding_append_test_case, s_hex_encoding_append_test_case)

static int s_hex_encoding_test_case_missing_leading_zero_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    uint8_t expected[] = {0x01, 0x02, 0x03, 0x04};
    char test_data[] = "1020304";

    uint8_t output[sizeof(expected)] = {0};

    struct aws_byte_cursor test_buf = aws_byte_cursor_from_c_str(test_data);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, sizeof(expected));

    ASSERT_SUCCESS(
        aws_hex_decode(&test_buf, &output_buf),
        "Hex decoding failed with "
        "error code %d",
        aws_last_error());

    ASSERT_BIN_ARRAYS_EQUALS(
        expected, sizeof(expected), output, sizeof(output), "Hex decode expected output did not match actual output");

    return 0;
}

AWS_TEST_CASE(hex_encoding_test_case_missing_leading_zero, s_hex_encoding_test_case_missing_leading_zero_fn)

static int s_hex_encoding_invalid_buffer_size_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char test_data[] = "foobar";
    size_t size_too_small = 2;
    uint8_t output[] = {0, 0};

    struct aws_byte_cursor test_buf = aws_byte_cursor_from_c_str(test_data);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, size_too_small);

    ASSERT_ERROR(
        AWS_ERROR_SHORT_BUFFER,
        aws_hex_encode(&test_buf, &output_buf),
        "Invalid buffer size should have failed with AWS_ERROR_SHORT_BUFFER");

    ASSERT_ERROR(
        AWS_ERROR_SHORT_BUFFER,
        aws_hex_decode(&test_buf, &output_buf),
        "Invalid buffer size should have failed with AWS_ERROR_SHORT_BUFFER");
    return 0;
}

AWS_TEST_CASE(hex_encoding_invalid_buffer_size_test, s_hex_encoding_invalid_buffer_size_test_fn)

static int s_hex_encoding_highbyte_string_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char bad_input[] = "66\xb6\xb6"
                       "6f6f6617";
    uint8_t output[sizeof(bad_input)] = {0};

    struct aws_byte_cursor bad_buf = aws_byte_cursor_from_c_str(bad_input);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, sizeof(output));

    ASSERT_ERROR(AWS_ERROR_INVALID_HEX_STR, aws_hex_decode(&bad_buf, &output_buf));
    return 0;
}
AWS_TEST_CASE(hex_encoding_highbyte_string_test, s_hex_encoding_highbyte_string_test_fn)

static int s_hex_encoding_overflow_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char test_data[] = "foobar";
    /* kill off the last two bits, so the not a multiple of 4 check doesn't
     * trigger first */
    size_t overflow = (SIZE_MAX - 1);
    uint8_t output[] = {0, 0};

    struct aws_byte_cursor test_buf = aws_byte_cursor_from_array(test_data, overflow);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, sizeof(output));

    ASSERT_ERROR(
        AWS_ERROR_OVERFLOW_DETECTED,
        aws_hex_encode(&test_buf, &output_buf),
        "overflow buffer size should have failed with AWS_ERROR_OVERFLOW_DETECTED");
    return 0;
}

AWS_TEST_CASE(hex_encoding_overflow_test, s_hex_encoding_overflow_test_fn)

static int s_hex_encoding_invalid_string_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char bad_input[] = "666f6f6x6172";
    uint8_t output[sizeof(bad_input)] = {0};

    struct aws_byte_cursor bad_buf = aws_byte_cursor_from_c_str(bad_input);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, sizeof(output));

    ASSERT_ERROR(
        AWS_ERROR_INVALID_HEX_STR,
        aws_hex_decode(&bad_buf, &output_buf),
        "An invalid string should have failed with AWS_ERROR_INVALID_HEX_STR");
    return 0;
}

AWS_TEST_CASE(hex_encoding_invalid_string_test, s_hex_encoding_invalid_string_test_fn)

AWS_STATIC_STRING_FROM_LITERAL(s_base64_encode_prefix, "Prefix");

/*base64 encoding test cases */
static int s_run_base64_encoding_test_case(
    struct aws_allocator *allocator,
    const char *test_str,
    size_t test_str_size,
    const char *expected,
    size_t expected_size) {
    size_t output_size = 0;
    size_t terminated_size = (expected_size + 1);

    /* Part 1: encoding */
    ASSERT_SUCCESS(
        aws_base64_compute_encoded_len(test_str_size, &output_size),
        "Compute base64 encoded length failed with %d",
        aws_last_error());
    ASSERT_INT_EQUALS(terminated_size, output_size, "Output size on string should be %d", terminated_size);

    struct aws_byte_cursor to_encode = aws_byte_cursor_from_array(test_str, test_str_size);

    struct aws_byte_buf allocation;
    ASSERT_SUCCESS(aws_byte_buf_init(&allocation, allocator, output_size + 2));
    memset(allocation.buffer, 0xdd, allocation.capacity);

    struct aws_byte_buf output = aws_byte_buf_from_empty_array(allocation.buffer + 1, output_size);

    ASSERT_SUCCESS(aws_base64_encode(&to_encode, &output), "encode call should have succeeded");

    ASSERT_BIN_ARRAYS_EQUALS(
        expected,
        expected_size,
        output.buffer,
        output.len,
        "Encode output should have been {%s}, was {%s}.",
        expected,
        output.buffer);
    ASSERT_INT_EQUALS(
        (unsigned char)*(allocation.buffer),
        (unsigned char)0xdd,
        "Write should not have occurred before the start of the buffer.");
    ASSERT_INT_EQUALS(
        (unsigned char)*(allocation.buffer + output_size + 1),
        (unsigned char)0xdd,
        "Write should not have occurred after the start of the buffer.");

    aws_byte_buf_clean_up(&allocation);

    /* part 2 - encoding properly appends rather than overwrites */
    ASSERT_SUCCESS(aws_byte_buf_init(&allocation, allocator, output_size + s_base64_encode_prefix->len));
    struct aws_byte_cursor prefix_cursor = aws_byte_cursor_from_string(s_base64_encode_prefix);
    ASSERT_SUCCESS(aws_byte_buf_append(&allocation, &prefix_cursor));

    ASSERT_SUCCESS(aws_base64_encode(&to_encode, &allocation), "encode call should have succeeded");

    ASSERT_BIN_ARRAYS_EQUALS(
        expected,
        expected_size,
        allocation.buffer + s_base64_encode_prefix->len,
        expected_size,
        "Encode output should have been {%s}, was {%s}.",
        expected,
        allocation.buffer + s_base64_encode_prefix->len);

    struct aws_byte_cursor prefix_output = {.ptr = allocation.buffer, .len = s_base64_encode_prefix->len};
    ASSERT_BIN_ARRAYS_EQUALS(
        s_base64_encode_prefix->bytes,
        s_base64_encode_prefix->len,
        allocation.buffer,
        s_base64_encode_prefix->len,
        "Encode prefix should have been {%s}, was {" PRInSTR "}.",
        s_base64_encode_prefix->bytes,
        AWS_BYTE_CURSOR_PRI(prefix_output));

    aws_byte_buf_clean_up(&allocation);

    /* Part 3: decoding */
    struct aws_byte_cursor expected_cur = aws_byte_cursor_from_array(expected, expected_size);
    ASSERT_SUCCESS(
        aws_base64_compute_decoded_len(&expected_cur, &output_size),
        "Compute base64 decoded length failed with %d",
        aws_last_error());
    ASSERT_INT_EQUALS(test_str_size, output_size, "Output size on string should be %d", test_str_size);

    ASSERT_SUCCESS(aws_byte_buf_init(&allocation, allocator, output_size + 2));
    memset(allocation.buffer, 0xdd, allocation.capacity);

    output = aws_byte_buf_from_empty_array(allocation.buffer + 1, output_size);

    struct aws_byte_cursor expected_buf = aws_byte_cursor_from_array(expected, expected_size);
    ASSERT_SUCCESS(aws_base64_decode(&expected_buf, &output), "decode call should have succeeded");

    ASSERT_BIN_ARRAYS_EQUALS(
        test_str,
        test_str_size,
        output.buffer,
        output_size,
        "Decode output should have been {%s} (len=%zu).",
        test_str,
        test_str_size);
    ASSERT_INT_EQUALS(
        (unsigned char)*(allocation.buffer),
        (unsigned char)0xdd,
        "Write should not have occurred before the start of the buffer.");
    ASSERT_INT_EQUALS(
        (unsigned char)*(allocation.buffer + output_size + 1),
        (unsigned char)0xdd,
        "Write should not have occurred after the start of the buffer.");

    aws_byte_buf_clean_up(&allocation);

    return 0;
}

static int s_base64_encoding_test_case_empty(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "";
    char expected[] = "";

    return s_run_base64_encoding_test_case(allocator, test_data, sizeof(test_data) - 1, expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_case_empty_test, s_base64_encoding_test_case_empty)

static int s_base64_encoding_test_case_f(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "f";
    char expected[] = "Zg==";

    return s_run_base64_encoding_test_case(allocator, test_data, sizeof(test_data) - 1, expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_case_f_test, s_base64_encoding_test_case_f)

static int s_base64_encoding_test_case_fo(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "fo";
    char expected[] = "Zm8=";

    return s_run_base64_encoding_test_case(allocator, test_data, sizeof(test_data) - 1, expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_case_fo_test, s_base64_encoding_test_case_fo)

static int s_base64_encoding_test_case_foo(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "foo";
    char expected[] = "Zm9v";

    return s_run_base64_encoding_test_case(allocator, test_data, sizeof(test_data) - 1, expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_case_foo_test, s_base64_encoding_test_case_foo)

static int s_base64_encoding_test_case_foob(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "foob";
    char expected[] = "Zm9vYg==";

    return s_run_base64_encoding_test_case(allocator, test_data, sizeof(test_data) - 1, expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_case_foob_test, s_base64_encoding_test_case_foob)

static int s_base64_encoding_test_case_fooba(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "fooba";
    char expected[] = "Zm9vYmE=";

    return s_run_base64_encoding_test_case(allocator, test_data, sizeof(test_data) - 1, expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_case_fooba_test, s_base64_encoding_test_case_fooba)

static int s_base64_encoding_test_case_foobar(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "foobar";
    char expected[] = "Zm9vYmFy";

    return s_run_base64_encoding_test_case(allocator, test_data, sizeof(test_data) - 1, expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_case_foobar_test, s_base64_encoding_test_case_foobar)

static int s_base64_encoding_test_case_32bytes(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    /*                  01234567890123456789012345678901 */
    char test_data[] = "this is a 32 byte long string!!!";
    char expected[] = "dGhpcyBpcyBhIDMyIGJ5dGUgbG9uZyBzdHJpbmchISE=";

    return s_run_base64_encoding_test_case(allocator, test_data, sizeof(test_data) - 1, expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_case_32bytes_test, s_base64_encoding_test_case_32bytes)

static int s_base64_encoding_test_zeros_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    uint8_t test_data[6] = {0};
    char expected[] = "AAAAAAAA";

    return s_run_base64_encoding_test_case(
        allocator, (char *)test_data, sizeof(test_data), expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_zeros, s_base64_encoding_test_zeros_fn)

static int s_base64_encoding_test_roundtrip(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    (void)allocator;

    fprintf(stderr, "--test\n");
    uint8_t test_data[32];
    for (size_t i = 0; i < sizeof(test_data); i++) {
        /* 0000 0100 0010 0000 1100 0100 */
#if 0
        test_data[i] = 0x;
        test_data[i + 1] = 0x20;
        test_data[i + 2] = 0xc4;
#endif
        test_data[i] = (uint8_t)i;
        /* b64 nibbles: 1 2 3 4 (BCDE) */
    }
    struct aws_byte_cursor original_data = aws_byte_cursor_from_array(test_data, sizeof(test_data));

    uint8_t test_hex[65] = {0};
    struct aws_byte_buf hex = aws_byte_buf_from_empty_array(test_hex, sizeof(test_hex));

    uint8_t test_b64[128] = {0};
    struct aws_byte_buf b64_data = aws_byte_buf_from_empty_array(test_b64, sizeof(test_b64));

    aws_base64_encode(&original_data, &b64_data);
    b64_data.len--;

    uint8_t decoded_data[32] = {0};
    struct aws_byte_buf decoded_buf = aws_byte_buf_from_empty_array(decoded_data, sizeof(decoded_data));

    struct aws_byte_cursor b64_cur = aws_byte_cursor_from_buf(&b64_data);
    aws_base64_decode(&b64_cur, &decoded_buf);

    if (memcmp(decoded_buf.buffer, original_data.ptr, decoded_buf.len) != 0) {
        aws_hex_encode(&original_data, &hex);
        fprintf(stderr, "Base64 round-trip failed\n");
        fprintf(stderr, "Original: %s\n", (char *)test_hex);
        fprintf(stderr, "Base64  : ");
        for (size_t i = 0; i < sizeof(test_b64); i++) {
            if (!test_b64[i]) {
                break;
            }
            fprintf(stderr, " %c", test_b64[i]);
        }
        fprintf(stderr, "\n");
        memset(test_hex, 0, sizeof(test_hex));
        struct aws_byte_cursor decoded_cur = aws_byte_cursor_from_buf(&decoded_buf);
        aws_hex_encode(&decoded_cur, &hex);
        fprintf(stderr, "Decoded : %s\n", (char *)test_hex);
        return 1;
    }

    return 0;
}
AWS_TEST_CASE(base64_encoding_test_roundtrip, s_base64_encoding_test_roundtrip)

/* this test is here because I manually touched the decoding table with sentinal
 * values for efficiency reasons and I want to make sure it matches the encoded
 * string. This checks that none of those values that were previously 0 which I
 * moved to a sentinal value of 0xDD, were actually supposed to be a 0 other
 * than character value of 65 -> "A" -> 0.
 */
static int s_base64_encoding_test_all_values_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    uint8_t test_data[255] = {0};

    for (uint8_t i = 0; i < (uint8_t)sizeof(test_data); ++i) {
        test_data[i] = i;
    }

    char expected[] = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERU"
                      "ZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouM"
                      "jY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0t"
                      "PU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+fr7/P3+";

    return s_run_base64_encoding_test_case(
        allocator, (char *)test_data, sizeof(test_data), expected, sizeof(expected) - 1);
}

AWS_TEST_CASE(base64_encoding_test_all_values, s_base64_encoding_test_all_values_fn)

static int s_base64_encoding_buffer_size_too_small_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char test_data[] = "foobar";
    char encoded_data[] = "Zm9vYmFy";
    size_t size_too_small = 4;
    uint8_t output[] = {0, 0};

    struct aws_byte_cursor test_buf = aws_byte_cursor_from_c_str(test_data);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, size_too_small);

    ASSERT_ERROR(
        AWS_ERROR_SHORT_BUFFER,
        aws_base64_encode(&test_buf, &output_buf),
        "Invalid buffer size should have failed with AWS_ERROR_SHORT_BUFFER");

    struct aws_byte_cursor encoded_buf = aws_byte_cursor_from_c_str(encoded_data);

    ASSERT_ERROR(
        AWS_ERROR_SHORT_BUFFER,
        aws_base64_decode(&encoded_buf, &output_buf),
        "Invalid buffer size should have failed with AWS_ERROR_SHORT_BUFFER");
    return 0;
}

AWS_TEST_CASE(base64_encoding_buffer_size_too_small_test, s_base64_encoding_buffer_size_too_small_test_fn)

static int s_base64_encoding_buffer_size_overflow_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char test_data[] = "foobar";
    char encoded_data[] = "Zm9vYmFy";
    /* kill off the last two bits, so the not a multiple of 4 check doesn't
     * trigger first */
    size_t overflow = (SIZE_MAX - 1) & ~0x03;
    uint8_t output[] = {0, 0};

    struct aws_byte_cursor test_buf = aws_byte_cursor_from_array(test_data, overflow + 2);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, sizeof(output));

    ASSERT_ERROR(
        AWS_ERROR_OVERFLOW_DETECTED,
        aws_base64_encode(&test_buf, &output_buf),
        "overflow buffer size should have failed with AWS_ERROR_OVERFLOW_DETECTED");

    struct aws_byte_cursor encoded_buf = aws_byte_cursor_from_array(encoded_data, overflow);

    ASSERT_ERROR(
        AWS_ERROR_OVERFLOW_DETECTED,
        aws_base64_decode(&encoded_buf, &output_buf),
        "overflow buffer size should have failed with AWS_ERROR_OVERFLOW_DETECTED");
    return 0;
}

AWS_TEST_CASE(base64_encoding_buffer_size_overflow_test, s_base64_encoding_buffer_size_overflow_test_fn)

static int s_base64_encoding_buffer_size_invalid_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char encoded_data[] = "Zm9vYmFy";
    /* kill off the last two bits, so the not a multiple of 4 check doesn't
     * trigger first */
    uint8_t output[] = {0, 0};

    struct aws_byte_cursor encoded_buf = aws_byte_cursor_from_array(encoded_data, sizeof(encoded_data));
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, sizeof(output));

    ASSERT_ERROR(
        AWS_ERROR_INVALID_BASE64_STR,
        aws_base64_decode(&encoded_buf, &output_buf),
        "Non multiple of 4 buffer size should have failed with AWS_ERROR_INVALID_BASE64_STR");
    return 0;
}

AWS_TEST_CASE(base64_encoding_buffer_size_invalid_test, s_base64_encoding_buffer_size_invalid_test_fn)

static int s_base64_encoding_invalid_buffer_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char encoded_data[] = "Z\n9vYmFy";
    uint8_t output[sizeof(encoded_data)] = {0};

    struct aws_byte_cursor encoded_buf = aws_byte_cursor_from_c_str(encoded_data);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, sizeof(output));

    ASSERT_ERROR(
        AWS_ERROR_INVALID_BASE64_STR,
        aws_base64_decode(&encoded_buf, &output_buf),
        "buffer with invalid character should have failed with AWS_ERROR_INVALID_BASE64_STR");
    return 0;
}

AWS_TEST_CASE(base64_encoding_invalid_buffer_test, s_base64_encoding_invalid_buffer_test_fn)

static int s_base64_encoding_highbyte_string_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char bad_input[] = "AAAA\xC1"
                       "AAA";
    uint8_t output[sizeof(bad_input)] = {0};

    struct aws_byte_cursor bad_buf = aws_byte_cursor_from_c_str(bad_input);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, sizeof(output));

    ASSERT_ERROR(AWS_ERROR_INVALID_BASE64_STR, aws_base64_decode(&bad_buf, &output_buf));
    return 0;
}
AWS_TEST_CASE(base64_encoding_highbyte_string_test, s_base64_encoding_highbyte_string_test_fn)

static int s_base64_encoding_invalid_padding_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    char encoded_data[] = "Zm9vY===";
    uint8_t output[sizeof(encoded_data)] = {0};

    struct aws_byte_cursor encoded_buf = aws_byte_cursor_from_c_str(encoded_data);
    struct aws_byte_buf output_buf = aws_byte_buf_from_empty_array(output, sizeof(output));

    ASSERT_ERROR(
        AWS_ERROR_INVALID_BASE64_STR,
        aws_base64_decode(&encoded_buf, &output_buf),
        "buffer with invalid padding should have failed with AWS_ERROR_INVALID_BASE64_STR");
    return 0;
}

AWS_TEST_CASE(base64_encoding_invalid_padding_test, s_base64_encoding_invalid_padding_test_fn)

/* network integer encoding tests */
static int s_uint64_buffer_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    uint64_t test_value = 0x1020304050607080;
    uint8_t buffer[8] = {0};
    aws_write_u64(test_value, buffer);

    uint8_t expected[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), buffer, sizeof(buffer), "Uint64_t to buffer failed");

    uint64_t unmarshalled_value = aws_read_u64(buffer);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");
    return 0;
}

AWS_TEST_CASE(uint64_buffer_test, s_uint64_buffer_test_fn)

static int s_uint64_buffer_non_aligned_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    uint64_t test_value = 0x1020304050607080;
    uint8_t *buffer = (uint8_t *)aws_mem_acquire(allocator, 9);

    ASSERT_FALSE((size_t)buffer & 0x07, "Heap allocated buffer should have been 8-byte aligned.");

    aws_write_u64(test_value, buffer + 1);

    uint8_t expected[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), (buffer + 1), sizeof(expected), "Uint64_t to buffer failed");

    uint64_t unmarshalled_value = aws_read_u64(buffer + 1);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");

    aws_mem_release(allocator, (void *)buffer);

    return 0;
}

AWS_TEST_CASE(uint64_buffer_non_aligned_test, s_uint64_buffer_non_aligned_test_fn)

static int s_uint32_buffer_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    uint32_t test_value = 0x10203040;
    uint8_t buffer[4] = {0};
    aws_write_u32(test_value, buffer);

    uint8_t expected[] = {0x10, 0x20, 0x30, 0x40};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), buffer, sizeof(buffer), "Uint32_t to buffer failed");

    uint32_t unmarshalled_value = aws_read_u32(buffer);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");

    return 0;
}

AWS_TEST_CASE(uint32_buffer_test, s_uint32_buffer_test_fn)

static int s_uint32_buffer_non_aligned_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    uint32_t test_value = 0x10203040;
    uint8_t *buffer = (uint8_t *)aws_mem_acquire(allocator, 9);

    ASSERT_FALSE((size_t)buffer & 0x07, "Heap allocated buffer should have been 8-byte aligned.");

    aws_write_u32(test_value, buffer + 5);

    uint8_t expected[] = {0x10, 0x20, 0x30, 0x40};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), (buffer + 5), sizeof(expected), "Uint32_t to buffer failed");

    uint64_t unmarshalled_value = aws_read_u32(buffer + 5);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");

    aws_mem_release(allocator, (void *)buffer);

    return 0;
}

AWS_TEST_CASE(uint32_buffer_non_aligned_test, s_uint32_buffer_non_aligned_test_fn)

static int s_uint24_buffer_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    uint32_t test_value = 0x102030;
    uint8_t buffer[3] = {0};
    aws_write_u24(test_value, buffer);

    uint8_t expected[] = {0x10, 0x20, 0x30};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), buffer, sizeof(buffer), "24 bit int to buffer failed");

    uint32_t unmarshalled_value = aws_read_u24(buffer);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");

    return 0;
}

AWS_TEST_CASE(uint24_buffer_test, s_uint24_buffer_test_fn)

static int s_uint24_buffer_non_aligned_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    uint32_t test_value = 0x102030;
    uint8_t *buffer = (uint8_t *)aws_mem_acquire(allocator, 9);

    ASSERT_FALSE((size_t)buffer & 0x07, "Heap allocated buffer should have been 8-byte aligned.");
    aws_write_u24(test_value, buffer + 6);

    uint8_t expected[] = {0x10, 0x20, 0x30};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), (buffer + 6), sizeof(expected), "24 bit int to buffer failed");

    uint32_t unmarshalled_value = aws_read_u24(buffer + 6);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");
    aws_mem_release(allocator, (void *)buffer);

    return 0;
}

AWS_TEST_CASE(uint24_buffer_non_aligned_test, s_uint24_buffer_non_aligned_test_fn)

static int s_uint16_buffer_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    uint16_t test_value = 0x1020;
    uint8_t buffer[2] = {0};
    aws_write_u16(test_value, buffer);

    uint8_t expected[] = {0x10, 0x20};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), buffer, sizeof(buffer), "Uint16_t to buffer failed");

    uint16_t unmarshalled_value = aws_read_u16(buffer);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");

    return 0;
}

AWS_TEST_CASE(uint16_buffer_test, s_uint16_buffer_test_fn)

static int s_uint16_buffer_non_aligned_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    uint16_t test_value = 0x1020;
    uint8_t *buffer = (uint8_t *)aws_mem_acquire(allocator, 9);

    ASSERT_FALSE((size_t)buffer & 0x07, "Heap allocated buffer should have been 8-byte aligned.");
    aws_write_u16(test_value, buffer + 7);

    uint8_t expected[] = {0x10, 0x20};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), (buffer + 7), sizeof(expected), "16 bit int to buffer failed");

    uint16_t unmarshalled_value = aws_read_u16(buffer + 7);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");
    aws_mem_release(allocator, (void *)buffer);

    return 0;
}

AWS_TEST_CASE(uint16_buffer_non_aligned_test, s_uint16_buffer_non_aligned_test_fn)

/* sanity check that signed/unsigned work the same */
static int s_uint16_buffer_signed_positive_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    int16_t test_value = 0x4030;
    uint8_t buffer[2] = {0};
    aws_write_u16((uint16_t)test_value, buffer);

    uint8_t expected[] = {0x40, 0x30};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), buffer, sizeof(buffer), "Uint16_t to buffer failed");

    int16_t unmarshalled_value = (int16_t)aws_read_u16(buffer);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");

    return 0;
}

AWS_TEST_CASE(uint16_buffer_signed_positive_test, s_uint16_buffer_signed_positive_test_fn)

static int s_uint16_buffer_signed_negative_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    int16_t test_value = -2;
    uint8_t buffer[2] = {0};
    aws_write_u16((uint16_t)test_value, buffer);

    uint8_t expected[] = {0xFF, 0xFE};
    ASSERT_BIN_ARRAYS_EQUALS(expected, sizeof(expected), buffer, sizeof(buffer), "Uint16_t to buffer failed");

    int16_t unmarshalled_value = (int16_t)aws_read_u16(buffer);
    ASSERT_INT_EQUALS(test_value, unmarshalled_value, "After unmarshalling the encoded data, it didn't match");

    return 0;
}

AWS_TEST_CASE(uint16_buffer_signed_negative_test, s_uint16_buffer_signed_negative_test_fn)

static int s_run_hex_encoding_append_dynamic_test_case(
    struct aws_allocator *allocator,
    const char *test_str,
    const char *expected,
    size_t initial_capacity,
    size_t starting_offset) {

    size_t output_size = 2 * strlen(test_str);

    struct aws_byte_cursor to_encode = aws_byte_cursor_from_c_str(test_str);

    struct aws_byte_buf dest;
    ASSERT_SUCCESS(aws_byte_buf_init(&dest, allocator, initial_capacity));
    memset(dest.buffer, 0xdd, dest.capacity);

    dest.len = starting_offset;

    ASSERT_SUCCESS(aws_hex_encode_append_dynamic(&to_encode, &dest), "encode call should have succeeded");

    size_t expected_size = strlen(expected);

    ASSERT_BIN_ARRAYS_EQUALS(
        expected,
        expected_size,
        dest.buffer + starting_offset,
        output_size,
        "Encode output should have been {%s}, was {%s}.",
        expected,
        dest.buffer + starting_offset);
    ASSERT_INT_EQUALS(output_size, dest.len - starting_offset);

    for (size_t i = 0; i < starting_offset; ++i) {
        ASSERT_INT_EQUALS(
            (unsigned char)*(dest.buffer + i),
            (unsigned char)0xdd,
            "Write should not have occurred before the the encoding's starting position.");
    }

    for (size_t i = starting_offset + output_size; i < dest.capacity; ++i) {
        ASSERT_INT_EQUALS(
            (unsigned char)*(dest.buffer + i),
            (unsigned char)0xdd,
            "Write should not have occurred after the encoding's final position.");
    }

    aws_byte_buf_clean_up(&dest);
    return 0;
}

static int s_hex_encoding_append_dynamic_test_case_fooba(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "fooba";
    char expected[] = "666f6f6261";

    ASSERT_TRUE(s_run_hex_encoding_append_dynamic_test_case(allocator, test_data, expected, 5, 3) == AWS_OP_SUCCESS);
    ASSERT_TRUE(s_run_hex_encoding_append_dynamic_test_case(allocator, test_data, expected, 50, 3) == AWS_OP_SUCCESS);

    return 0;
}

AWS_TEST_CASE(hex_encoding_append_dynamic_test_case_fooba, s_hex_encoding_append_dynamic_test_case_fooba)

static int s_hex_encoding_append_dynamic_test_case_empty(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    char test_data[] = "";
    char expected[] = "";

    ASSERT_TRUE(s_run_hex_encoding_append_dynamic_test_case(allocator, test_data, expected, 5, 3) == AWS_OP_SUCCESS);
    ASSERT_TRUE(s_run_hex_encoding_append_dynamic_test_case(allocator, test_data, expected, 50, 3) == AWS_OP_SUCCESS);

    return 0;
}

AWS_TEST_CASE(hex_encoding_append_dynamic_test_case_empty, s_hex_encoding_append_dynamic_test_case_empty)
