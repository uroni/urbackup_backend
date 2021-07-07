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

#include <aws/common/error.h>

#include <aws/common/thread.h>
#include <aws/testing/aws_test_harness.h>

static struct aws_error_info s_errors[] = {
    AWS_DEFINE_ERROR_INFO(1024, "test error 1", "test lib"),
    AWS_DEFINE_ERROR_INFO(1025, "test error 2", "test lib"),
};

static struct aws_error_info_list s_errors_list = {
    .error_list = s_errors,
    .count = AWS_ARRAY_SIZE(s_errors),
};

static int s_setup_errors_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    aws_reset_error();
    aws_set_global_error_handler_fn(NULL, NULL);
    aws_set_thread_local_error_handler_fn(NULL, NULL);
    aws_register_error_info(&s_errors_list);

    return AWS_OP_SUCCESS;
}

static int s_teardown_errors_test_fn(struct aws_allocator *allocator, int setup_res, void *ctx) {
    (void)allocator;
    (void)setup_res;
    (void)ctx;

    aws_reset_error();
    aws_set_global_error_handler_fn(NULL, NULL);
    aws_set_thread_local_error_handler_fn(NULL, NULL);

    return AWS_OP_SUCCESS;
}

static int s_raise_errors_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    int error = aws_last_error();

    ASSERT_NULL(error, "error should be initialized to NULL");
    ASSERT_INT_EQUALS(0, aws_last_error(), "error code should be initialized to 0");

    struct aws_error_info test_error_1 = s_errors[0];
    struct aws_error_info test_error_2 = s_errors[1];

    ASSERT_INT_EQUALS(-1, aws_raise_error(test_error_1.error_code), "Raise error should return failure code.");
    error = aws_last_error();
    ASSERT_INT_EQUALS(
        test_error_1.error_code, error, "Expected error code %d, but was %d", test_error_1.error_code, error);

    ASSERT_STR_EQUALS(
        test_error_1.error_str,
        aws_error_str(error),
        "Expected error string %s, but got %s",
        test_error_1.error_str,
        aws_error_str(error));
    ASSERT_STR_EQUALS(
        test_error_1.lib_name,
        aws_error_lib_name(error),
        "Expected error libname %s, but got %s",
        test_error_1.lib_name,
        aws_error_lib_name(error));

    ASSERT_INT_EQUALS(-1, aws_raise_error(test_error_2.error_code), "Raise error should return failure code.");
    error = aws_last_error();

    ASSERT_INT_EQUALS(
        test_error_2.error_code, error, "Expected error code %d, but was %d", test_error_2.error_code, error);

    error = aws_last_error();
    ASSERT_NOT_NULL(error, "last error should not have been null");
    ASSERT_STR_EQUALS(
        test_error_2.error_str,
        aws_error_str(error),
        "Expected error string %s, but got %s",
        test_error_2.error_str,
        aws_error_str(error));
    ASSERT_STR_EQUALS(
        test_error_2.lib_name,
        aws_error_lib_name(error),
        "Expected error libname %s, but got %s",
        test_error_2.lib_name,
        aws_error_lib_name(error));

    aws_reset_error();
    error = aws_last_error();
    ASSERT_NULL(error, "error should be reset to NULL");
    ASSERT_INT_EQUALS(0, aws_last_error(), "error code should be reset to 0");
    return 0;
}

static int s_reset_errors_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_error_info test_error_1 = s_errors[0];
    struct aws_error_info test_error_2 = s_errors[1];

    aws_raise_error(test_error_2.error_code);
    aws_restore_error(test_error_1.error_code);

    int error = aws_last_error();
    ASSERT_NOT_NULL(error, "last error should not have been null");
    ASSERT_INT_EQUALS(
        test_error_1.error_code, error, "Expected error code %d, but was %d", test_error_1.error_code, error);
    ASSERT_STR_EQUALS(
        test_error_1.error_str,
        aws_error_str(error),
        "Expected error string %s, but got %s",
        test_error_1.error_str,
        aws_error_str(error));
    ASSERT_STR_EQUALS(
        test_error_1.lib_name,
        aws_error_lib_name(error),
        "Expected error libname %s, but got %s",
        test_error_1.lib_name,
        aws_error_lib_name(error));

    return 0;
}

struct error_test_cb_data {
    int global_cb_called;
    int tl_cb_called;
    int last_seen;
};

