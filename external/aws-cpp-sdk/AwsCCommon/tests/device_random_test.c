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
#include <aws/common/device_random.h>

#include <aws/common/byte_buf.h>

#include <aws/testing/aws_test_harness.h>

#include <math.h>

/* Number of random numbers to generate and put in buckets. Higher numbers mean more tolerance */
#define DISTRIBUTION_PUT_COUNT 1000000

/* Must be a power of 2. Lower numbers mean more tolerance. */
#define DISTRIBUTION_BUCKET_COUNT 16

/* Fail if a bucket's contents vary from expected by more than this ratio. Higher ratio means more tolerance.
 * For example, if putting 1000 numbers into 10 buckets, we expect 100 in each bucket.
 * If ratio is 0.25 than accept 75 -> 125 numbers per bucket. */
#define DISTRIBUTION_ACCEPTED_DEVIATION_RATIO 0.05

/* For testing that random number generator has a uniform distribution.
 * They're RANDOM numbers, so to avoid RANDOM failures use lots of inputs and be tolerate some deviance */
struct distribution_tester {
    uint64_t max_value;
    uint64_t buckets[DISTRIBUTION_BUCKET_COUNT];
    uint64_t num_puts;
};

static int s_distribution_tester_put(struct distribution_tester *tester, uint64_t rand_num) {
    ASSERT_TRUE(rand_num <= tester->max_value);
    uint64_t bucket_size = (tester->max_value / DISTRIBUTION_BUCKET_COUNT) + 1;
    uint64_t bucket_idx = rand_num / bucket_size;
    ASSERT_TRUE(bucket_idx < DISTRIBUTION_BUCKET_COUNT);
    tester->buckets[bucket_idx]++;
    tester->num_puts++;
    return AWS_OP_SUCCESS;
}

static int s_distribution_tester_check_results(const struct distribution_tester *tester) {
    ASSERT_TRUE(tester->num_puts == DISTRIBUTION_PUT_COUNT);

    double expected_numbers_per_bucket = (double)DISTRIBUTION_PUT_COUNT / DISTRIBUTION_BUCKET_COUNT;

    uint64_t max_acceptable_numbers_per_bucket =
        (uint64_t)ceil(expected_numbers_per_bucket * (1.0 + DISTRIBUTION_ACCEPTED_DEVIATION_RATIO));

    uint64_t min_acceptable_numbers_per_bucket =
        (uint64_t)floor(expected_numbers_per_bucket * (1.0 - DISTRIBUTION_ACCEPTED_DEVIATION_RATIO));

    for (uint64_t i = 0; i < DISTRIBUTION_BUCKET_COUNT; ++i) {
        uint64_t numbers_in_bucket = tester->buckets[i];
        ASSERT_TRUE(numbers_in_bucket <= max_acceptable_numbers_per_bucket);
        ASSERT_TRUE(numbers_in_bucket >= min_acceptable_numbers_per_bucket);
    }

    return AWS_OP_SUCCESS;
}

static int s_device_rand_u64_distribution_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    struct distribution_tester tester = {.max_value = UINT64_MAX};

    for (size_t i = 0; i < DISTRIBUTION_PUT_COUNT; ++i) {
        uint64_t next_value = 0;
        ASSERT_SUCCESS(aws_device_random_u64(&next_value));
        ASSERT_SUCCESS(s_distribution_tester_put(&tester, next_value));
    }

    ASSERT_SUCCESS(s_distribution_tester_check_results(&tester));
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(device_rand_u64_distribution, s_device_rand_u64_distribution_fn)

static int s_device_rand_u32_distribution_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    struct distribution_tester tester = {.max_value = UINT32_MAX};

    for (size_t i = 0; i < DISTRIBUTION_PUT_COUNT; ++i) {
        uint32_t next_value = 0;
        ASSERT_SUCCESS(aws_device_random_u32(&next_value));
        ASSERT_SUCCESS(s_distribution_tester_put(&tester, next_value));
    }

    ASSERT_SUCCESS(s_distribution_tester_check_results(&tester));
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(device_rand_u32_distribution, s_device_rand_u32_distribution_fn)

static int s_device_rand_u16_distribution_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    struct distribution_tester tester = {.max_value = UINT16_MAX};

    for (size_t i = 0; i < DISTRIBUTION_PUT_COUNT; ++i) {
        uint16_t next_value = 0;
        ASSERT_SUCCESS(aws_device_random_u16(&next_value));
        ASSERT_SUCCESS(s_distribution_tester_put(&tester, next_value));
    }

    ASSERT_SUCCESS(s_distribution_tester_check_results(&tester));
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(device_rand_u16_distribution, s_device_rand_u16_distribution_fn)

static int s_device_rand_buffer_distribution_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    uint8_t array[DISTRIBUTION_PUT_COUNT] = {0};
    struct aws_byte_buf buf = aws_byte_buf_from_empty_array(array, sizeof(array));
    ASSERT_SUCCESS(aws_device_random_buffer(&buf));

    /* Test each byte in the buffer */
    struct distribution_tester tester = {.max_value = UINT8_MAX};

    for (size_t i = 0; i < DISTRIBUTION_PUT_COUNT; ++i) {
        ASSERT_SUCCESS(s_distribution_tester_put(&tester, array[i]));
    }

    ASSERT_SUCCESS(s_distribution_tester_check_results(&tester));
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(device_rand_buffer_distribution, s_device_rand_buffer_distribution_fn)
