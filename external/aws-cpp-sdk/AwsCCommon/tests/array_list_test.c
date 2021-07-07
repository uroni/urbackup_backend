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

#include <aws/common/array_list.h>

#include <aws/common/string.h>
#include <aws/testing/aws_test_harness.h>

static int s_array_list_order_push_back_pop_front_fn(struct aws_allocator *allocator, void *ctx) {

    (void)ctx;

    struct aws_array_list list;

    size_t list_size = 4;
    int first = 1, second = 2, third = 3, fourth = 4;

    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, list_size, sizeof(int)),
        "List setup should have been successful. err code %d",
        aws_last_error());
    ASSERT_INT_EQUALS(0, list.length, "List size should be 0.");
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&first), "List push failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&second), "List push failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&third), "List push failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&fourth), "List push failed with error code %d", aws_last_error());

    ASSERT_INT_EQUALS(list_size, list.length, "List size should be %d.", (int)list_size);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    int item = 0;
    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(first, item, "Item should have been the first item.");
    ASSERT_INT_EQUALS(list_size - 1, list.length, "List size should be %d.", (int)list_size - 1);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(second, item, "Item should have been the second item.");
    ASSERT_INT_EQUALS(list_size - 2, list.length, "List size should be %d.", (int)list_size - 2);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(third, item, "Item should have been the third item.");
    ASSERT_INT_EQUALS(list_size - 3, list.length, "List size should be %d.", (int)list_size - 3);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(fourth, item, "Item should have been the fourth item.");
    ASSERT_INT_EQUALS(list_size - 4, list.length, "List size should be %d.", (int)list_size - 4);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_order_push_back_pop_front_test, s_array_list_order_push_back_pop_front_fn)

