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

#include <aws/common/priority_queue.h>

#include <aws/testing/aws_test_harness.h>

#include <stdlib.h>

static int s_compare_ints(const void *a, const void *b) {
    int arg1 = *(const int *)a;
    int arg2 = *(const int *)b;

    if (arg1 < arg2) {
        return -1;
    }
    if (arg1 > arg2) {
        return 1;
    }
    return 0;
}

static int s_test_priority_queue_preserves_order(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_priority_queue queue;

    int err = aws_priority_queue_init_dynamic(&queue, allocator, 10, sizeof(int), s_compare_ints);
    ASSERT_SUCCESS(err, "Priority queue initialization failed with error %d", err);

    int first = 45, second = 67, third = 80, fourth = 120, fifth = 10000;

    ASSERT_SUCCESS(aws_priority_queue_push(&queue, &third), "Push operation failed for item %d", third);
    ASSERT_SUCCESS(aws_priority_queue_push(&queue, &fourth), "Push operation failed for item %d", fourth);
    ASSERT_SUCCESS(aws_priority_queue_push(&queue, &second), "Push operation failed for item %d", second);
    ASSERT_SUCCESS(aws_priority_queue_push(&queue, &fifth), "Push operation failed for item %d", fifth);
    ASSERT_SUCCESS(aws_priority_queue_push(&queue, &first), "Push operation failed for item %d", first);

    size_t num_elements = aws_priority_queue_size(&queue);
    ASSERT_INT_EQUALS(5, num_elements, "Priority queue size should have been %d but was %d", 5, num_elements);

    int pop_val, top_val, *top_val_ptr;
    err = aws_priority_queue_top(&queue, (void **)&top_val_ptr);
    ASSERT_SUCCESS(err, "Top operation failed with error %d", err);
    top_val = *top_val_ptr;
    err = aws_priority_queue_pop(&queue, &pop_val);
    ASSERT_SUCCESS(err, "Pop operation failed with error %d", err);
    ASSERT_INT_EQUALS(first, pop_val, "First element returned should have been %d but was %d", first, pop_val);
    ASSERT_INT_EQUALS(
        pop_val, top_val, "Popped element should have been the top element. expected %d but was %d", pop_val, top_val);

    err = aws_priority_queue_top(&queue, (void **)&top_val_ptr);
    ASSERT_SUCCESS(err, "Top operation failed with error %d", err);
    top_val = *top_val_ptr;
    err = aws_priority_queue_pop(&queue, &pop_val);
    ASSERT_SUCCESS(err, "Pop operation failed with error %d", err);
    ASSERT_INT_EQUALS(second, pop_val, "Second element returned should have been %d but was %d", second, pop_val);
    ASSERT_INT_EQUALS(
        pop_val, top_val, "Popped element should have been the top element. expected %d but was %d", pop_val, top_val);

    err = aws_priority_queue_top(&queue, (void **)&top_val_ptr);
    ASSERT_SUCCESS(err, "Top operation failed with error %d", err);
    top_val = *top_val_ptr;
    err = aws_priority_queue_pop(&queue, &pop_val);
    ASSERT_SUCCESS(err, "Pop operation failed with error %d", err);
    ASSERT_INT_EQUALS(third, pop_val, "Third element returned should have been %d but was %d", third, pop_val);
    ASSERT_INT_EQUALS(
        pop_val, top_val, "Popped element should have been the top element. expected %d but was %d", pop_val, top_val);

    err = aws_priority_queue_top(&queue, (void **)&top_val_ptr);
    ASSERT_SUCCESS(err, "Top operation failed with error %d", err);
    top_val = *top_val_ptr;
    err = aws_priority_queue_pop(&queue, &pop_val);
    ASSERT_SUCCESS(err, "Pop operation failed with error %d", err);
    ASSERT_INT_EQUALS(fourth, pop_val, "Fourth element returned should have been %d but was %d", fourth, pop_val);
    ASSERT_INT_EQUALS(
        pop_val, top_val, "Popped element should have been the top element. expected %d but was %d", pop_val, top_val);

    err = aws_priority_queue_top(&queue, (void **)&top_val_ptr);
    ASSERT_SUCCESS(err, "Top operation failed with error %d", err);
    top_val = *top_val_ptr;
    err = aws_priority_queue_pop(&queue, &pop_val);
    ASSERT_SUCCESS(err, "Pop operation failed with error %d", err);
    ASSERT_INT_EQUALS(fifth, pop_val, "Fifth element returned should have been %d but was %d", fifth, pop_val);
    ASSERT_INT_EQUALS(
        pop_val, top_val, "Popped element should have been the top element. expected %d but was %d", pop_val, top_val);

    ASSERT_ERROR(
        AWS_ERROR_PRIORITY_QUEUE_EMPTY,
        aws_priority_queue_pop(&queue, &pop_val),
        "Popping from empty queue should result in error");

    aws_priority_queue_clean_up(&queue);
    return 0;
}

