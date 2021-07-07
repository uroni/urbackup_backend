/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/testing/aws_test_allocators.h>

static int s_test_timebomb_allocator(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    struct aws_allocator timebomb;
    ASSERT_SUCCESS(aws_timebomb_allocator_init(&timebomb, allocator, 2));

    /* Should have two successful allocations, then failures. */
    void *one = aws_mem_acquire(&timebomb, 1);
    ASSERT_NOT_NULL(one);

    void *two = aws_mem_calloc(&timebomb, 1, 1);
    ASSERT_NOT_NULL(two);

    ASSERT_NULL(aws_mem_acquire(&timebomb, 1));
    ASSERT_NULL(aws_mem_acquire(&timebomb, 1));

    /* Releasing memory should not stop the allocations from failing. */
    aws_mem_release(&timebomb, one);
    ASSERT_NULL(aws_mem_acquire(&timebomb, 1));

    /* Reset should allow allocations to succeed again (until bomb goes off). */
    aws_timebomb_allocator_reset_countdown(&timebomb, 1);
    one = aws_mem_acquire(&timebomb, 1);
    ASSERT_NOT_NULL(one);

    ASSERT_NULL(aws_mem_acquire(&timebomb, 1));

    /* Clean up */
    aws_mem_release(&timebomb, one);
    aws_mem_release(&timebomb, two);
    aws_timebomb_allocator_clean_up(&timebomb);
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(timebomb_allocator, s_test_timebomb_allocator);