static void s_error_test_global_cb(int err, void *ctx) {
    struct error_test_cb_data *cb_data = (struct error_test_cb_data *)ctx;
    cb_data->global_cb_called = 1;
    cb_data->last_seen = err;
}

static void s_error_test_thread_local_cb(int err, void *ctx) {
    struct error_test_cb_data *cb_data = (struct error_test_cb_data *)ctx;
    cb_data->tl_cb_called = 1;
    cb_data->last_seen = err;
}

static int s_error_callback_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct error_test_cb_data cb_data = {.last_seen = 0, .global_cb_called = 0, .tl_cb_called = 0};

    struct aws_error_info test_error_1 = s_errors[0];
    struct aws_error_info test_error_2 = s_errors[1];

    aws_error_handler_fn *old_fn = aws_set_global_error_handler_fn(s_error_test_global_cb, &cb_data);
    ASSERT_NULL(old_fn, "setting the global error callback the first time should return null");
    aws_raise_error(test_error_1.error_code);

    ASSERT_NOT_NULL(cb_data.last_seen, "last error should not have been null");
    ASSERT_TRUE(cb_data.global_cb_called, "Global Callback should have been invoked");
    ASSERT_FALSE(cb_data.tl_cb_called, "Thread Local Callback should not have been invoked");

    ASSERT_INT_EQUALS(
        test_error_1.error_code,
        cb_data.last_seen,
        "Expected error code %d, but was %d",
        test_error_1.error_code,
        cb_data.last_seen);
    ASSERT_STR_EQUALS(
        test_error_1.error_str,
        aws_error_str(cb_data.last_seen),
        "Expected error string %s, but got %s",
        test_error_1.error_str,
        aws_error_str(cb_data.last_seen));
    ASSERT_STR_EQUALS(
        test_error_1.lib_name,
        aws_error_lib_name(cb_data.last_seen),
        "Expected error libname %s, but got %s",
        test_error_1.lib_name,
        aws_error_lib_name(cb_data.last_seen));

    cb_data.last_seen = 0;
    cb_data.global_cb_called = 0;
    old_fn = aws_set_thread_local_error_handler_fn(s_error_test_thread_local_cb, &cb_data);
    ASSERT_NULL(old_fn, "setting the global error callback the first time should return null");

    aws_raise_error(test_error_2.error_code);
    ASSERT_INT_EQUALS(
        test_error_2.error_code,
        aws_last_error(),
        "Expected error code %d, but was %d",
        test_error_2.error_code,
        aws_last_error());

    ASSERT_NOT_NULL(cb_data.last_seen, "last error should not have been null");
    ASSERT_FALSE(cb_data.global_cb_called, "Global Callback should not have been invoked");
    ASSERT_TRUE(cb_data.tl_cb_called, "Thread local Callback should have been invoked");

    ASSERT_INT_EQUALS(
        test_error_2.error_code,
        cb_data.last_seen,
        "Expected error code %d, but was %d",
        test_error_2.error_code,
        cb_data.last_seen);
    ASSERT_STR_EQUALS(
        test_error_2.error_str,
        aws_error_str(cb_data.last_seen),
        "Expected error string %s, but got %s",
        test_error_2.error_str,
        aws_error_str(cb_data.last_seen));
    ASSERT_STR_EQUALS(
        test_error_2.lib_name,
        aws_error_lib_name(cb_data.last_seen),
        "Expected error libname %s, but got %s",
        test_error_2.lib_name,
        aws_error_lib_name(cb_data.last_seen));

    old_fn = aws_set_thread_local_error_handler_fn(NULL, NULL);
    ASSERT_PTR_EQUALS(
        s_error_test_thread_local_cb,
        old_fn,
        "Setting a new thread local error callback should have returned the most recent value");
    old_fn = aws_set_global_error_handler_fn(NULL, NULL);
    ASSERT_PTR_EQUALS(
        s_error_test_global_cb,
        old_fn,
        "Setting a new global error callback should have returned the most recent value");

    return 0;
}