static int s_test_priority_queue_random_values(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    enum { SIZE = 20 };
    struct aws_priority_queue queue;
    int storage[SIZE], err;
    aws_priority_queue_init_static(&queue, storage, SIZE, sizeof(int), s_compare_ints);
    int values[SIZE];
    srand((unsigned)(uintptr_t)&queue);
    for (int i = 0; i < SIZE; i++) {
        values[i] = rand() % 1000;
        err = aws_priority_queue_push(&queue, &values[i]);
        ASSERT_SUCCESS(err, "Push operation failed with error %d", err);
    }

    qsort(values, SIZE, sizeof(int), s_compare_ints);

    /* pop only half */
    for (int i = 0; i < SIZE / 2; i++) {
        int top;
        err = aws_priority_queue_pop(&queue, &top);
        ASSERT_SUCCESS(err, "Pop operation failed with error %d", err);
        ASSERT_INT_EQUALS(values[i], top, "Elements priority are out of order. Expected: %d Actual %d", values[i], top);
    }

    /* push new random values in that first half*/
    for (int i = 0; i < SIZE / 2; i++) {
        values[i] = rand() % 1000;
        err = aws_priority_queue_push(&queue, &values[i]);
        ASSERT_SUCCESS(err, "Push operation failed with error %d", err);
    }

    /* sort again so we can verify correct order on pop */
    qsort(values, SIZE, sizeof(int), s_compare_ints);
    /* pop all the queue */
    for (int i = 0; i < SIZE; i++) {
        int top;
        err = aws_priority_queue_pop(&queue, &top);
        ASSERT_SUCCESS(err, "Pop operation failed with error %d", err);
        ASSERT_INT_EQUALS(values[i], top, "Elements priority are out of order. Expected: %d Actual %d", values[i], top);
    }

    aws_priority_queue_clean_up(&queue);

    return 0;
}

static int s_test_priority_queue_size_and_capacity(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_priority_queue queue;
    int err = aws_priority_queue_init_dynamic(&queue, allocator, 5, sizeof(int), s_compare_ints);
    ASSERT_SUCCESS(err, "Dynamic init failed with error %d", err);
    size_t capacity = aws_priority_queue_capacity(&queue);
    ASSERT_INT_EQUALS(5, capacity, "Expected Capacity %d but was %d", 5, capacity);

    for (int i = 0; i < 15; i++) {
        err = aws_priority_queue_push(&queue, &i);
        ASSERT_SUCCESS(err, "Push operation failed with error %d", err);
    }

    size_t size = aws_priority_queue_size(&queue);
    ASSERT_INT_EQUALS(15, size, "Expected Size %d but was %d", 15, capacity);

    capacity = aws_priority_queue_capacity(&queue);
    ASSERT_INT_EQUALS(20, capacity, "Expected Capacity %d but was %d", 20, capacity);

    aws_priority_queue_clean_up(&queue);
    return 0;
}

