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

#include <aws/common/task_scheduler.h>
#include <aws/common/thread.h>
#include <aws/testing/aws_test_harness.h>

struct executed_task_data {
    struct aws_task *task;
    void *arg;
    enum aws_task_status status;
};

static struct executed_task_data s_executed_tasks[16];

static size_t s_executed_tasks_n;

/* Updates tl_executed_tasks and tl_executed_task_n when function is executed */
static void s_task_n_fn(struct aws_task *task, void *arg, enum aws_task_status status) {
    if (s_executed_tasks_n > AWS_ARRAY_SIZE(s_executed_tasks)) {
        AWS_ASSERT(0);
    }

    struct executed_task_data *data = &s_executed_tasks[s_executed_tasks_n++];
    data->task = task;
    data->arg = arg;
    data->status = status;
}

static int s_test_scheduler_ordering(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    s_executed_tasks_n = 0;

    struct aws_task_scheduler scheduler;
    aws_task_scheduler_init(&scheduler, allocator);

    struct aws_task task2;
    aws_task_init(&task2, s_task_n_fn, (void *)2, "scheduler_ordering_1");

    /* schedule 250 ns in the future. */
    uint64_t task2_timestamp = 250;
    aws_task_scheduler_schedule_future(&scheduler, &task2, task2_timestamp);

    struct aws_task task1;
    aws_task_init(&task1, s_task_n_fn, (void *)1, "scheduler_ordering_2");

    /* schedule now. */
    aws_task_scheduler_schedule_now(&scheduler, &task1);

    struct aws_task task3;
    aws_task_init(&task3, s_task_n_fn, (void *)3, "scheduler_ordering_3");

    /* schedule 500 ns in the future. */
    uint64_t task3_timestamp = 500;
    aws_task_scheduler_schedule_future(&scheduler, &task3, task3_timestamp);

    /* run tasks 1 and 2 (but not 3) */
    aws_task_scheduler_run_all(&scheduler, task2_timestamp);

    ASSERT_UINT_EQUALS(2, s_executed_tasks_n);

    struct executed_task_data *task_data = &s_executed_tasks[0];
    ASSERT_PTR_EQUALS(&task1, task_data->task);
    ASSERT_PTR_EQUALS(task1.arg, task_data->arg);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_RUN_READY, task_data->status);

    task_data = &s_executed_tasks[1];
    ASSERT_PTR_EQUALS(&task2, task_data->task);
    ASSERT_PTR_EQUALS(task2.arg, task_data->arg);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_RUN_READY, task_data->status);

    /* run task 3 */
    aws_task_scheduler_run_all(&scheduler, task3.timestamp);

    ASSERT_UINT_EQUALS(3, s_executed_tasks_n);

    task_data = &s_executed_tasks[2];
    ASSERT_PTR_EQUALS(&task3, task_data->task);
    ASSERT_PTR_EQUALS(task3.arg, task_data->arg);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_RUN_READY, task_data->status);

    aws_task_scheduler_clean_up(&scheduler);
    return 0;
}

static void s_null_fn(struct aws_task *task, void *arg, enum aws_task_status status) {
    (void)task;
    (void)arg;
    (void)status;
}

static int s_test_scheduler_has_tasks(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_task_scheduler scheduler;
    aws_task_scheduler_init(&scheduler, allocator);

    /* Check when no tasks scheduled */
    uint64_t next_task_time = 123456;
    ASSERT_FALSE(aws_task_scheduler_has_tasks(&scheduler, &next_task_time));
    ASSERT_UINT_EQUALS(UINT64_MAX, next_task_time);

    /* Check when a task is scheduled for the future */
    struct aws_task timed_task;
    aws_task_init(&timed_task, s_null_fn, (void *)1, "scheduler_has_tasks_1");

    aws_task_scheduler_schedule_future(&scheduler, &timed_task, 10);
    ASSERT_TRUE(aws_task_scheduler_has_tasks(&scheduler, &next_task_time));
    ASSERT_UINT_EQUALS(10, next_task_time);

    /* Check when a task is scheduled for now */
    struct aws_task now_task;
    aws_task_init(&now_task, s_null_fn, (void *)2, "scheduler_has_tasks_2");

    aws_task_scheduler_schedule_now(&scheduler, &now_task);
    ASSERT_TRUE(aws_task_scheduler_has_tasks(&scheduler, &next_task_time));
    ASSERT_UINT_EQUALS(0, next_task_time);

    aws_task_scheduler_clean_up(&scheduler);
    return 0;
}

