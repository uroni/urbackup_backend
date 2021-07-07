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

#include <aws/common/common.h>

#include <aws/common/array_list.h>
#include <aws/common/assert.h>
#include <aws/common/thread.h>
#include <aws/testing/aws_test_harness.h>

#ifdef __MACH__
#    include <CoreFoundation/CoreFoundation.h>
#endif

static void *s_test_alloc_acquire(struct aws_allocator *allocator, size_t size) {
    (void)allocator;
    return (size > 0) ? malloc(size) : NULL;
}

static void s_test_alloc_release(struct aws_allocator *allocator, void *ptr) {
    (void)allocator;
    free(ptr);
}

static void *s_test_realloc(struct aws_allocator *allocator, void *ptr, size_t oldsize, size_t newsize) {
    (void)allocator;
    (void)oldsize;
    /* Realloc should ensure that newsize is never 0 */
    AWS_FATAL_ASSERT(newsize != 0);
    return realloc(ptr, newsize);
}

static void *s_test_calloc(struct aws_allocator *allocator, size_t num, size_t size) {
    (void)allocator;
    return (num > 0 && size > 0) ? calloc(num, size) : NULL;
}

/**
 * Check that we correctly protect against
 * https://wiki.sei.cmu.edu/confluence/display/c/MEM04-C.+Beware+of+zero-length+allocations
 * For now, can only test the realloc case, because it returns NULL on error
 * Test the remaining cases once https://github.com/awslabs/aws-c-common/issues/471 is solved
 */
AWS_TEST_CASE(test_alloc_nothing, s_test_alloc_nothing_fn)
static int s_test_alloc_nothing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_allocator test_allocator = {.mem_acquire = s_test_alloc_acquire,
                                           .mem_release = s_test_alloc_release,
                                           .mem_realloc = s_test_realloc,
                                           .mem_calloc = s_test_calloc};

    /* realloc should handle the case correctly, return null, and free the memory */
    void *p = aws_mem_acquire(&test_allocator, 12);
    ASSERT_SUCCESS(aws_mem_realloc(&test_allocator, &p, 12, 0));
    ASSERT_NULL(p);
    return 0;
}

/*
 * Small Block Allocator tests
 */
static int s_sba_alloc_free_once(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_allocator *sba = aws_small_block_allocator_new(allocator, false);
    void *mem = aws_mem_acquire(sba, 42);
    ASSERT_NOT_NULL(mem);
    const size_t allocated = aws_mem_tracer_bytes(allocator);
    ASSERT_TRUE(allocated > 0);
    aws_mem_release(sba, mem);
    aws_small_block_allocator_destroy(sba);

    return 0;
}
AWS_TEST_CASE(sba_alloc_free_once, s_sba_alloc_free_once)

#define NUM_TEST_ALLOCS 10000
#define NUM_TEST_THREADS 8
static int s_sba_random_allocs_and_frees(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_allocator *sba = aws_small_block_allocator_new(allocator, false);
    srand(42);

    void *allocs[NUM_TEST_ALLOCS];
    for (size_t count = 0; count < NUM_TEST_ALLOCS; ++count) {
        size_t size = aws_max_size(rand() % 512, 1);
        void *alloc = aws_mem_acquire(sba, size);
        ASSERT_NOT_NULL(alloc);
        allocs[count] = alloc;
    }

    for (size_t count = 0; count < NUM_TEST_ALLOCS; ++count) {
        void *alloc = allocs[count];
        aws_mem_release(sba, alloc);
    }

    aws_small_block_allocator_destroy(sba);

    return 0;
}
AWS_TEST_CASE(sba_random_allocs_and_frees, s_sba_random_allocs_and_frees)

static int s_sba_random_reallocs(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_allocator *sba = aws_small_block_allocator_new(allocator, false);
    srand(128);

    void *alloc = NULL;
    size_t size = 0;
    for (size_t count = 0; count < NUM_TEST_ALLOCS; ++count) {
        size_t old_size = size;
        size = rand() % 4096;
        ASSERT_SUCCESS(aws_mem_realloc(sba, &alloc, old_size, size));
    }
    ASSERT_SUCCESS(aws_mem_realloc(sba, &alloc, size, 0));

    aws_small_block_allocator_destroy(sba);

    return 0;
}
AWS_TEST_CASE(sba_random_reallocs, s_sba_random_reallocs)

struct sba_thread_test_data {
    struct aws_allocator *sba;
    uint32_t thread_idx;
};