static int s_array_list_order_push_back_pop_back_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;

    static size_t list_size = 4;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());

    int first = 1, second = 2, third = 3, fourth = 4;

    ASSERT_INT_EQUALS(0, list.length, "List size should be 0.");
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&first), "List push failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&second), "List push failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&third), "List push failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&fourth), "List push failed with error code %d", aws_last_error());

    ASSERT_INT_EQUALS(list_size, list.length, "List size should be %d.", (int)list_size);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    int item = 0;
    ASSERT_SUCCESS(aws_array_list_back(&list, (void *)&item), "List back failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_back(&list), "List pop back failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(fourth, item, "Item should have been the fourth item.");
    ASSERT_INT_EQUALS(list_size - 1, list.length, "List size should be %d.", (int)list_size - 4);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(aws_array_list_back(&list, (void *)&item), "List back failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_back(&list), "List pop back failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(third, item, "Item should have been the third item.");
    ASSERT_INT_EQUALS(list_size - 2, list.length, "List size should be %d.", (int)list_size - 3);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(aws_array_list_back(&list, (void *)&item), "List back failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_back(&list), "List pop back failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(second, item, "Item should have been the second item.");
    ASSERT_INT_EQUALS(list_size - 3, list.length, "List size should be %d.", (int)list_size - 2);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(aws_array_list_back(&list, (void *)&item), "List back failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_back(&list), "List pop back failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(first, item, "Item should have been the first item.");
    ASSERT_INT_EQUALS(list_size - 4, list.length, "List size should be %d.", (int)list_size - 1);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_order_push_back_pop_back_test, s_array_list_order_push_back_pop_back_fn)

static int s_array_list_pop_front_n_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;

    ASSERT_SUCCESS(aws_array_list_init_dynamic(&list, allocator, 8, sizeof(int)));

    int first = 1, second = 2, third = 3, fourth = 4;
    int item = 0;
    ASSERT_SUCCESS(aws_array_list_push_back(&list, (void *)&first));
    ASSERT_SUCCESS(aws_array_list_push_back(&list, (void *)&second));
    ASSERT_SUCCESS(aws_array_list_push_back(&list, (void *)&third));
    ASSERT_SUCCESS(aws_array_list_push_back(&list, (void *)&fourth));

    /* Popping 0 front elements should have no effect */
    aws_array_list_pop_front_n(&list, 0);
    ASSERT_INT_EQUALS(4, aws_array_list_length(&list));

    /* Pop 2/4 front elements. Third item should be in front. */
    aws_array_list_pop_front_n(&list, 2);
    ASSERT_INT_EQUALS(2, aws_array_list_length(&list));
    ASSERT_SUCCESS(aws_array_list_front(&list, &item));
    ASSERT_INT_EQUALS(third, item);

    /* Pop last 2/2 elements. List should be empty. */
    aws_array_list_pop_front_n(&list, 2);
    ASSERT_INT_EQUALS(0, aws_array_list_length(&list), "List should be empty after popping last 2 items");

    /* Put some elements into list again.
     * Popping more items than list contains should just clear the list */
    ASSERT_SUCCESS(aws_array_list_push_back(&list, (void *)&first));
    ASSERT_SUCCESS(aws_array_list_push_back(&list, (void *)&second));
    ASSERT_SUCCESS(aws_array_list_push_back(&list, (void *)&third));
    ASSERT_SUCCESS(aws_array_list_push_back(&list, (void *)&fourth));
    aws_array_list_pop_front_n(&list, 99);
    ASSERT_INT_EQUALS(0, aws_array_list_length(&list));

    aws_array_list_clean_up(&list);

    return 0;
}

AWS_TEST_CASE(array_list_pop_front_n_test, s_array_list_pop_front_n_fn)

static int s_reset_list(struct aws_array_list *list, const int *array, size_t array_len) {
    aws_array_list_clear(list);
    for (size_t i = 0; i < array_len; ++i) {
        ASSERT_SUCCESS(aws_array_list_push_back(list, &array[i]));
    }
    return AWS_OP_SUCCESS;
}

static int s_check_list_eq(const struct aws_array_list *list, const int *array, size_t array_len) {
    ASSERT_UINT_EQUALS(array_len, aws_array_list_length(list));
    for (size_t i = 0; i < array_len; ++i) {
        int item;
        ASSERT_SUCCESS(aws_array_list_get_at(list, &item, i));
        ASSERT_INT_EQUALS(array[i], item);
    }
    return AWS_OP_SUCCESS;
}

static int s_array_list_erase_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;
    ASSERT_SUCCESS(aws_array_list_init_dynamic(&list, allocator, 10, sizeof(int)));

    {
        /* Attempts to erase invalid indices should fail */
        const int starting_values[] = {1, 2, 3, 4};
        ASSERT_SUCCESS(s_reset_list(&list, starting_values, AWS_ARRAY_SIZE(starting_values)));

        ASSERT_ERROR(AWS_ERROR_INVALID_INDEX, aws_array_list_erase(&list, AWS_ARRAY_SIZE(starting_values)));
        ASSERT_ERROR(AWS_ERROR_INVALID_INDEX, aws_array_list_erase(&list, AWS_ARRAY_SIZE(starting_values) + 100));

        ASSERT_SUCCESS(s_check_list_eq(&list, starting_values, AWS_ARRAY_SIZE(starting_values)));
    }

    {
        /* Erase front item */
        const int starting_values[] = {1, 2, 3, 4};
        ASSERT_SUCCESS(s_reset_list(&list, starting_values, AWS_ARRAY_SIZE(starting_values)));

        ASSERT_SUCCESS(aws_array_list_erase(&list, 0));

        const int expected_values[] = {2, 3, 4};
        ASSERT_SUCCESS(s_check_list_eq(&list, expected_values, AWS_ARRAY_SIZE(expected_values)));
    }

    {
        /* Erase back item */
        const int starting_values[] = {1, 2, 3, 4};
        ASSERT_SUCCESS(s_reset_list(&list, starting_values, AWS_ARRAY_SIZE(starting_values)));

        ASSERT_SUCCESS(aws_array_list_erase(&list, 3));

        const int expected_values[] = {1, 2, 3};
        ASSERT_SUCCESS(s_check_list_eq(&list, expected_values, AWS_ARRAY_SIZE(expected_values)));
    }

    {
        /* Erase middle item */
        const int starting_values[] = {1, 2, 3, 4};
        ASSERT_SUCCESS(s_reset_list(&list, starting_values, AWS_ARRAY_SIZE(starting_values)));

        ASSERT_SUCCESS(aws_array_list_erase(&list, 1));

        const int expected_values[] = {1, 3, 4};
        ASSERT_SUCCESS(s_check_list_eq(&list, expected_values, AWS_ARRAY_SIZE(expected_values)));
    }

    aws_array_list_clean_up(&list);
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(array_list_erase_test, s_array_list_erase_fn)

static int s_array_list_exponential_mem_model_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;

    static size_t list_size = 1;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());

    int first = 1, second = 2, third = 3;

    ASSERT_INT_EQUALS(0, list.length, "List size should be 0.");
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&first), "array list push back failed with error %d", aws_last_error());
    ASSERT_INT_EQUALS(list_size, list.current_size / sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&second),
        "array list push back failed with error %d",
        aws_last_error());
    ASSERT_INT_EQUALS(
        list_size << 1,
        list.current_size / sizeof(int),
        "Allocated list size should be %d.",
        (int)(list_size << 1) * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&third), "array list push back failed with error %d", aws_last_error());
    ASSERT_INT_EQUALS(
        list_size << 2,
        list.current_size / sizeof(int),
        "Allocated list size should be %d.",
        (int)(list_size << 2) * sizeof(int));

    ASSERT_INT_EQUALS(3, list.length, "List size should be %d.", 3);

    int item = 0;
    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(first, item, "Item should have been the first item.");

    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(second, item, "Item should have been the second item.");

    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(third, item, "Item should have been the third item.");

    ASSERT_INT_EQUALS(0, list.length, "List size should be 0.");
    ASSERT_INT_EQUALS(
        list_size << 2,
        list.current_size / sizeof(int),
        "Allocated list size should be %d.",
        (int)(list_size << 2) * sizeof(int));

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_exponential_mem_model_test, s_array_list_exponential_mem_model_test_fn)