static int s_test_scheduler_pops_task_fashionably_late(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    s_executed_tasks_n = 0;

    struct aws_task_scheduler scheduler;
    aws_task_scheduler_init(&scheduler, allocator);

    struct aws_task task;
    aws_task_init(&task, s_task_n_fn, (void *)0, "scheduler_pops_task_fashionably_late");

    aws_task_scheduler_schedule_future(&scheduler, &task, 10);

    /* Run scheduler before task is supposed to execute, check that it didn't execute */
    aws_task_scheduler_run_all(&scheduler, 5);

    ASSERT_UINT_EQUALS(0, s_executed_tasks_n);

    /* Run scheduler long after task was due to execute, check that it executed */
    aws_task_scheduler_run_all(&scheduler, 500);
    ASSERT_UINT_EQUALS(1, s_executed_tasks_n);

    struct executed_task_data *task_data = &s_executed_tasks[0];
    ASSERT_PTR_EQUALS(&task, task_data->task);
    ASSERT_PTR_EQUALS(task.arg, task_data->arg);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_RUN_READY, task_data->status);

    aws_task_scheduler_clean_up(&scheduler);
    return 0;
}

/* container for running a task that schedules another task when it executes */
struct task_scheduler_reentrancy_args {
    struct aws_task_scheduler *scheduler;
    struct aws_task task;
    bool executed;
    enum aws_task_status status;
    struct task_scheduler_reentrancy_args *next_task_args;
};

static void s_reentrancy_fn(struct aws_task *task, void *arg, enum aws_task_status status) {
    (void)task;
    struct task_scheduler_reentrancy_args *reentrancy_args = (struct task_scheduler_reentrancy_args *)arg;

    if (reentrancy_args->next_task_args) {
        aws_task_scheduler_schedule_now(reentrancy_args->scheduler, &reentrancy_args->next_task_args->task);
    }

    reentrancy_args->executed = 1;
    reentrancy_args->status = status;
}

static void s_reentrancy_args_init(
    struct task_scheduler_reentrancy_args *args,
    struct aws_task_scheduler *scheduler,
    struct task_scheduler_reentrancy_args *next_task_args) {

    AWS_ZERO_STRUCT(*args);
    args->scheduler = scheduler;
    aws_task_init(&args->task, s_reentrancy_fn, args, "scheduler_reentrancy");
    args->status = -1;
    args->next_task_args = next_task_args;
}

static int s_test_scheduler_reentrant_safe(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_task_scheduler scheduler;
    aws_task_scheduler_init(&scheduler, allocator);

    /* When task1 executes, it schedules task2 */
    struct task_scheduler_reentrancy_args task2_args;
    s_reentrancy_args_init(&task2_args, &scheduler, NULL);

    struct task_scheduler_reentrancy_args task1_args;
    s_reentrancy_args_init(&task1_args, &scheduler, &task2_args);

    aws_task_scheduler_schedule_now(&scheduler, &task1_args.task);

    /* Run, only task1 should have executed */
    aws_task_scheduler_run_all(&scheduler, 100);

    ASSERT_TRUE(task1_args.executed);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_RUN_READY, task1_args.status);

    ASSERT_FALSE(task2_args.executed);

    /* Run again, task2 should execute */
    aws_task_scheduler_run_all(&scheduler, 200);

    ASSERT_TRUE(task2_args.executed);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_RUN_READY, task2_args.status);

    aws_task_scheduler_clean_up(&scheduler);
    return 0;
}

struct cancellation_args {
    enum aws_task_status status;
};

static void s_cancellation_fn(struct aws_task *task, void *arg, enum aws_task_status status) {

    (void)task;
    struct cancellation_args *cancellation_args = (struct cancellation_args *)arg;

    cancellation_args->status = status;
}

static int s_test_scheduler_cleanup_cancellation(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_task_scheduler scheduler;
    aws_task_scheduler_init(&scheduler, allocator);

    struct cancellation_args now_task_args = {.status = 100000};
    struct aws_task now_task;
    aws_task_init(&now_task, s_cancellation_fn, &now_task_args, "scheduler_cleanup_cancellation_1");
    aws_task_scheduler_schedule_now(&scheduler, &now_task);

    struct cancellation_args future_task_args = {.status = 100000};
    struct aws_task future_task;
    aws_task_init(&future_task, s_cancellation_fn, &future_task_args, "scheduler_cleanup_cancellation_2");
    aws_task_scheduler_schedule_future(&scheduler, &future_task, 9999999999999);

    aws_task_scheduler_clean_up(&scheduler);

    ASSERT_INT_EQUALS(AWS_TASK_STATUS_CANCELED, now_task_args.status);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_CANCELED, future_task_args.status);
    return 0;
}