static void s_sba_threaded_alloc_worker(void *user_data) {
    struct aws_allocator *sba = ((struct sba_thread_test_data *)user_data)->sba;

    void *allocs[NUM_TEST_ALLOCS];
    for (size_t count = 0; count < NUM_TEST_ALLOCS / NUM_TEST_THREADS; ++count) {
        size_t size = aws_max_size(rand() % 512, 1);
        void *alloc = aws_mem_acquire(sba, size);
        AWS_FATAL_ASSERT(alloc);
        allocs[count] = alloc;
    }

    for (size_t count = 0; count < NUM_TEST_ALLOCS / NUM_TEST_THREADS; ++count) {
        void *alloc = allocs[count];
        aws_mem_release(sba, alloc);
    }
}

static void s_sba_thread_test(struct aws_allocator *allocator, void (*thread_fn)(void *), struct aws_allocator *sba) {
    const struct aws_thread_options *thread_options = aws_default_thread_options();
    struct aws_thread threads[NUM_TEST_THREADS];
    struct sba_thread_test_data thread_data[NUM_TEST_THREADS];
    AWS_ZERO_ARRAY(threads);
    AWS_ZERO_ARRAY(thread_data);
    for (size_t thread_idx = 0; thread_idx < AWS_ARRAY_SIZE(threads); ++thread_idx) {
        struct aws_thread *thread = &threads[thread_idx];
        aws_thread_init(thread, allocator);
        struct sba_thread_test_data *data = &thread_data[thread_idx];
        data->sba = sba;
        data->thread_idx = (uint32_t)thread_idx;
        aws_thread_launch(thread, thread_fn, data, thread_options);
    }

    for (size_t thread_idx = 0; thread_idx < AWS_ARRAY_SIZE(threads); ++thread_idx) {
        struct aws_thread *thread = &threads[thread_idx];
        aws_thread_join(thread);
    }
}

static int s_sba_threaded_allocs_and_frees(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    srand(96);

    struct aws_allocator *sba = aws_small_block_allocator_new(allocator, true);

    s_sba_thread_test(allocator, s_sba_threaded_alloc_worker, sba);

    aws_small_block_allocator_destroy(sba);

    return 0;
}
AWS_TEST_CASE(sba_threaded_allocs_and_frees, s_sba_threaded_allocs_and_frees)

static void s_sba_threaded_realloc_worker(void *user_data) {
    struct sba_thread_test_data *thread_data = user_data;
    struct aws_allocator *sba = thread_data->sba;
    void *alloc = NULL;
    size_t size = 0;
    for (size_t count = 0; count < NUM_TEST_ALLOCS / NUM_TEST_THREADS; ++count) {
        size_t old_size = size;
        size = rand() % 1024;
        if (old_size) {
            AWS_FATAL_ASSERT(0 == memcmp(alloc, &thread_data->thread_idx, 1));
        }
        AWS_FATAL_ASSERT(0 == aws_mem_realloc(sba, &alloc, old_size, size));
        /* If there was a value, make sure it's still there */
        if (old_size && size) {
            AWS_FATAL_ASSERT(0 == memcmp(alloc, &thread_data->thread_idx, 1));
        }
        if (size) {
            memset(alloc, (int)thread_data->thread_idx, size);
        }
    }
    AWS_FATAL_ASSERT(0 == aws_mem_realloc(sba, &alloc, size, 0));
}

static int s_sba_threaded_reallocs(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    srand(12);

    struct aws_allocator *sba = aws_small_block_allocator_new(allocator, true);

    s_sba_thread_test(allocator, s_sba_threaded_realloc_worker, sba);

    aws_small_block_allocator_destroy(sba);

    return 0;
}
AWS_TEST_CASE(sba_threaded_reallocs, s_sba_threaded_reallocs)

static int s_sba_churn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    srand(9000);

    struct aws_array_list allocs;
    aws_array_list_init_dynamic(&allocs, allocator, NUM_TEST_ALLOCS, sizeof(void *));

    struct aws_allocator *sba = aws_small_block_allocator_new(allocator, false);

    size_t alloc_count = 0;
    while (alloc_count++ < NUM_TEST_ALLOCS * 10) {
        size_t size = aws_max_size(rand() % (2 * 4096), 1);
        void *alloc = aws_mem_acquire(sba, size);
        aws_array_list_push_back(&allocs, &alloc);

        /* randomly free a previous allocation, simulating the real world a bit */
        if ((rand() % allocs.length) > (allocs.length / 2)) {
            size_t idx = rand() % allocs.length;
            aws_array_list_get_at(&allocs, &alloc, idx);
            aws_array_list_erase(&allocs, idx);
            aws_mem_release(sba, alloc);
        }
    }

    /* free all remaining allocations */
    for (size_t idx = 0; idx < allocs.length; ++idx) {
        void *alloc = NULL;
        aws_array_list_get_at(&allocs, &alloc, idx);
        aws_mem_release(sba, alloc);
    }

    aws_array_list_clean_up(&allocs);

    aws_small_block_allocator_destroy(sba);

    return 0;
}
AWS_TEST_CASE(sba_churn, s_sba_churn)