static int s_array_list_exponential_mem_model_iteration_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;

    static size_t list_size = 1;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());

    int first = 1, second = 2, third = 3;

    ASSERT_INT_EQUALS(0, list.length, "List size should be 0.");
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "Allocated list size should be %d.", (int)list_size * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&first, 0), "array list push back failed with error %d", aws_last_error());
    ASSERT_INT_EQUALS(list_size, list.current_size / sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&second, 1),
        "array list push back failed with error %d",
        aws_last_error());
    ASSERT_INT_EQUALS(
        list_size << 1,
        list.current_size / sizeof(int),
        "Allocated list size should be %d.",
        (int)(list_size << 1) * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&third, 2), "array list push back failed with error %d", aws_last_error());
    ASSERT_INT_EQUALS(
        list_size << 2,
        list.current_size / sizeof(int),
        "Allocated list size should be %d.",
        (int)(list_size << 2) * sizeof(int));

    ASSERT_INT_EQUALS(3, list.length, "List size should be %d.", 3);

    int item = 0;
    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(first, item, "Item should have been the first item.");

    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(second, item, "Item should have been the second item.");

    ASSERT_SUCCESS(
        aws_array_list_front(&list, (void *)&item), "List front failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_pop_front(&list), "List pop front failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(third, item, "Item should have been the third item.");

    ASSERT_INT_EQUALS(0, list.length, "List size should be 0.");
    ASSERT_INT_EQUALS(
        list_size << 2,
        list.current_size / sizeof(int),
        "Allocated list size should be %d.",
        (int)(list_size << 2) * sizeof(int));

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_exponential_mem_model_iteration_test, s_array_list_exponential_mem_model_iteration_test_fn)

static int s_array_list_set_at_overwrite_safety_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_array_list list;

    size_t list_size = 4;
    int overwrite_data[5];

    aws_array_list_init_static(&list, overwrite_data, list_size, sizeof(int));

    memset(overwrite_data, 0x11, sizeof(overwrite_data));
    list.current_size = list_size * sizeof(int);
    unsigned value = 0xFFFFFFFF;

    ASSERT_SUCCESS(aws_array_list_set_at(&list, (void *)&value, 3));
    ASSERT_ERROR(AWS_ERROR_INVALID_INDEX, aws_array_list_set_at(&list, (void *)&value, 4));
    ASSERT_INT_EQUALS(0x11111111, overwrite_data[4]);

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_set_at_overwrite_safety, s_array_list_set_at_overwrite_safety_fn)

static int s_array_list_iteration_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;

    static size_t list_size = 4;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());

    int first = 1, second = 2, third = 3, fourth = 4;

    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&first, 0), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(1, list.length, "List size should be %d.", 1);
    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&second, 1), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list.length, "List size should be %d.", 2);
    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&third, 2), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(3, list.length, "List size should be %d.", 3);
    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&fourth, 3), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(4, list.length, "List size should be %d.", 4);

    int item = 0;
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list, (void *)&item, 0), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(first, item, "Item should have been the first item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list, (void *)&item, 1), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(second, item, "Item should have been the second item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list, (void *)&item, 2), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(third, item, "Item should have been the third item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list, (void *)&item, 3), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(fourth, item, "Item should have been the fourth item.");

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_iteration_test, s_array_list_iteration_test_fn)

