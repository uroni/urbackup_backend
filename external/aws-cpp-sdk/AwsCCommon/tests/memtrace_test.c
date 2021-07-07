/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/testing/aws_test_harness.h>

#include <aws/common/allocator.h>
#include <aws/common/device_random.h>

#include "logging/test_logger.h"

#define NUM_ALLOCS 100
static int s_test_memtrace_count(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_allocator *tracer = aws_mem_tracer_new(allocator, NULL, AWS_MEMTRACE_BYTES, 0);

    void *allocs[NUM_ALLOCS] = {0};
    size_t sizes[NUM_ALLOCS] = {0};
    size_t total = 0;

    for (size_t idx = 0; idx < AWS_ARRAY_SIZE(allocs); ++idx) {
        uint32_t size = 0;
        aws_device_random_u32(&size);
        size = (size % 1024) + 1; /* not necessary to allocate a gajillion bytes */
        allocs[idx] = aws_mem_acquire(tracer, size);
        sizes[idx] = size;
        total += size;
    }

    ASSERT_UINT_EQUALS(total, aws_mem_tracer_bytes(tracer));
    ASSERT_UINT_EQUALS(NUM_ALLOCS, aws_mem_tracer_count(tracer));

    size_t freed = 0;
    for (size_t idx = 0; idx < AWS_ARRAY_SIZE(allocs); ++idx) {
        uint32_t roll = 0;
        aws_device_random_u32(&roll);
        if (roll % 3 == 0) {
            aws_mem_release(tracer, allocs[idx]);
            allocs[idx] = NULL;
            total -= sizes[idx];
            ++freed;
        }
    }

    ASSERT_UINT_EQUALS(total, aws_mem_tracer_bytes(tracer));
    ASSERT_UINT_EQUALS(NUM_ALLOCS - freed, aws_mem_tracer_count(tracer));

    for (size_t idx = 0; idx < AWS_ARRAY_SIZE(allocs); ++idx) {
        if (allocs[idx]) {
            aws_mem_release(tracer, allocs[idx]);
        }
    }

    ASSERT_UINT_EQUALS(0, aws_mem_tracer_bytes(tracer));
    ASSERT_UINT_EQUALS(0, aws_mem_tracer_count(tracer));

    struct aws_allocator *original = aws_mem_tracer_destroy(tracer);
    ASSERT_PTR_EQUALS(allocator, original);

    return 0;
}
AWS_TEST_CASE(test_memtrace_count, s_test_memtrace_count)

static void *s_alloc_1(struct aws_allocator *allocator, size_t size) {
    return aws_mem_acquire(allocator, size);
}

static void *s_alloc_2(struct aws_allocator *allocator, size_t size) {
    return aws_mem_acquire(allocator, size);
}

static void *s_alloc_3(struct aws_allocator *allocator, size_t size) {
    return aws_mem_acquire(allocator, size);
}

static void *s_alloc_4(struct aws_allocator *allocator, size_t size) {
    return aws_mem_acquire(allocator, size);
}

static struct aws_logger s_test_logger;

static int s_test_memtrace_stacks(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    /* only bother to run this test if the platform can do a backtrace */
    void *probe_stack[1];
    if (!aws_backtrace(probe_stack, 1)) {
        return 0;
    }

    test_logger_init(&s_test_logger, allocator, AWS_LL_TRACE, 0);
    aws_logger_set(&s_test_logger);

    struct aws_allocator *tracer = aws_mem_tracer_new(allocator, NULL, AWS_MEMTRACE_STACKS, 8);

    void *allocs[NUM_ALLOCS] = {0};
    size_t total = 0;

    for (size_t idx = 0; idx < AWS_ARRAY_SIZE(allocs); ++idx) {
        uint32_t size = 0;
        aws_device_random_u32(&size);
        size = (size % 1024) + 1; /* not necessary to allocate a gajillion bytes */

        void *(*allocate)(struct aws_allocator *, size_t) = NULL;
        switch (idx % 4) {
            case 0:
                allocate = s_alloc_1;
                break;
            case 1:
                allocate = s_alloc_2;
                break;
            case 2:
                allocate = s_alloc_3;
                break;
            case 3:
                allocate = s_alloc_4;
                break;
        }

        allocs[idx] = allocate(tracer, size);
        total += size;
    }

    ASSERT_UINT_EQUALS(total, aws_mem_tracer_bytes(tracer));
    ASSERT_UINT_EQUALS(NUM_ALLOCS, aws_mem_tracer_count(tracer));
    aws_mem_tracer_dump(tracer);

    /* make sure all of the functions that allocated are found */
    struct test_logger_impl *test_logger = s_test_logger.p_impl;
    /* if this is not a debug build, there may not be symbols, so the test cannot
     * verify if a best effort was made */
#if defined(DEBUG_BUILD)
    ASSERT_TRUE(strstr((const char *)test_logger->log_buffer.buffer, "s_alloc_1"));
    ASSERT_TRUE(strstr((const char *)test_logger->log_buffer.buffer, "s_alloc_2"));
    ASSERT_TRUE(strstr((const char *)test_logger->log_buffer.buffer, "s_alloc_3"));
    ASSERT_TRUE(strstr((const char *)test_logger->log_buffer.buffer, "s_alloc_4"));
    ASSERT_TRUE(strstr((const char *)test_logger->log_buffer.buffer, __FUNCTION__));
#endif

    /* reset log */
    aws_byte_buf_reset(&test_logger->log_buffer, true);

    for (size_t idx = 0; idx < AWS_ARRAY_SIZE(allocs); ++idx) {
        if (allocs[idx]) {
            aws_mem_release(tracer, allocs[idx]);
        }
    }

    ASSERT_UINT_EQUALS(0, aws_mem_tracer_bytes(tracer));
    ASSERT_UINT_EQUALS(0, aws_mem_tracer_count(tracer));
    aws_mem_tracer_dump(tracer);

    /* Make sure no known allocs are left */
    ASSERT_UINT_EQUALS(0, test_logger->log_buffer.len);

    struct aws_allocator *original = aws_mem_tracer_destroy(tracer);
    ASSERT_PTR_EQUALS(allocator, original);

    aws_logger_clean_up(&s_test_logger);

    return 0;
}
AWS_TEST_CASE(test_memtrace_stacks, s_test_memtrace_stacks)

