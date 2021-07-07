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
#include <aws/common/common.h>
#include <aws/testing/aws_test_harness.h>

AWS_TEST_CASE(test_secure_zero, s_test_secure_zero_fn)
static int s_test_secure_zero_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    /* We can't actually test the secure part of the zero operation - anything
     * we do to observe the buffer will teach the compiler that it needs to
     * actually zero the buffer (or provide a convincing-enough simulation of
     * the same). So we'll just test that it behaves like memset.
     */

    unsigned char buf[16];

    for (size_t i = 0; i < sizeof(buf); i++) {
        volatile unsigned char *ptr = buf;
        ptr += i;

        *ptr = (unsigned char)0xDD;
    }

    aws_secure_zero(buf, sizeof(buf) / 2);

    for (size_t i = 0; i < sizeof(buf); i++) {
        if (i < sizeof(buf) / 2) {
            ASSERT_INT_EQUALS(0, buf[i]);
        } else {
            ASSERT_INT_EQUALS((unsigned char)0xDD, (unsigned char)buf[i]);
        }
    }

    return SUCCESS;
}

AWS_TEST_CASE(test_buffer_secure_zero, s_test_buffer_secure_zero_fn)
static int s_test_buffer_secure_zero_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_byte_buf buf;
    size_t len = 27;
    ASSERT_SUCCESS(aws_byte_buf_init(&buf, allocator, len));
    buf.len = buf.capacity;
    for (size_t i = 0; i < len; ++i) {
        buf.buffer[i] = 0xDD;
    }

    aws_byte_buf_secure_zero(&buf);
    for (size_t i = 0; i < len; ++i) {
        ASSERT_INT_EQUALS(0, buf.buffer[i]);
    }
    ASSERT_INT_EQUALS(0, buf.len);

    aws_byte_buf_clean_up(&buf);
    return SUCCESS;
}

AWS_TEST_CASE(test_buffer_clean_up_secure, s_test_buffer_clean_up_secure_fn)
static int s_test_buffer_clean_up_secure_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    /* We cannot test the zeroing of data here, because that would require reading
     * memory that has already been freed. Simply verifies that there is no memory leak.
     */
    struct aws_byte_buf buf;
    ASSERT_SUCCESS(aws_byte_buf_init(&buf, allocator, 37));
    aws_byte_buf_clean_up_secure(&buf);
    ASSERT_INT_EQUALS(buf.len, 0);
    ASSERT_INT_EQUALS(buf.capacity, 0);
    ASSERT_NULL(buf.buffer);
    ASSERT_NULL(buf.allocator);
    return SUCCESS;
}

AWS_TEST_CASE(is_zeroed, s_test_is_zeroed_fn)
static int s_test_is_zeroed_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    /* Using a value that's 2X the largest amount we check in a single CPU instruction */
    enum { max_size = 64 * 2 };
    uint8_t buf[max_size];

    for (size_t size = 1; size <= max_size; ++size) {
        /* Zero out buffer and check */
        memset(buf, 0, size);
        ASSERT_TRUE(aws_is_mem_zeroed(buf, size));

        /* Set 1 byte to be non-zero and check */
        for (size_t non_zero_byte = 0; non_zero_byte < size; ++non_zero_byte) {
            buf[non_zero_byte] = 1;
            ASSERT_FALSE(aws_is_mem_zeroed(buf, size));
            buf[non_zero_byte] = 0;
        }
    }

    return SUCCESS;
}
