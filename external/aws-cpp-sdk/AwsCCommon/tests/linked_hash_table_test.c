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

#include <aws/common/linked_hash_table.h>
#include <aws/testing/aws_test_harness.h>

static int s_test_linked_hash_table_preserves_insertion_order_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_linked_hash_table table;

    ASSERT_SUCCESS(
        aws_linked_hash_table_init(&table, allocator, aws_hash_c_string, aws_hash_callback_c_str_eq, NULL, NULL, 3));

    const char *first_key = "first";
    const char *second_key = "second";
    const char *third_key = "third";
    const char *fourth_key = "fourth";

    int first = 1;
    int second = 2;
    int third = 3;
    int fourth = 4;

    ASSERT_SUCCESS(aws_linked_hash_table_put(&table, first_key, &first));
    ASSERT_SUCCESS(aws_linked_hash_table_put(&table, second_key, &second));
    ASSERT_SUCCESS(aws_linked_hash_table_put(&table, third_key, &third));
    ASSERT_SUCCESS(aws_linked_hash_table_put(&table, fourth_key, &fourth));

    ASSERT_INT_EQUALS(4, aws_linked_hash_table_get_element_count(&table));

    int *value = NULL;
    ASSERT_SUCCESS(aws_linked_hash_table_find(&table, first_key, (void **)&value));
    ASSERT_NOT_NULL(value);
    ASSERT_INT_EQUALS(first, *value);

    ASSERT_SUCCESS(aws_linked_hash_table_find(&table, second_key, (void **)&value));
    ASSERT_NOT_NULL(value);
    ASSERT_INT_EQUALS(second, *value);

    ASSERT_SUCCESS(aws_linked_hash_table_find(&table, third_key, (void **)&value));
    ASSERT_NOT_NULL(value);
    ASSERT_INT_EQUALS(third, *value);

    ASSERT_SUCCESS(aws_linked_hash_table_find(&table, fourth_key, (void **)&value));
    ASSERT_NOT_NULL(value);
    ASSERT_INT_EQUALS(fourth, *value);

    const struct aws_linked_list *list = aws_linked_hash_table_get_iteration_list(&table);
    ASSERT_NOT_NULL(list);

    struct aws_linked_list_node *node = aws_linked_list_front(list);

    struct aws_linked_hash_table_node *table_node = AWS_CONTAINER_OF(node, struct aws_linked_hash_table_node, node);
    ASSERT_INT_EQUALS(first, *(int *)table_node->value);

    node = aws_linked_list_next(node);
    ASSERT_NOT_NULL(node);
    table_node = AWS_CONTAINER_OF(node, struct aws_linked_hash_table_node, node);
    ASSERT_INT_EQUALS(second, *(int *)table_node->value);

    node = aws_linked_list_next(node);
    ASSERT_NOT_NULL(node);
    table_node = AWS_CONTAINER_OF(node, struct aws_linked_hash_table_node, node);
    ASSERT_INT_EQUALS(third, *(int *)table_node->value);

    node = aws_linked_list_next(node);
    ASSERT_NOT_NULL(node);
    table_node = AWS_CONTAINER_OF(node, struct aws_linked_hash_table_node, node);
    ASSERT_INT_EQUALS(fourth, *(int *)table_node->value);

    node = aws_linked_list_next(node);
    ASSERT_PTR_EQUALS(aws_linked_list_end(list), node);

    aws_linked_hash_table_clean_up(&table);
    return 0;
}

AWS_TEST_CASE(test_linked_hash_table_preserves_insertion_order, s_test_linked_hash_table_preserves_insertion_order_fn)

struct linked_hash_table_test_value_element {
    bool value_removed;
};

static void s_linked_hash_table_element_value_destroy(void *value) {
    struct linked_hash_table_test_value_element *value_element = value;
    value_element->value_removed = true;
}

static int s_test_linked_hash_table_entries_cleanup_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_linked_hash_table table;

    ASSERT_SUCCESS(aws_linked_hash_table_init(
        &table,
        allocator,
        aws_hash_c_string,
        aws_hash_callback_c_str_eq,
        NULL,
        s_linked_hash_table_element_value_destroy,
        2));

    const char *first_key = "first";
    const char *second_key = "second";

    struct linked_hash_table_test_value_element first = {.value_removed = false};
    struct linked_hash_table_test_value_element second = {.value_removed = false};

    ASSERT_SUCCESS(aws_linked_hash_table_put(&table, first_key, &first));
    ASSERT_SUCCESS(aws_linked_hash_table_put(&table, second_key, &second));
    ASSERT_INT_EQUALS(2, aws_linked_hash_table_get_element_count(&table));

    ASSERT_SUCCESS(aws_linked_hash_table_remove(&table, second_key));

    ASSERT_TRUE(second.value_removed);
    ASSERT_INT_EQUALS(1, aws_linked_hash_table_get_element_count(&table));

    aws_linked_hash_table_clear(&table);
    ASSERT_INT_EQUALS(0, aws_linked_hash_table_get_element_count(&table));

    ASSERT_TRUE(first.value_removed);
    ASSERT_TRUE(aws_linked_list_empty(aws_linked_hash_table_get_iteration_list(&table)));

    aws_linked_hash_table_clean_up(&table);
    return 0;
}

AWS_TEST_CASE(test_linked_hash_table_entries_cleanup, s_test_linked_hash_table_entries_cleanup_fn)

static int s_test_linked_hash_table_entries_overwrite_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct aws_linked_hash_table table;

    ASSERT_SUCCESS(aws_linked_hash_table_init(
        &table,
        allocator,
        aws_hash_c_string,
        aws_hash_callback_c_str_eq,
        NULL,
        s_linked_hash_table_element_value_destroy,
        2));

    const char *first_key = "first";

    struct linked_hash_table_test_value_element first = {.value_removed = false};
    struct linked_hash_table_test_value_element second = {.value_removed = false};

    ASSERT_SUCCESS(aws_linked_hash_table_put(&table, first_key, &first));
    ASSERT_SUCCESS(aws_linked_hash_table_put(&table, first_key, &second));
    ASSERT_INT_EQUALS(1, aws_linked_hash_table_get_element_count(&table));

    ASSERT_TRUE(first.value_removed);
    ASSERT_FALSE(second.value_removed);

    struct linked_hash_table_test_value_element *value = NULL;
    ASSERT_SUCCESS(aws_linked_hash_table_find(&table, first_key, (void **)&value));
    ASSERT_NOT_NULL(value);
    ASSERT_PTR_EQUALS(&second, value);

    aws_linked_hash_table_clean_up(&table);
    return 0;
}

AWS_TEST_CASE(test_linked_hash_table_entries_overwrite, s_test_linked_hash_table_entries_overwrite_fn)