static int s_array_list_iteration_by_ptr_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;

    static size_t list_size = 4;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());

    int first = 1, second = 2, third = 3, fourth = 4;

    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&first, 0), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(1, list.length, "List size should be %d.", 1);
    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&second, 1), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list.length, "List size should be %d.", 2);
    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&third, 2), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(3, list.length, "List size should be %d.", 3);
    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&fourth, 3), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(4, list.length, "List size should be %d.", 4);

    int *item;
    ASSERT_SUCCESS(
        aws_array_list_get_at_ptr(&list, (void **)&item, 0), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(first, *item, "Item should have been the first item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at_ptr(&list, (void **)&item, 1), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(second, *item, "Item should have been the second item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at_ptr(&list, (void **)&item, 2), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(third, *item, "Item should have been the third item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at_ptr(&list, (void **)&item, 3), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(fourth, *item, "Item should have been the fourth item.");

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_iteration_by_ptr_test, s_array_list_iteration_by_ptr_test_fn)

static int s_array_list_preallocated_iteration_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_array_list list;

    int list_data[4];
    size_t list_size = 4;
    aws_array_list_init_static(&list, (void *)list_data, list_size, sizeof(int));

    int first = 1, second = 2, third = 3, fourth = 4;

    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&first, 0), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(1, list.length, "List size should be %d.", 1);
    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&second, 1), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list.length, "List size should be %d.", 2);
    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&third, 2), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(3, list.length, "List size should be %d.", 3);
    ASSERT_SUCCESS(
        aws_array_list_set_at(&list, (void *)&fourth, 3), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(4, list.length, "List size should be %d.", 4);
    ASSERT_FAILS(aws_array_list_set_at(&list, (void *)&fourth, 4), "Adding element past the end should have failed");
    ASSERT_INT_EQUALS(
        AWS_ERROR_INVALID_INDEX,
        aws_last_error(),
        "Error code should have been INVALID_INDEX but was %d",
        aws_last_error());

    int item = 0;
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list, (void *)&item, 0), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(first, item, "Item should have been the first item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list, (void *)&item, 1), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(second, item, "Item should have been the second item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list, (void *)&item, 2), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(third, item, "Item should have been the third item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list, (void *)&item, 3), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(fourth, item, "Item should have been the fourth item.");
    ASSERT_FAILS(aws_array_list_get_at(&list, (void *)&item, 4), "Getting an element past the end should have failed");
    ASSERT_INT_EQUALS(
        AWS_ERROR_INVALID_INDEX,
        aws_last_error(),
        "Error code should have been INVALID_INDEX but was %d",
        aws_last_error());

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_preallocated_iteration_test, s_array_list_preallocated_iteration_test_fn)

static int s_array_list_preallocated_push_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_array_list list;

    int list_data[4];
    const size_t list_size = 4;
    aws_array_list_init_static(&list, (void *)list_data, list_size, sizeof(int));

    int first = 1, second = 2, third = 3, fourth = 4;

    ASSERT_INT_EQUALS(0, list.length, "List size should be 0.");
    ASSERT_INT_EQUALS(sizeof(list_data), list.current_size, "Allocated list size should be %d.", sizeof(list_data));

    ASSERT_SUCCESS(aws_array_list_push_back(&list, &first), "List push failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_push_back(&list, &second), "List push failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_push_back(&list, &third), "List push failed with error code %d", aws_last_error());
    ASSERT_SUCCESS(aws_array_list_push_back(&list, &fourth), "List push failed with error code %d", aws_last_error());
    ASSERT_ERROR(
        AWS_ERROR_LIST_EXCEEDS_MAX_SIZE,
        aws_array_list_push_back(&list, &fourth),
        "List push past static size should have failed with AWS_ERROR_LIST_EXCEEDS_MAX_SIZE but was %d",
        aws_last_error());

    aws_array_list_clean_up(&list);

    return 0;
}
AWS_TEST_CASE(array_list_preallocated_push_test, s_array_list_preallocated_push_test_fn)