static int s_unknown_error_code_in_slot_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    int error = aws_last_error();

    ASSERT_NULL(error, "error should be initialized to NULL");
    ASSERT_INT_EQUALS(0, aws_last_error(), "error code should be initialized to 0");

    struct aws_error_info test_error_2 = s_errors[1];

    aws_raise_error(test_error_2.error_code + 1);
    error = aws_last_error();
    /* error code should still propogate */
    ASSERT_INT_EQUALS(
        test_error_2.error_code + 1, error, "Expected error code %d, but was %d", test_error_2.error_code + 1, error);

    /* string should be invalid though */
    ASSERT_STR_EQUALS(
        "Unknown Error Code",
        aws_error_str(error),
        "Expected error string %s, but got %s",
        "Unknown Error Code",
        aws_error_str(error));
    ASSERT_STR_EQUALS(
        "Unknown Error Code",
        aws_error_lib_name(error),
        "Expected error string %s, but got %s",
        "Unknown Error Code",
        aws_error_lib_name(error));
    ASSERT_STR_EQUALS(
        "Unknown Error Code",
        aws_error_debug_str(error),
        "Expected error string %s, but got %s",
        "Unknown Error Code",
        aws_error_debug_str(error));

    return 0;
}

static int s_unknown_error_code_no_slot_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    int error = aws_last_error();

    ASSERT_NULL(error, "error should be initialized to NULL");
    ASSERT_INT_EQUALS(0, aws_last_error(), "error code should be initialized to 0");

    int non_slotted_error_code = 3000;
    aws_raise_error(non_slotted_error_code);
    error = aws_last_error();
    /* error code should still propogate */
    ASSERT_INT_EQUALS(
        non_slotted_error_code, error, "Expected error code %d, but was %d", non_slotted_error_code, error);

    /* string should be invalid though */
    ASSERT_STR_EQUALS(
        "Unknown Error Code",
        aws_error_str(error),
        "Expected error string %s, but got %s",
        "Unknown Error Code",
        aws_error_str(error));
    ASSERT_STR_EQUALS(
        "Unknown Error Code",
        aws_error_lib_name(error),
        "Expected error string %s, but got %s",
        "Unknown Error Code",
        aws_error_lib_name(error));
    ASSERT_STR_EQUALS(
        "Unknown Error Code",
        aws_error_debug_str(error),
        "Expected error string %s, but got %s",
        "Unknown Error Code",
        aws_error_debug_str(error));

    return 0;
}

static int s_unknown_error_code_range_too_large_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    int error = aws_last_error();

    ASSERT_NULL(error, "error should be initialized to NULL");
    ASSERT_INT_EQUALS(0, aws_last_error(), "error code should be initialized to 0");

    int oor_error_code = 10001;
    aws_raise_error(oor_error_code);
    error = aws_last_error();
    /* error code should still propogate */
    ASSERT_INT_EQUALS(oor_error_code, error, "Expected error code %d, but was %d", oor_error_code, error);

    /* string should be invalid though */
    ASSERT_STR_EQUALS(
        "Unknown Error Code",
        aws_error_str(error),
        "Expected error string %s, but got %s",
        "Unknown Error Code",
        aws_error_str(error));
    ASSERT_STR_EQUALS(
        "Unknown Error Code",
        aws_error_lib_name(error),
        "Expected error string %s, but got %s",
        "Unknown Error Code",
        aws_error_lib_name(error));
    ASSERT_STR_EQUALS(
        "Unknown Error Code",
        aws_error_debug_str(error),
        "Expected error string %s, but got %s",
        "Unknown Error Code",
        aws_error_debug_str(error));

    return 0;
}

struct error_thread_test_data {
    int thread_1_code;
    int thread_1_get_last_code;
    aws_thread_id_t thread_1_id;
    int thread_1_encountered_count;
    int thread_2_code;
    int thread_2_get_last_code;
    int thread_2_encountered_count;
    aws_thread_id_t thread_2_id;
};

static void s_error_thread_test_thread_local_cb(int err, void *ctx) {
    struct error_thread_test_data *cb_data = (struct error_thread_test_data *)ctx;

    aws_thread_id_t thread_id = aws_thread_current_thread_id();

    if (aws_thread_thread_id_equal(thread_id, cb_data->thread_1_id)) {
        cb_data->thread_1_code = err;
        cb_data->thread_1_get_last_code = aws_last_error();
        cb_data->thread_1_encountered_count += 1;
        return;
    }

    cb_data->thread_2_code = err;
    cb_data->thread_2_get_last_code = aws_last_error();
    cb_data->thread_2_id = aws_thread_current_thread_id();
    cb_data->thread_2_encountered_count += 1;
}

static void s_error_thread_fn(void *arg) {
    aws_set_thread_local_error_handler_fn(s_error_thread_test_thread_local_cb, arg);

    aws_raise_error(15);
}

