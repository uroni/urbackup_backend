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

#include <aws/common/common.h>

#include <aws/testing/aws_test_harness.h>

#ifdef __MACH__
#    include <CoreFoundation/CoreFoundation.h>
#endif

static size_t s_alloc_counter, s_alloc_total_size, s_call_ct_malloc, s_call_ct_free, s_call_ct_realloc;

static void *s_test_alloc_acquire(struct aws_allocator *allocator, size_t size) {
    (void)allocator;

    s_alloc_counter++;
    s_call_ct_malloc++;
    s_alloc_total_size += size;

    uint8_t *buf = malloc(size + 16);
    *(size_t *)buf = size;
    buf += 16;
    return buf;
}

static void s_test_alloc_release(struct aws_allocator *allocator, void *ptr) {
    (void)allocator;

    uint8_t *buf = ptr;
    s_call_ct_free++;

    buf -= 16;
    size_t old_size = *(size_t *)buf;
    s_alloc_counter--;
    s_alloc_total_size -= old_size;

    free(buf);
}

static size_t s_original_size, s_reported_oldsize;

static void *s_test_realloc(struct aws_allocator *allocator, void *ptr, size_t oldsize, size_t newsize) {
    (void)allocator;

    uint8_t *buf = ptr;
    buf -= 16;
    s_call_ct_realloc++;

    s_original_size = *(size_t *)buf;
    s_reported_oldsize = oldsize;

    /* Always pick a new pointer for test purposes */
    void *newbuf = malloc(newsize);
    if (!newbuf) {
        abort();
    }

    memcpy(newbuf, buf, 16 + (oldsize > newsize ? newsize : oldsize));
    free(buf);
    buf = newbuf;

    *(size_t *)buf = newsize;
    s_alloc_total_size += (newsize - oldsize);

    return buf + 16;
}

static void *s_test_malloc_failing(struct aws_allocator *allocator, size_t size) {
    (void)allocator;
    (void)size;
    return NULL;
}

static void *s_test_realloc_failing(struct aws_allocator *allocator, void *ptr, size_t oldsize, size_t newsize) {
    (void)allocator;
    (void)ptr;
    (void)oldsize;
    (void)newsize;
    return NULL;
}

static const uint8_t TEST_PATTERN[32] = {0xa5, 0x41, 0xcb, 0xe7, 0x00, 0x19, 0xd9, 0xf3, 0x60, 0x4a, 0x2b,
                                         0x68, 0x55, 0x46, 0xb7, 0xe0, 0x74, 0x91, 0x2a, 0xbe, 0x5e, 0x41,
                                         0x06, 0x39, 0x02, 0x02, 0xf6, 0x79, 0x1c, 0x4a, 0x08, 0xa9};

AWS_TEST_CASE(test_realloc_fallback, s_test_realloc_fallback_fn)
static int s_test_realloc_fallback_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_allocator test_allocator = {
        .mem_acquire = s_test_alloc_acquire,
        .mem_release = s_test_alloc_release,
        .mem_realloc = NULL,
    };

    s_call_ct_malloc = s_call_ct_free = s_call_ct_realloc = 0;

    void *buf = aws_mem_acquire(&test_allocator, 32);
    void *oldbuf = buf;
    memcpy(buf, TEST_PATTERN, 32);
    ASSERT_SUCCESS(aws_mem_realloc(&test_allocator, &buf, 32, 64));
    ASSERT_INT_EQUALS(s_call_ct_malloc, 2);
    ASSERT_INT_EQUALS(s_call_ct_free, 1);
    ASSERT_INT_EQUALS(s_alloc_counter, 1);
    ASSERT_INT_EQUALS(s_alloc_total_size, 64);
    ASSERT_INT_EQUALS(memcmp(buf, TEST_PATTERN, 32), 0);
    ASSERT_FALSE(buf == oldbuf);

    aws_mem_release(&test_allocator, buf);

    return 0;
}

AWS_TEST_CASE(test_realloc_fallback_oom, s_test_realloc_fallback_oom_fn)
static int s_test_realloc_fallback_oom_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_allocator test_allocator = {
        .mem_acquire = s_test_alloc_acquire,
        .mem_release = s_test_alloc_release,
        .mem_realloc = NULL,
    };

    s_call_ct_malloc = s_call_ct_free = s_call_ct_realloc = 0;
    void *buf = aws_mem_acquire(&test_allocator, 32);
    void *oldbuf = buf;

    test_allocator.mem_acquire = s_test_malloc_failing;

    ASSERT_ERROR(AWS_ERROR_OOM, aws_mem_realloc(&test_allocator, &buf, 32, 64));
    ASSERT_INT_EQUALS(s_call_ct_free, 0);
    ASSERT_PTR_EQUALS(buf, oldbuf);

    aws_mem_release(&test_allocator, buf);

    return 0;
}