static int s_test_scheduler_cleanup_reentrants(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_task_scheduler scheduler;
    aws_task_scheduler_init(&scheduler, allocator);

    /* When now_task1 executes, it schedules now_task2 */
    struct task_scheduler_reentrancy_args now_task2_args;
    s_reentrancy_args_init(&now_task2_args, &scheduler, NULL);

    struct task_scheduler_reentrancy_args now_task1_args;
    s_reentrancy_args_init(&now_task1_args, &scheduler, &now_task2_args);

    aws_task_scheduler_schedule_now(&scheduler, &now_task1_args.task);

    /* When future_task1 executes, it schedules future_task2 */
    struct task_scheduler_reentrancy_args future_task2_args;
    s_reentrancy_args_init(&future_task2_args, &scheduler, NULL);

    struct task_scheduler_reentrancy_args future_task1_args;
    s_reentrancy_args_init(&future_task1_args, &scheduler, &future_task2_args);

    aws_task_scheduler_schedule_future(&scheduler, &future_task1_args.task, 555555555555555555);

    /* Clean up scheduler. All tasks should be executed with CANCELLED status */
    aws_task_scheduler_clean_up(&scheduler);

    ASSERT_INT_EQUALS(AWS_TASK_STATUS_CANCELED, now_task1_args.status);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_CANCELED, now_task2_args.status);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_CANCELED, future_task1_args.status);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_CANCELED, future_task2_args.status);

    return AWS_OP_SUCCESS;
}

/* Allocator that only works N times. Not at all thread safe. */
struct oom_allocator_impl {
    struct aws_allocator *alloc; /* normal underlying allocator */
    size_t num_allocations;
    size_t num_allocations_limit;
    size_t num_allocations_rejected;
};

static void *s_oom_allocator_acquire(struct aws_allocator *allocator, size_t size) {
    struct oom_allocator_impl *impl = allocator->impl;
    void *mem = NULL;

    if (impl->num_allocations < impl->num_allocations_limit) {
        mem = aws_mem_acquire(impl->alloc, size);
        if (mem) {
            impl->num_allocations++;
        }
    } else {
        impl->num_allocations_rejected++;
    }

    return mem;
}

static void s_oom_allocator_release(struct aws_allocator *allocator, void *ptr) {
    struct oom_allocator_impl *impl = allocator->impl;
    aws_mem_release(impl->alloc, ptr);
}

static struct aws_allocator *s_oom_allocator_new(struct aws_allocator *normal_allocator, size_t num_allocations_limit) {
    struct oom_allocator_impl *impl = aws_mem_acquire(normal_allocator, sizeof(struct oom_allocator_impl));
    AWS_ZERO_STRUCT(*impl);
    impl->alloc = normal_allocator;
    impl->num_allocations_limit = num_allocations_limit;

    struct aws_allocator *oom_allocator = aws_mem_acquire(normal_allocator, sizeof(struct aws_allocator));
    AWS_ZERO_STRUCT(*oom_allocator);
    oom_allocator->mem_acquire = s_oom_allocator_acquire;
    oom_allocator->mem_release = s_oom_allocator_release;
    oom_allocator->impl = impl;

    return oom_allocator;
}

static void s_oom_allocator_destroy(struct aws_allocator *oom_allocator) {
    struct oom_allocator_impl *impl = oom_allocator->impl;
    aws_mem_release(impl->alloc, oom_allocator);
    aws_mem_release(impl->alloc, impl);
}

static void s_oom_task_fn(struct aws_task *task, void *arg, enum aws_task_status status) {
    (void)status;
    struct aws_linked_list *done_list = arg;
    aws_linked_list_push_back(done_list, &task->node);
}

