/*
 *  Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License").
 *  You may not use this file except in compliance with the License.
 *  A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 *  or in the "license" file accompanying this file. This file is distributed
 *  on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied. See the License for the specific language governing
 *  permissions and limitations under the License.
 */

#include <aws/common/thread.h>

#include <aws/testing/aws_test_harness.h>

struct thread_test_data {
    aws_thread_id_t thread_id;
};

static void s_thread_fn(void *arg) {
    struct thread_test_data *test_data = (struct thread_test_data *)arg;
    test_data->thread_id = aws_thread_current_thread_id();
}

static int s_test_thread_creation_join_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    struct thread_test_data test_data;

    struct aws_thread thread;
    aws_thread_init(&thread, allocator);

    ASSERT_SUCCESS(aws_thread_launch(&thread, s_thread_fn, (void *)&test_data, 0), "thread creation failed");
    ASSERT_INT_EQUALS(
        AWS_THREAD_JOINABLE, aws_thread_get_detach_state(&thread), "thread state should have returned JOINABLE");
    ASSERT_SUCCESS(aws_thread_join(&thread), "thread join failed");
    ASSERT_TRUE(
        aws_thread_thread_id_equal(test_data.thread_id, aws_thread_get_id(&thread)),
        "get_thread_id should have returned the same id as the thread calling current_thread_id");
    ASSERT_INT_EQUALS(
        AWS_THREAD_JOIN_COMPLETED,
        aws_thread_get_detach_state(&thread),
        "thread state should have returned JOIN_COMPLETED");

    aws_thread_clean_up(&thread);

    return 0;
}

AWS_TEST_CASE(thread_creation_join_test, s_test_thread_creation_join_fn)

static uint32_t s_atexit_call_count = 0;
static void s_thread_atexit_fn(void *user_data) {
    (void)user_data;
    AWS_FATAL_ASSERT(s_atexit_call_count == 0);
    s_atexit_call_count = 1;
}

static void s_thread_atexit_fn2(void *user_data) {
    (void)user_data;
    AWS_FATAL_ASSERT(s_atexit_call_count == 1);
    s_atexit_call_count = 2;
}

static void s_thread_worker_with_atexit(void *arg) {
    (void)arg;
    AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_thread_current_at_exit(s_thread_atexit_fn2, NULL));
    AWS_FATAL_ASSERT(AWS_OP_SUCCESS == aws_thread_current_at_exit(s_thread_atexit_fn, NULL));
}

static int s_test_thread_atexit(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    struct aws_thread thread;
    ASSERT_SUCCESS(aws_thread_init(&thread, allocator));

    ASSERT_SUCCESS(aws_thread_launch(&thread, s_thread_worker_with_atexit, NULL, 0), "thread creation failed");
    ASSERT_SUCCESS(aws_thread_join(&thread), "thread join failed");

    ASSERT_INT_EQUALS(2, s_atexit_call_count);

    aws_thread_clean_up(&thread);

    return 0;
}

AWS_TEST_CASE(thread_atexit_test, s_test_thread_atexit)
