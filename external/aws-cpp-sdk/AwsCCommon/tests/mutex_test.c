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

#include <aws/common/mutex.h>

#include <aws/common/thread.h>
#include <aws/testing/aws_test_harness.h>

static int s_test_mutex_acquire_release(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_mutex mutex;
    aws_mutex_init(&mutex);

    ASSERT_SUCCESS(aws_mutex_lock(&mutex), "Mutex acquire should have returned success.");
    ASSERT_SUCCESS(aws_mutex_unlock(&mutex), "Mutex release should have returned success.");

    aws_mutex_clean_up(&mutex);

    return 0;
}

struct thread_mutex_data {
    struct aws_mutex mutex;
    volatile int counter;
    int max_counts;
    volatile int thread_fn_increments;
};

static void s_mutex_thread_fn(void *mutex_data) {
    struct thread_mutex_data *p_mutex = (struct thread_mutex_data *)mutex_data;
    int finished = 0;
    while (!finished) {
        aws_mutex_lock(&p_mutex->mutex);
        if (p_mutex->counter != p_mutex->max_counts) {
            int counter = p_mutex->counter + 1;
            p_mutex->counter = counter;
            p_mutex->thread_fn_increments += 1;
            finished = p_mutex->counter == p_mutex->max_counts;
        } else {
            finished = 1;
        }
        aws_mutex_unlock(&p_mutex->mutex);
    }
}

static int s_test_mutex_is_actually_mutex(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct thread_mutex_data mutex_data = {
        .counter = 0,
        .max_counts = 1000000,
        .thread_fn_increments = 0,
    };

    aws_mutex_init(&mutex_data.mutex);

    struct aws_thread thread;
    aws_thread_init(&thread, allocator);
    ASSERT_SUCCESS(
        aws_thread_launch(&thread, s_mutex_thread_fn, &mutex_data, 0),
        "thread creation failed with error %d",
        aws_last_error());
    int finished = 0;
    int increments = 0;
    while (!finished) {
        aws_mutex_lock(&mutex_data.mutex);
        /*add some fairness for thread startup time.*/
        if (!mutex_data.thread_fn_increments) {
            aws_mutex_unlock(&mutex_data.mutex);
            continue;
        }

        if (mutex_data.counter != mutex_data.max_counts) {
            increments += 1;
            int counter = mutex_data.counter + 1;
            mutex_data.counter = counter;
            finished = mutex_data.counter == mutex_data.max_counts;
        } else {
            finished = 1;
        }
        aws_mutex_unlock(&mutex_data.mutex);
    }

    ASSERT_SUCCESS(aws_thread_join(&thread), "Thread join failed with error code %d.", aws_last_error());
    ASSERT_TRUE(mutex_data.thread_fn_increments > 0, "Thread 2 should have written some");
    ASSERT_TRUE(increments > 0, "Thread 1 should have written some");
    ASSERT_INT_EQUALS(
        mutex_data.max_counts, mutex_data.counter, "Both threads should have written exactly the max counts.");
    ASSERT_INT_EQUALS(
        mutex_data.counter,
        mutex_data.thread_fn_increments + increments,
        "Both threads should have written up to the max count");

    aws_thread_clean_up(&thread);
    aws_mutex_clean_up(&mutex_data.mutex);

    return 0;
}

AWS_TEST_CASE(mutex_aquire_release_test, s_test_mutex_acquire_release)
AWS_TEST_CASE(mutex_is_actually_mutex_test, s_test_mutex_is_actually_mutex)