static int s_test_scheduler_oom_still_works(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    /* Create allocator for scheduler that limits how many allocations it can make.
     * Note that timed_queue is an array-list under the hood, so it only grabs memory at init and resize time */
    struct aws_allocator *oom_allocator = s_oom_allocator_new(allocator, 3); /* let timed_queue resize a few times */
    ASSERT_NOT_NULL(oom_allocator);
    struct oom_allocator_impl *oom_impl = oom_allocator->impl;

    struct aws_task_scheduler scheduler;
    ASSERT_SUCCESS(aws_task_scheduler_init(&scheduler, oom_allocator));

    /* Pass this to each task so it can insert itself when it's done */
    struct aws_linked_list done_tasks;
    aws_linked_list_init(&done_tasks);

    /* Create a bunch of tasks with random times, more tasks than the scheduler can fit in the timed_queue */
    size_t timed_queue_count = 0;
    size_t timed_list_count = 0;
    uint64_t highest_timestamp = 0;
    do {
        struct aws_task *task = aws_mem_acquire(allocator, sizeof(struct aws_task));
        ASSERT_NOT_NULL(task);
        aws_task_init(task, s_oom_task_fn, &done_tasks, "scheduler_oom_still_works1");

        size_t prev_rejects = oom_impl->num_allocations_rejected;

        /* add 1 to random time just so no future-tasks have same timestamp as now-tasks */
        uint64_t timestamp = (uint64_t)rand() + 1;
        if (timestamp > highest_timestamp) {
            highest_timestamp = timestamp;
        }

        aws_task_scheduler_schedule_future(&scheduler, task, timestamp);

        /* If scheduling causes a rejected allocation, then task was put on timed_list */
        if (prev_rejects < oom_impl->num_allocations_rejected) {
            ++timed_list_count;
        } else {
            ++timed_queue_count;
        }

        /* Keep going until there are twice as many tasks in timed_queue as in timed_list.
         * We do this exact ratio so that, when running tasks, at first the scheduler needs to choose between the two,
         * but eventually it's just picking from timed_queue. */
    } while (timed_list_count * 2 < timed_queue_count);

    /* Schedule some now-tasks as well */
    size_t now_count;
    for (now_count = 0; now_count < 10; ++now_count) {
        struct aws_task *task = aws_mem_acquire(allocator, sizeof(struct aws_task));
        ASSERT_NOT_NULL(task);
        aws_task_init(task, s_oom_task_fn, &done_tasks, "scheduler_oom_still_works2");

        aws_task_scheduler_schedule_now(&scheduler, task);
    }

    /* Run all tasks and clean up scheduler.
     * Run it in a few steps, just to stress the edge-cases */
    const uint64_t num_run_steps = 4;
    for (size_t run_i = 0; run_i < num_run_steps; ++run_i) {
        uint64_t timestamp = (highest_timestamp / num_run_steps) * run_i;
        aws_task_scheduler_run_all(&scheduler, timestamp);
    }
    aws_task_scheduler_run_all(&scheduler, UINT64_MAX); /* Run whatever's left */

    aws_task_scheduler_clean_up(&scheduler);

    /* Check that tasks ran in proper order */
    uint64_t done_task_count = 0;
    uint64_t prev_task_done_time = 0;
    while (!aws_linked_list_empty(&done_tasks)) {
        struct aws_task *task = AWS_CONTAINER_OF(aws_linked_list_pop_front(&done_tasks), struct aws_task, node);
        ASSERT_TRUE(
            prev_task_done_time <= task->timestamp,
            "Tasks ran in wrong order: %llu before %llu",
            prev_task_done_time,
            task->timestamp);
        aws_mem_release(allocator, task);

        done_task_count++;
    }

    size_t scheduled_task_count = now_count + timed_queue_count + timed_list_count;
    ASSERT_UINT_EQUALS(scheduled_task_count, done_task_count);

    s_oom_allocator_destroy(oom_allocator);
    return AWS_OP_SUCCESS;
}

struct task_cancelling_task_data {
    struct aws_task_scheduler *scheduler;
    struct aws_task *task_to_cancel;
};

static void s_task_cancelling_task(struct aws_task *task, void *arg, enum aws_task_status status) {
    s_task_n_fn(task, arg, status);
    struct task_cancelling_task_data *task_data = arg;
    aws_task_scheduler_cancel_task(task_data->scheduler, task_data->task_to_cancel);
}