static int s_test_memtrace_none(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    struct aws_allocator *tracer = aws_mem_tracer_new(allocator, NULL, AWS_MEMTRACE_NONE, 0);

    void *allocs[NUM_ALLOCS] = {0};
    size_t total = 0;

    for (size_t idx = 0; idx < AWS_ARRAY_SIZE(allocs); ++idx) {
        uint32_t size = 0;
        aws_device_random_u32(&size);
        size = (size % 1024) + 1; /* not necessary to allocate a gajillion bytes */
        allocs[idx] = aws_mem_acquire(tracer, size);
        total += size;
    }

    ASSERT_UINT_EQUALS(0, aws_mem_tracer_bytes(tracer));

    for (size_t idx = 0; idx < AWS_ARRAY_SIZE(allocs); ++idx) {
        if (allocs[idx]) {
            aws_mem_release(tracer, allocs[idx]);
        }
    }

    ASSERT_UINT_EQUALS(0, aws_mem_tracer_bytes(tracer));

    struct aws_allocator *original = aws_mem_tracer_destroy(tracer);
    ASSERT_PTR_EQUALS(allocator, original);

    return 0;
}
AWS_TEST_CASE(test_memtrace_none, s_test_memtrace_none)

static int s_test_memtrace_midstream(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    void *allocs[NUM_ALLOCS] = {0};

    /* allocate some from the base allocator first */
    for (size_t idx = 0; idx < AWS_ARRAY_SIZE(allocs) / 4; ++idx) {
        uint32_t size = 0;
        aws_device_random_u32(&size);
        size = (size % 1024) + 1; /* not necessary to allocate a gajillion bytes */
        allocs[idx] = aws_mem_acquire(allocator, size);
    }

    struct aws_allocator *tracer = aws_mem_tracer_new(allocator, NULL, AWS_MEMTRACE_BYTES, 0);

    /* Now allocate from the tracer, and make sure everything still works */
    size_t total = 0;
    size_t tracked_allocs = 0;
    for (size_t idx = AWS_ARRAY_SIZE(allocs) / 4 + 1; idx < AWS_ARRAY_SIZE(allocs); ++idx) {
        uint32_t size = 0;
        aws_device_random_u32(&size);
        size = (size % 1024) + 1; /* not necessary to allocate a gajillion bytes */
        allocs[idx] = aws_mem_acquire(tracer, size);
        total += size;
        ++tracked_allocs;
    }

    ASSERT_UINT_EQUALS(total, aws_mem_tracer_bytes(tracer));
    ASSERT_UINT_EQUALS(tracked_allocs, aws_mem_tracer_count(tracer));

    for (size_t idx = 0; idx < AWS_ARRAY_SIZE(allocs); ++idx) {
        if (allocs[idx]) {
            aws_mem_release(tracer, allocs[idx]);
        }
    }

    ASSERT_UINT_EQUALS(0, aws_mem_tracer_bytes(tracer));
    ASSERT_UINT_EQUALS(0, aws_mem_tracer_count(tracer));

    struct aws_allocator *original = aws_mem_tracer_destroy(tracer);
    ASSERT_PTR_EQUALS(allocator, original);

    return 0;
}
AWS_TEST_CASE(test_memtrace_midstream, s_test_memtrace_midstream)