static int s_array_list_shrink_to_fit_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;

    static size_t list_size = 4;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());

    int first = 1, second = 2;

    ASSERT_SUCCESS(aws_array_list_push_back(&list, &first), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(1, list.length, "List size should be %d.", 1);
    ASSERT_SUCCESS(aws_array_list_push_back(&list, &second), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list.length, "List size should be %d.", 2);

    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "size before shrink should be %d.", list_size * sizeof(int));

    ASSERT_SUCCESS(
        aws_array_list_shrink_to_fit(&list), "List shrink to fit failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list.length, "List size should be %d.", 2);
    ASSERT_INT_EQUALS(2, list.current_size / sizeof(int), "Shrunken size should be %d.", 2 * sizeof(int));

    int item = 0;
    ASSERT_SUCCESS(aws_array_list_get_at(&list, &item, 0), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(first, item, "Item should have been the first item.");
    ASSERT_SUCCESS(aws_array_list_get_at(&list, &item, 1), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(second, item, "Item should have been the second item.");
    ASSERT_FAILS(aws_array_list_get_at(&list, &item, 2), "Getting an element past the end should have failed");
    ASSERT_INT_EQUALS(
        AWS_ERROR_INVALID_INDEX,
        aws_last_error(),
        "Error code should have been INVALID_INDEX but was %d",
        aws_last_error());

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_shrink_to_fit_test, s_array_list_shrink_to_fit_test_fn)

static int s_array_list_shrink_to_fit_static_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_array_list list;

    int list_data[4];
    const size_t list_size = 4;

    aws_array_list_init_static(&list, (void *)list_data, list_size, sizeof(int));

    int first = 1, second = 2;

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&first), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(1, list.length, "List size should be %d.", 1);
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&second), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list.length, "List size should be %d.", 2);

    ASSERT_INT_EQUALS(sizeof(list_data), list.current_size, "size before shrink should be %d.", sizeof(list_data));

    ASSERT_FAILS(aws_array_list_shrink_to_fit(&list), "List shrink of static list should have failed.");
    ASSERT_INT_EQUALS(
        AWS_ERROR_LIST_STATIC_MODE_CANT_SHRINK,
        aws_last_error(),
        "Error code should have been LIST_STATIC_MODE_CANT_SHRINK but was %d",
        aws_last_error());

    ASSERT_PTR_EQUALS(&list_data, list.data, "The underlying allocation should not have changed");
    ASSERT_INT_EQUALS(sizeof(list_data), list.current_size, "List size should not have been changed");

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_shrink_to_fit_static_test, s_array_list_shrink_to_fit_static_test_fn)

static int s_array_list_clear_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;

    static size_t list_size = 4;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());

    int first = 1, second = 2;

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&first), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(1, list.length, "List size should be %d.", 1);
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list, (void *)&second), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list.length, "List size should be %d.", 2);

    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "size before clear should be %d.", list_size * sizeof(int));

    aws_array_list_clear(&list);
    ASSERT_INT_EQUALS(0, list.length, "List size should be %d after clear.", 0);
    ASSERT_INT_EQUALS(
        list_size, list.current_size / sizeof(int), "cleared size should be %d.", (int)list_size * sizeof(int));

    int item;
    ASSERT_FAILS(aws_array_list_front(&list, (void *)&item), "front() after a clear on list should have been an error");
    ASSERT_INT_EQUALS(
        AWS_ERROR_LIST_EMPTY, aws_last_error(), "Error code should have been LIST_EMPTY but was %d", aws_last_error());

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_clear_test, s_array_list_clear_test_fn)

static int s_array_list_copy_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list_a;
    struct aws_array_list list_b;

    static size_t list_size = 4;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list_a, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list_b, allocator, 0, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());

    int first = 1, second = 2;

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list_a, (void *)&first), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(1, list_a.length, "List size should be %d.", 1);
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list_a, (void *)&second), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list_a.length, "List size should be %d.", 2);

    ASSERT_SUCCESS(aws_array_list_copy(&list_a, &list_b), "List copy failed with error code %d", aws_last_error());

    int item = 0;
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list_b, (void *)&item, 0), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(first, item, "Item should have been the first item.");
    ASSERT_SUCCESS(
        aws_array_list_get_at(&list_b, (void *)&item, 1), "Array set failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(second, item, "Item should have been the second item.");

    ASSERT_INT_EQUALS(
        aws_array_list_length(&list_a), aws_array_list_length(&list_b), "list lengths should have matched.");

    aws_array_list_clean_up(&list_a);
    aws_array_list_clean_up(&list_b);
    return 0;
}