static int s_error_code_cross_thread_test_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    struct error_thread_test_data test_data = {.thread_1_code = 0,
                                               .thread_1_get_last_code = 0,
                                               .thread_1_encountered_count = 0,
                                               .thread_2_code = 0,
                                               .thread_2_get_last_code = 0,
                                               .thread_2_encountered_count = 0,
                                               .thread_2_id = 0};

    test_data.thread_1_id = aws_thread_current_thread_id();

    aws_set_thread_local_error_handler_fn(s_error_thread_test_thread_local_cb, &test_data);

    int thread_1_error_code_expected = 5;
    aws_raise_error(thread_1_error_code_expected);

    struct aws_thread thread;
    aws_thread_init(&thread, allocator);
    ASSERT_SUCCESS(
        aws_thread_launch(&thread, s_error_thread_fn, &test_data, NULL),
        "Thread creation failed with error %d",
        aws_last_error());
    ASSERT_SUCCESS(aws_thread_join(&thread), "Thread join failed with error %d", aws_last_error());
    aws_thread_clean_up(&thread);
    ASSERT_INT_EQUALS(
        1,
        test_data.thread_1_encountered_count,
        "The thread local CB should only have triggered for the first thread once.");
    ASSERT_INT_EQUALS(
        1,
        test_data.thread_2_encountered_count,
        "The thread local CB should only have triggered for the second thread once.");
    ASSERT_FALSE(test_data.thread_2_id == 0, "thread 2 id should have been set to something other than 0");
    ASSERT_FALSE(test_data.thread_2_id == test_data.thread_1_id, "threads 1 and 2 should be different ids");
    ASSERT_INT_EQUALS(
        thread_1_error_code_expected,
        aws_last_error(),
        "Thread 1's error should not have changed when thread 2 raised an error.");
    ASSERT_INT_EQUALS(
        thread_1_error_code_expected, test_data.thread_1_code, "Thread 1 code should have matched the original error.");
    ASSERT_INT_EQUALS(
        thread_1_error_code_expected,
        test_data.thread_1_get_last_code,
        "Thread 1 get last error code should have matched the original error.");
    ASSERT_INT_EQUALS(15, test_data.thread_2_code, "Thread 2 code should have matched the thread 2 error.");
    ASSERT_INT_EQUALS(
        15, test_data.thread_2_get_last_code, "Thread 2 get last error code should have matched the thread 2 error.");

    return 0;
}

static int s_aws_load_error_strings_test(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    /* Load aws-c-common's actual error info.
     * This will fail if the error info list is out of sync with the error enums. */
    aws_common_library_init(allocator);
    return AWS_OP_SUCCESS;
}

static int s_aws_assume_compiles_test(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    AWS_ASSUME(true);

    if (false) {
        AWS_UNREACHABLE();
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE_FIXTURE(
    raise_errors_test,
    s_setup_errors_test_fn,
    s_raise_errors_test_fn,
    s_teardown_errors_test_fn,
    NULL)
AWS_TEST_CASE_FIXTURE(
    error_callback_test,
    s_setup_errors_test_fn,
    s_error_callback_test_fn,
    s_teardown_errors_test_fn,
    NULL)
AWS_TEST_CASE_FIXTURE(
    reset_errors_test,
    s_setup_errors_test_fn,
    s_reset_errors_test_fn,
    s_teardown_errors_test_fn,
    NULL)
AWS_TEST_CASE_FIXTURE(
    unknown_error_code_in_slot_test,
    s_setup_errors_test_fn,
    s_unknown_error_code_in_slot_test_fn,
    s_teardown_errors_test_fn,
    NULL)
AWS_TEST_CASE_FIXTURE(
    unknown_error_code_no_slot_test,
    s_setup_errors_test_fn,
    s_unknown_error_code_no_slot_test_fn,
    s_teardown_errors_test_fn,
    NULL)
AWS_TEST_CASE_FIXTURE(
    unknown_error_code_range_too_large_test,
    s_setup_errors_test_fn,
    s_unknown_error_code_range_too_large_test_fn,
    s_teardown_errors_test_fn,
    NULL)
AWS_TEST_CASE_FIXTURE(
    error_code_cross_thread_test,
    s_setup_errors_test_fn,
    s_error_code_cross_thread_test_fn,
    s_teardown_errors_test_fn,
    NULL)
AWS_TEST_CASE(aws_load_error_strings_test, s_aws_load_error_strings_test)
AWS_TEST_CASE(aws_assume_compiles_test, s_aws_assume_compiles_test)