#define ADD_ELEMS(pq, ...)                                                                                             \
    do {                                                                                                               \
        static int ADD_ELEMS_elems[] = {__VA_ARGS__};                                                                  \
        for (size_t ADD_ELEMS_i = 0; ADD_ELEMS_i < sizeof(ADD_ELEMS_elems) / sizeof(*ADD_ELEMS_elems);                 \
             ADD_ELEMS_i++) {                                                                                          \
            ASSERT_SUCCESS(aws_priority_queue_push(&(pq), &ADD_ELEMS_elems[ADD_ELEMS_i]));                             \
        }                                                                                                              \
    } while (0)

#define CHECK_ORDER(pq, ...)                                                                                           \
    do {                                                                                                               \
        static int CHECK_ORDER_elems[] = {__VA_ARGS__};                                                                \
        size_t CHECK_ORDER_count = sizeof(CHECK_ORDER_elems) / sizeof(*CHECK_ORDER_elems);                             \
        size_t CHECK_ORDER_i = 0;                                                                                      \
        int CHECK_ORDER_val;                                                                                           \
        while (aws_priority_queue_pop(&(pq), &CHECK_ORDER_val) == AWS_OP_SUCCESS) {                                    \
            ASSERT_TRUE(CHECK_ORDER_i < CHECK_ORDER_count);                                                            \
            ASSERT_INT_EQUALS(CHECK_ORDER_val, CHECK_ORDER_elems[CHECK_ORDER_i]);                                      \
            CHECK_ORDER_i++;                                                                                           \
        }                                                                                                              \
        ASSERT_INT_EQUALS(CHECK_ORDER_i, CHECK_ORDER_count);                                                           \
    } while (0)

static int s_test_remove_root(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_priority_queue queue;
    struct aws_priority_queue_node node = {12345};
    int val = 0;
    ASSERT_SUCCESS(aws_priority_queue_init_dynamic(&queue, allocator, 16, sizeof(int), s_compare_ints));

    ASSERT_SUCCESS(aws_priority_queue_push_ref(&queue, &val, &node));
    ADD_ELEMS(queue, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);

    val = 42;
    ASSERT_SUCCESS(aws_priority_queue_remove(&queue, &val, &node));
    ASSERT_INT_EQUALS(val, 0);
    ASSERT_ERROR(AWS_ERROR_PRIORITY_QUEUE_BAD_NODE, aws_priority_queue_remove(&queue, &val, &node));

    CHECK_ORDER(queue, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);

    aws_priority_queue_clean_up(&queue);

    return 0;
}