AWS_TEST_CASE(array_list_copy_test, s_array_list_copy_test_fn)

static int s_array_list_swap_contents_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    /* build lists */
    struct aws_array_list list_a;
    int a_1 = 1;
    int a_capacity = 1;
    ASSERT_SUCCESS(aws_array_list_init_dynamic(&list_a, allocator, a_capacity, sizeof(int)));
    ASSERT_SUCCESS(aws_array_list_push_back(&list_a, (void *)&a_1));

    struct aws_array_list list_b;
    int b_1 = 5;
    int b_2 = 6;
    int b_capacity = 3;
    ASSERT_SUCCESS(aws_array_list_init_dynamic(&list_b, allocator, b_capacity, sizeof(int)));
    ASSERT_SUCCESS(aws_array_list_push_back(&list_b, (void *)&b_1));
    ASSERT_SUCCESS(aws_array_list_push_back(&list_b, (void *)&b_2));

    void *a_buffer;
    ASSERT_SUCCESS(aws_array_list_get_at_ptr(&list_a, &a_buffer, 0));

    void *b_buffer;
    ASSERT_SUCCESS(aws_array_list_get_at_ptr(&list_b, &b_buffer, 0));

    /* swap */
    aws_array_list_swap_contents(&list_a, &list_b);

    /* compare state after swap */
    void *a_buffer_after_swap;
    ASSERT_SUCCESS(aws_array_list_get_at_ptr(&list_a, &a_buffer_after_swap, 0));
    ASSERT_PTR_EQUALS(b_buffer, a_buffer_after_swap, "Lists A and B should have swapped buffer ownership, but did not");

    void *b_buffer_after_swap;
    ASSERT_SUCCESS(aws_array_list_get_at_ptr(&list_b, &b_buffer_after_swap, 0));
    ASSERT_PTR_EQUALS(a_buffer, b_buffer_after_swap, "Lists A and B should have swapped buffer ownership, but did not");

    int item;
    ASSERT_INT_EQUALS(2, aws_array_list_length(&list_a), "List A should have taken B's old length");
    ASSERT_INT_EQUALS(b_capacity, aws_array_list_capacity(&list_a), "List A should have taken B's old capacity");
    ASSERT_SUCCESS(aws_array_list_get_at(&list_a, &item, 0), "List A should have B's old first item");
    ASSERT_INT_EQUALS(b_1, item, "List A should have B's old first item");
    ASSERT_SUCCESS(aws_array_list_get_at(&list_a, &item, 1), "List A should have B's old second item");
    ASSERT_INT_EQUALS(b_2, item, "List A should have B's old second item");

    ASSERT_INT_EQUALS(1, aws_array_list_length(&list_b), "List B should have taken A's old length");
    ASSERT_INT_EQUALS(a_capacity, aws_array_list_capacity(&list_b), "List B should have taken A's old capacity");
    ASSERT_SUCCESS(aws_array_list_get_at(&list_b, &item, 0), "List B should have A's old first item");
    ASSERT_INT_EQUALS(a_1, item, "List B should have A's old first item");

    aws_array_list_clean_up(&list_a);
    aws_array_list_clean_up(&list_b);

    return 0;
}

AWS_TEST_CASE(array_list_swap_contents_test, s_array_list_swap_contents_test_fn)

static int s_array_list_not_enough_space_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list_a;
    struct aws_array_list list_b;

    static size_t list_size = 4;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list_a, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list_b, allocator, 1, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());

    int first = 1, second = 2;

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list_a, (void *)&first), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(1, list_a.length, "List size should be %d.", 1);
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list_a, (void *)&second), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list_a.length, "List size should be %d.", 2);

    ASSERT_SUCCESS(aws_array_list_copy(&list_a, &list_b), "Copy from list_a to list_b should have succeeded");
    ASSERT_INT_EQUALS(list_a.length, list_b.length, "List b should have grown to the length of list a");
    ASSERT_INT_EQUALS(
        2 * sizeof(int),
        list_b.current_size,
        "List b should have grown to the size of the number of elements in list a");

    aws_array_list_clean_up(&list_a);
    aws_array_list_clean_up(&list_b);

    return 0;
}

AWS_TEST_CASE(array_list_not_enough_space_test, s_array_list_not_enough_space_test_fn)