AWS_TEST_CASE(test_realloc_passthrough_oom, s_test_realloc_passthrough_oom_fn)
static int s_test_realloc_passthrough_oom_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_allocator test_allocator = {
        .mem_acquire = s_test_alloc_acquire,
        .mem_release = s_test_alloc_release,
        .mem_realloc = s_test_realloc_failing,
    };

    s_call_ct_malloc = s_call_ct_free = s_call_ct_realloc = 0;

    void *buf = aws_mem_acquire(&test_allocator, 32);
    void *oldbuf = buf;
    memcpy(buf, TEST_PATTERN, 32);

    ASSERT_ERROR(AWS_ERROR_OOM, aws_mem_realloc(&test_allocator, &buf, 32, 64));
    ASSERT_PTR_EQUALS(buf, oldbuf);

    aws_mem_release(&test_allocator, buf);

    return 0;
}

AWS_TEST_CASE(test_realloc_passthrough, s_test_realloc_passthrough_fn)
static int s_test_realloc_passthrough_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_allocator test_allocator = {
        .mem_acquire = s_test_alloc_acquire,
        .mem_release = s_test_alloc_release,
        .mem_realloc = s_test_realloc,
    };

    s_call_ct_malloc = s_call_ct_free = s_call_ct_realloc = 0;

    void *buf = aws_mem_acquire(&test_allocator, 32);
    void *oldbuf = buf;
    memcpy(buf, TEST_PATTERN, 32);

    ASSERT_SUCCESS(aws_mem_realloc(&test_allocator, &buf, 32, 64));
    ASSERT_INT_EQUALS(memcmp(buf, TEST_PATTERN, 32), 0);
    ASSERT_INT_EQUALS(s_reported_oldsize, 32);
    ASSERT_INT_EQUALS(s_original_size, 32);
    ASSERT_INT_EQUALS(s_call_ct_malloc, 1);
    ASSERT_INT_EQUALS(s_call_ct_free, 0);
    ASSERT_FALSE(buf == oldbuf);

    aws_mem_release(&test_allocator, buf);

    return 0;
}

AWS_TEST_CASE(test_cf_allocator_wrapper, s_test_cf_allocator_wrapper_fn)

static int s_test_cf_allocator_wrapper_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

#ifdef __MACH__
    CFAllocatorRef cf_allocator = aws_wrapped_cf_allocator_new(allocator);
    ASSERT_NOT_NULL(cf_allocator);
    char test_prefix[] = "test_string";
    CFStringRef test_str = CFStringCreateWithCString(cf_allocator, test_prefix, kCFStringEncodingUTF8);

    ASSERT_NOT_NULL(test_str);
    /* NOLINTNEXTLINE */
    ASSERT_BIN_ARRAYS_EQUALS(
        test_prefix,
        sizeof(test_prefix) - 1,
        CFStringGetCStringPtr(test_str, kCFStringEncodingUTF8),
        CFStringGetLength(test_str));

    CFRelease(test_str);
    aws_wrapped_cf_allocator_destroy(cf_allocator);
#endif

    return 0;
}

AWS_TEST_CASE(test_acquire_many, s_test_acquire_many_fn)
static int s_test_acquire_many_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    void *a = NULL;
    void *b = NULL;
    void *buffer = aws_mem_acquire_many(allocator, 2, &a, (size_t)64, &b, (size_t)64);

    ASSERT_NOT_NULL(buffer);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);

    ASSERT_UINT_EQUALS((uintptr_t)a, (uintptr_t)buffer);
    ASSERT_UINT_EQUALS((uintptr_t)b, (uintptr_t)buffer + 64);
    ASSERT_UINT_EQUALS((uintptr_t)a % sizeof(intmax_t), 0);
    ASSERT_UINT_EQUALS((uintptr_t)b % sizeof(intmax_t), 0);

    aws_mem_release(allocator, buffer);
    a = NULL;
    b = NULL;

    buffer = aws_mem_acquire_many(allocator, 2, &a, (size_t)1, &b, (size_t)1);

    ASSERT_NOT_NULL(buffer);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);

    ASSERT_UINT_EQUALS((uintptr_t)a, (uintptr_t)buffer);
    ASSERT_UINT_EQUALS((uintptr_t)b, (uintptr_t)buffer + sizeof(intmax_t));
    ASSERT_UINT_EQUALS((uintptr_t)a % sizeof(intmax_t), 0);
    ASSERT_UINT_EQUALS((uintptr_t)b % sizeof(intmax_t), 0);

    aws_mem_release(allocator, buffer);

    return 0;
}