static int s_test_scheduler_schedule_cancellation(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    s_executed_tasks_n = 0;

    struct aws_task_scheduler scheduler;
    aws_task_scheduler_init(&scheduler, allocator);

    struct aws_task task2;
    aws_task_init(&task2, s_task_n_fn, (void *)2, "scheduler_schedule_cancellation1");

    /* schedule 250 ns in the future. */
    uint64_t task2_timestamp = 250;
    aws_task_scheduler_schedule_future(&scheduler, &task2, task2_timestamp);

    struct aws_task task1;
    aws_task_init(&task1, s_task_n_fn, (void *)1, "scheduler_schedule_cancellation2");

    /* schedule now. */
    aws_task_scheduler_schedule_now(&scheduler, &task1);

    struct aws_task task5;
    aws_task_init(&task5, s_task_n_fn, (void *)3, "scheduler_schedule_cancellation5");

    /* schedule 500 ns in the future. */
    uint64_t task5_timestamp = 500;
    aws_task_scheduler_schedule_future(&scheduler, &task5, task5_timestamp);

    struct aws_task task4;
    aws_task_init(&task4, s_task_n_fn, (void *)5, "scheduler_schedule_cancellation4");

    struct task_cancelling_task_data task_cancel_data = {
        .scheduler = &scheduler,
        .task_to_cancel = &task4,
    };

    struct aws_task task3;

    aws_task_init(&task3, s_task_cancelling_task, &task_cancel_data, "scheduler_schedule_cancellation3");
    aws_task_scheduler_schedule_now(&scheduler, &task3);
    aws_task_scheduler_schedule_now(&scheduler, &task4);

    aws_task_scheduler_cancel_task(&scheduler, &task1);
    aws_task_scheduler_cancel_task(&scheduler, &task2);

    aws_task_scheduler_run_all(&scheduler, task5_timestamp);

    ASSERT_UINT_EQUALS(5, s_executed_tasks_n);

    struct executed_task_data *task_data = &s_executed_tasks[0];
    ASSERT_PTR_EQUALS(&task1, task_data->task);
    ASSERT_PTR_EQUALS(task1.arg, task_data->arg);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_CANCELED, task_data->status);

    task_data = &s_executed_tasks[1];
    ASSERT_PTR_EQUALS(&task2, task_data->task);
    ASSERT_PTR_EQUALS(task2.arg, task_data->arg);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_CANCELED, task_data->status);

    task_data = &s_executed_tasks[2];
    ASSERT_PTR_EQUALS(&task3, task_data->task);
    ASSERT_PTR_EQUALS(task3.arg, task_data->arg);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_RUN_READY, task_data->status);

    task_data = &s_executed_tasks[3];
    ASSERT_PTR_EQUALS(&task4, task_data->task);
    ASSERT_PTR_EQUALS(task4.arg, task_data->arg);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_CANCELED, task_data->status);

    task_data = &s_executed_tasks[4];
    ASSERT_PTR_EQUALS(&task5, task_data->task);
    ASSERT_PTR_EQUALS(task5.arg, task_data->arg);
    ASSERT_INT_EQUALS(AWS_TASK_STATUS_RUN_READY, task_data->status);

    aws_task_scheduler_clean_up(&scheduler);
    return 0;
}

static int s_test_scheduler_cleanup_idempotent(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_task_scheduler scheduler;
    ASSERT_SUCCESS(aws_task_scheduler_init(&scheduler, allocator));
    aws_task_scheduler_clean_up(&scheduler);
    aws_task_scheduler_clean_up(&scheduler);
    return 0;
}

static int s_test_scheduler_oom_during_init(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_allocator *oom_allocator = s_oom_allocator_new(allocator, 0);
    ASSERT_NOT_NULL(oom_allocator);

    struct aws_task_scheduler scheduler;
    ASSERT_ERROR(AWS_ERROR_OOM, aws_task_scheduler_init(&scheduler, oom_allocator));
    aws_task_scheduler_clean_up(&scheduler);

    s_oom_allocator_destroy(oom_allocator);
    return 0;
}

AWS_TEST_CASE(scheduler_pops_task_late_test, s_test_scheduler_pops_task_fashionably_late);
AWS_TEST_CASE(scheduler_ordering_test, s_test_scheduler_ordering);
AWS_TEST_CASE(scheduler_has_tasks_test, s_test_scheduler_has_tasks);
AWS_TEST_CASE(scheduler_reentrant_safe, s_test_scheduler_reentrant_safe);
AWS_TEST_CASE(scheduler_cleanup_cancellation, s_test_scheduler_cleanup_cancellation);
AWS_TEST_CASE(scheduler_cleanup_reentrants, s_test_scheduler_cleanup_reentrants);
AWS_TEST_CASE(scheduler_oom_still_works, s_test_scheduler_oom_still_works);
AWS_TEST_CASE(scheduler_schedule_cancellation, s_test_scheduler_schedule_cancellation);
AWS_TEST_CASE(scheduler_cleanup_idempotent, s_test_scheduler_cleanup_idempotent);
AWS_TEST_CASE(scheduler_oom_during_init, s_test_scheduler_oom_during_init);