static int s_array_list_not_enough_space_test_failure_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list_a;
    struct aws_array_list list_b;

    size_t list_size = 4;
    int static_list[1];
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list_a, allocator, list_size, sizeof(int)),
        "List initialization failed with error %d",
        aws_last_error());
    ASSERT_TRUE(list_a.data);
    aws_array_list_init_static(&list_b, static_list, 1, sizeof(int));
    ASSERT_TRUE(list_b.data);

    int first = 1, second = 2;

    ASSERT_SUCCESS(
        aws_array_list_push_back(&list_a, (void *)&first), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(1, list_a.length, "List size should be %d.", 1);
    ASSERT_SUCCESS(
        aws_array_list_push_back(&list_a, (void *)&second), "List push failed with error code %d", aws_last_error());
    ASSERT_INT_EQUALS(2, list_a.length, "List size should be %d.", 2);
    ASSERT_ERROR(
        AWS_ERROR_DEST_COPY_TOO_SMALL,
        aws_array_list_copy(&list_a, &list_b),
        "Copying to a static list too small should have failed with TOO_SMALL but got %d instead",
        aws_last_error());

    aws_array_list_clean_up(&list_a);
    aws_array_list_clean_up(&list_b);

    return 0;
}

AWS_TEST_CASE(array_list_not_enough_space_test_failure, s_array_list_not_enough_space_test_failure_fn)

static int s_array_list_of_strings_sort_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    AWS_STATIC_STRING_FROM_LITERAL(empty, "");
    AWS_STATIC_STRING_FROM_LITERAL(foo, "foo");
    AWS_STATIC_STRING_FROM_LITERAL(bar, "bar");
    AWS_STATIC_STRING_FROM_LITERAL(foobar, "foobar");
    AWS_STATIC_STRING_FROM_LITERAL(foo2, "foo");
    AWS_STATIC_STRING_FROM_LITERAL(foobaz, "foobaz");
    AWS_STATIC_STRING_FROM_LITERAL(bar_food, "bar food");
    AWS_STATIC_STRING_FROM_LITERAL(bar_null_food, "bar\0food");
    AWS_STATIC_STRING_FROM_LITERAL(bar_null_back, "bar\0back");

    const struct aws_string *strings[] = {
        empty, foo, bar, foobar, foo2, foobaz, bar_food, bar_null_food, bar_null_back};
    const struct aws_string *sorted[] = {empty, bar, bar_null_back, bar_null_food, bar_food, foo, foo2, foobar, foobaz};
    int num_strings = AWS_ARRAY_SIZE(strings);

    struct aws_array_list list;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, num_strings, sizeof(const struct aws_string *)),
        "List initialization failed with error %d",
        aws_last_error());
    for (int idx = 0; idx < num_strings; ++idx) {
        ASSERT_SUCCESS(
            aws_array_list_push_back(&list, (void *)(strings + idx)),
            "List push failed with error code %d",
            aws_last_error());
    }

    aws_array_list_sort(&list, aws_array_list_comparator_string);

    /* No control over whether foo or foo2 will be first, but checking for
     * string equality with sorted array makes that irrelevant.
     */
    for (int idx = 0; idx < num_strings; ++idx) {
        const struct aws_string *str;
        ASSERT_SUCCESS(
            aws_array_list_get_at(&list, (void **)&str, idx), "List get failed with error code %d", aws_last_error());
        ASSERT_INT_EQUALS(0, aws_string_compare(str, sorted[idx]), "Strings should be equal");
    }
    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_of_strings_sort, s_array_list_of_strings_sort_fn)

static int s_array_list_empty_sort_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_array_list list;
    ASSERT_SUCCESS(
        aws_array_list_init_dynamic(&list, allocator, 5, sizeof(const struct aws_string *)),
        "List initialization failed with error %d",
        aws_last_error());

    /* Nothing much to check, just want to make sure sort run on empty list
     * doesn't crash. */
    ASSERT_INT_EQUALS(0, aws_array_list_length(&list));
    ASSERT_INT_EQUALS(5, aws_array_list_capacity(&list));
    aws_array_list_sort(&list, aws_array_list_comparator_string);
    ASSERT_INT_EQUALS(0, aws_array_list_length(&list));
    ASSERT_INT_EQUALS(5, aws_array_list_capacity(&list));

    aws_array_list_clean_up(&list);
    return 0;
}

AWS_TEST_CASE(array_list_empty_sort, s_array_list_empty_sort_fn)
