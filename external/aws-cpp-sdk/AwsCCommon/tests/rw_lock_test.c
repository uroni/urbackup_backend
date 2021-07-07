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

#include <aws/common/rw_lock.h>

#include <aws/common/thread.h>
#include <aws/testing/aws_test_harness.h>

static int s_test_rw_lock_acquire_release(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_rw_lock rw_lock;
    aws_rw_lock_init(&rw_lock);

    ASSERT_SUCCESS(aws_rw_lock_wlock(&rw_lock), "rw_lock acquire should have returned success.");
    ASSERT_SUCCESS(aws_rw_lock_wunlock(&rw_lock), "rw_lock release should have returned success.");

    ASSERT_SUCCESS(aws_rw_lock_rlock(&rw_lock), "rw_lock acquire should have returned success.");
    ASSERT_SUCCESS(aws_rw_lock_runlock(&rw_lock), "rw_lock release should have returned success.");

    aws_rw_lock_clean_up(&rw_lock);

    return 0;
}
AWS_TEST_CASE(rw_lock_aquire_release_test, s_test_rw_lock_acquire_release)

struct thread_rw_lock_data {
    struct aws_rw_lock rw_lock;
    volatile int counter;
    int max_counts;
    volatile int thread_fn_increments;
};

static void s_rw_lock_thread_fn(void *rw_lock_data) {
    struct thread_rw_lock_data *p_rw_lock = (struct thread_rw_lock_data *)rw_lock_data;
    int finished = 0;
    while (!finished) {

        aws_rw_lock_rlock(&p_rw_lock->rw_lock);

        if (p_rw_lock->counter != p_rw_lock->max_counts) {
            int counter = p_rw_lock->counter + 1;

            aws_rw_lock_runlock(&p_rw_lock->rw_lock);
            aws_rw_lock_wlock(&p_rw_lock->rw_lock);

            p_rw_lock->counter = counter;
            p_rw_lock->thread_fn_increments += 1;

            aws_rw_lock_wunlock(&p_rw_lock->rw_lock);
            aws_rw_lock_rlock(&p_rw_lock->rw_lock);

            finished = p_rw_lock->counter == p_rw_lock->max_counts;
        } else {
            finished = 1;
        }

        aws_rw_lock_runlock(&p_rw_lock->rw_lock);
    }
}

static int s_test_rw_lock_is_actually_rw_lock(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct thread_rw_lock_data rw_lock_data = {
        .counter = 0,
        .max_counts = 1000000,
        .thread_fn_increments = 0,
    };

    aws_rw_lock_init(&rw_lock_data.rw_lock);

    struct aws_thread thread;
    aws_thread_init(&thread, allocator);
    ASSERT_SUCCESS(
        aws_thread_launch(&thread, s_rw_lock_thread_fn, &rw_lock_data, 0),
        "thread creation failed with error %d",
        aws_last_error());
    int finished = 0;
    while (!finished) {

        aws_rw_lock_rlock(&rw_lock_data.rw_lock);

        finished = rw_lock_data.counter == rw_lock_data.max_counts;

        aws_rw_lock_runlock(&rw_lock_data.rw_lock);
    }

    ASSERT_SUCCESS(aws_thread_join(&thread), "Thread join failed with error code %d.", aws_last_error());
    ASSERT_INT_EQUALS(
        rw_lock_data.thread_fn_increments, rw_lock_data.max_counts, "Thread 2 should have written all data");
    ASSERT_INT_EQUALS(
        rw_lock_data.max_counts, rw_lock_data.counter, "Both threads should have written exactly the max counts.");

    aws_thread_clean_up(&thread);
    aws_rw_lock_clean_up(&rw_lock_data.rw_lock);

    return 0;
}
AWS_TEST_CASE(rw_lock_is_actually_rw_lock_test, s_test_rw_lock_is_actually_rw_lock)

static int s_iterations = 0;
static void s_thread_reader_fn(void *ud) {

    struct aws_rw_lock *lock = ud;

    int finished = 0;
    while (!finished) {

        aws_rw_lock_rlock(lock);

        finished = s_iterations == 10000;

        aws_rw_lock_runlock(lock);
    }
}

static int s_test_rw_lock_many_readers(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_rw_lock lock;
    aws_rw_lock_init(&lock);

    struct aws_thread threads[2];
    AWS_ZERO_ARRAY(threads);

    for (size_t i = 0; i < AWS_ARRAY_SIZE(threads); ++i) {

        aws_thread_init(&threads[i], allocator);
        ASSERT_SUCCESS(
            aws_thread_launch(&threads[i], s_thread_reader_fn, &lock, 0),
            "thread creation failed with error %d",
            aws_last_error());
    }

    int finished = 0;
    while (!finished) {

        aws_rw_lock_wlock(&lock);

        finished = ++s_iterations == 10000;

        aws_rw_lock_wunlock(&lock);
    }

    for (size_t i = 0; i < AWS_ARRAY_SIZE(threads); ++i) {

        ASSERT_SUCCESS(aws_thread_join(&threads[i]), "Thread join failed with error code %d.", aws_last_error());
        aws_thread_clean_up(&threads[i]);
    }

    aws_rw_lock_clean_up(&lock);

    return 0;
}
AWS_TEST_CASE(rw_lock_many_readers_test, s_test_rw_lock_many_readers)
