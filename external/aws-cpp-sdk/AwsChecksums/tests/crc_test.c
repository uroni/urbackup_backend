/*
 * Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/checksums/crc.h>
#include <aws/checksums/private/crc_priv.h>
#include <test_macros.h>

static const uint8_t DATA_32_ZEROS[32] = {0};
static const uint32_t KNOWN_CRC32_32_ZEROES = 0x190A55AD;
static const uint32_t KNOWN_CRC32C_32_ZEROES = 0x8A9136AA;

static const uint8_t DATA_32_VALUES[32] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                           16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
static const uint32_t KNOWN_CRC32C_32_VALUES = 0x46DD794E;

static const uint8_t TEST_VECTOR[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static const uint32_t KNOWN_CRC32_TEST_VECTOR = 0xCBF43926;
static const uint32_t KNOWN_CRC32C_TEST_VECTOR = 0xE3069283;

static uint8_t *s_non_mem_aligned_vector;

typedef uint32_t(crc_fn)(const uint8_t *input, int length, uint32_t previousCrc32);
#define CRC_FUNC_NAME(crc_func) #crc_func, crc_func
#define DATA_NAME(dataset) #dataset, dataset, sizeof(dataset)

/* Makes sure that the specified crc function produces the expected results for known input and output*/
static int s_test_known_crc(
    const char *func_name,
    crc_fn *func,
    const char *data_name,
    const uint8_t *input,
    size_t length,
    uint32_t expected) {

    uint32_t result = func(input, (int)length, 0);
    ASSERT_HEX_EQUALS(expected, result, "%s(%s)", func_name, data_name);

    /* chain the crc computation so 2 calls each operate on about 1/2 of the buffer*/
    uint32_t crc1 = func(input, (int)(length / 2), 0);
    result = func(input + (length / 2), (int)(length - length / 2), crc1);
    ASSERT_HEX_EQUALS(expected, result, "chaining %s(%s)", func_name, data_name);

    crc1 = 0;
    for (size_t i = 0; i < length; ++i) {
        crc1 = func(input + i, 1, crc1);
    }

    ASSERT_HEX_EQUALS(expected, crc1, "one byte at a time %s(%s)", func_name, data_name);

    RETURN_SUCCESS("%s() pass", func_name);
failure:
    return FAILURE;
}

/* helper function that groups crc32 tests*/
static int s_test_known_crc32(const char *func_name, crc_fn *func) {
    int res = 0;
    res |= s_test_known_crc(func_name, func, DATA_NAME(DATA_32_ZEROS), KNOWN_CRC32_32_ZEROES);
    res |= s_test_known_crc(func_name, func, DATA_NAME(TEST_VECTOR), KNOWN_CRC32_TEST_VECTOR);
    return res;
}

/* helper function that groups crc32c tests*/
static int s_test_known_crc32c(const char *func_name, crc_fn *func) {
    int res = 0;

    res |= s_test_known_crc(func_name, func, DATA_NAME(DATA_32_ZEROS), KNOWN_CRC32C_32_ZEROES);
    res |= s_test_known_crc(func_name, func, DATA_NAME(DATA_32_VALUES), KNOWN_CRC32C_32_VALUES);
    res |= s_test_known_crc(func_name, func, DATA_NAME(TEST_VECTOR), KNOWN_CRC32C_TEST_VECTOR);

    /*this tests three things, first it tests the case where we aren't 8-byte aligned*/
    /*seconde, it tests that reads aren't performed before start of buffer*/
    /*third, it tests that wwrites aren't performed after the end of the buffer.*/
    /*if any of those things happen, then the checksum will be wrong and the assertion will fail */
    s_non_mem_aligned_vector = malloc(sizeof(DATA_32_VALUES) + 6);
    memset(s_non_mem_aligned_vector, 1, sizeof(DATA_32_VALUES) + 6);
    memcpy(s_non_mem_aligned_vector + 3, DATA_32_VALUES, sizeof(DATA_32_VALUES));
    res |= s_test_known_crc(
        func_name,
        func,
        "non_mem_aligned_vector",
        s_non_mem_aligned_vector + 3,
        sizeof(DATA_32_VALUES),
        KNOWN_CRC32C_32_VALUES);
    free(s_non_mem_aligned_vector);
    return res;
}

/**
 * Quick sanity check of some known CRC values for known input.
 * The reference functions are included in these tests to verify that they aren't obviously broken.
 */
static int s_test_crc32c(void) {
    int res = 0;

    res |= s_test_known_crc32c(CRC_FUNC_NAME(aws_checksums_crc32c));
    res |= s_test_known_crc32c(CRC_FUNC_NAME(aws_checksums_crc32c_sw));

    return res;
}

static int s_test_crc32(void) {
    int res = 0;
    res |= s_test_known_crc32(CRC_FUNC_NAME(aws_checksums_crc32));

    return res;
}