static int s_test_remove_leaf(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_priority_queue queue;
    struct aws_priority_queue_node node = {12345};
    ASSERT_SUCCESS(aws_priority_queue_init_dynamic(&queue, allocator, 16, sizeof(int), s_compare_ints));

    ADD_ELEMS(queue, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    int val = 16;
    ASSERT_SUCCESS(aws_priority_queue_push_ref(&queue, &val, &node));

    val = 42;
    ASSERT_SUCCESS(aws_priority_queue_remove(&queue, &val, &node));
    ASSERT_INT_EQUALS(val, 16);
    ASSERT_ERROR(AWS_ERROR_PRIORITY_QUEUE_BAD_NODE, aws_priority_queue_remove(&queue, &val, &node));

    CHECK_ORDER(queue, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    aws_priority_queue_clean_up(&queue);

    return 0;
}

/*
 * Here we force the heap to sift a value up to its parents when removing an interior node.
 *
 * 0
 *  20
 *   22
 *    222 <- Removed, swapped with 15
 *     2222
 *     2221
 *    221
 *     2212
 *     2211
 *   21
 *    212
 *     2122
 *     2121
 *    211
 *     2112
 *     2111
 *  1
 *   2
 *    3
 *     4
 *     5
 *    6
 *     7
 *     8
 *   9
 *    10
 *     11
 *     12
 *    13
 *     14
 *     15
 */
static int s_test_remove_interior_sift_up(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_priority_queue queue;
    struct aws_priority_queue_node node = {12345};
    ASSERT_SUCCESS(aws_priority_queue_init_dynamic(&queue, allocator, 16, sizeof(int), s_compare_ints));

    ADD_ELEMS(queue, 0, 20, 1, 22, 21, 2, 9);
    int val = 222;
    ASSERT_SUCCESS(aws_priority_queue_push_ref(&queue, &val, &node));
    ADD_ELEMS(
        queue, 221, 212, 211, 3, 6, 10, 13, 2222, 2221, 2212, 2211, 2122, 2121, 2112, 2111, 4, 5, 7, 8, 11, 12, 14, 15);

    val = 42;
    ASSERT_SUCCESS(aws_priority_queue_remove(&queue, &val, &node));
    ASSERT_INT_EQUALS(val, 222);
    ASSERT_ERROR(AWS_ERROR_PRIORITY_QUEUE_BAD_NODE, aws_priority_queue_remove(&queue, &val, &node));

    CHECK_ORDER(
        queue,
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
        15,
        20,
        21,
        22,
        211,
        212,
        221,
        /* 222, */ 2111,
        2112,
        2121,
        2122,
        2211,
        2212,
        2221,
        2222);

    aws_priority_queue_clean_up(&queue);

    return 0;
}

/*
 * Here we force the heap to sift a value down to a leaf when removing an interior node.
 *
 * 0
 *  1 <- Removed, swapped with 30
 *   2
 *    3
 *     4
 *     5
 *    6
 *     7
 *     8
 *   9
 *    10
 *     11
 *     12
 *    13
 *     14
 *     15
 *  16
 *   17
 *    18
 *     19
 *     20
 *    21
 *     22
 *     23
 *   24
 *    25
 *     26
 *     27
 *    28
 *     29
 *     30
 */
static int s_test_remove_interior_sift_down(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_priority_queue queue;
    struct aws_priority_queue_node node = {12345};
    ASSERT_SUCCESS(aws_priority_queue_init_dynamic(&queue, allocator, 16, sizeof(int), s_compare_ints));

    ADD_ELEMS(queue, 0);

    int val = 1;
    ASSERT_SUCCESS(aws_priority_queue_push_ref(&queue, &val, &node));

    ADD_ELEMS(
        queue,
        16,
        2,
        9,
        17,
        24,
        3,
        6,
        10,
        13,
        18,
        21,
        25,
        28,
        4,
        5,
        7,
        8,
        11,
        12,
        14,
        15,
        19,
        20,
        22,
        23,
        26,
        27,
        29,
        30);

    val = 42;
    ASSERT_SUCCESS(aws_priority_queue_remove(&queue, &val, &node));
    ASSERT_INT_EQUALS(val, 1);
    ASSERT_ERROR(AWS_ERROR_PRIORITY_QUEUE_BAD_NODE, aws_priority_queue_remove(&queue, &val, &node));

    CHECK_ORDER(
        queue,
        0,
        /* 1, */ 2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
        15,
        16,
        17,
        18,
        19,
        20,
        21,
        22,
        23,
        24,
        25,
        26,
        27,
        28,
        29,
        30);

    aws_priority_queue_clean_up(&queue);

    return 0;
}

AWS_TEST_CASE(priority_queue_remove_interior_sift_down_test, s_test_remove_interior_sift_down);
AWS_TEST_CASE(priority_queue_remove_interior_sift_up_test, s_test_remove_interior_sift_up);
AWS_TEST_CASE(priority_queue_remove_leaf_test, s_test_remove_leaf);
AWS_TEST_CASE(priority_queue_remove_root_test, s_test_remove_root);
AWS_TEST_CASE(priority_queue_push_pop_order_test, s_test_priority_queue_preserves_order);
AWS_TEST_CASE(priority_queue_random_values_test, s_test_priority_queue_random_values);
AWS_TEST_CASE(priority_queue_size_and_capacity_test, s_test_priority_queue_size_and_capacity);
