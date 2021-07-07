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
#include <aws/common/encoding.h>
#include <aws/event-stream/event_stream.h>
#include <aws/testing/aws_test_harness.h>

static int s_test_incoming_no_op_valid_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    uint8_t expected_data[] = {
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x05, 0xc2, 0x48, 0xeb, 0x7d, 0x98, 0xc8, 0xff};

    struct aws_event_stream_message message;
    ASSERT_SUCCESS(
        aws_event_stream_message_init(&message, allocator, NULL, NULL), "Message validation should have succeeded");

    ASSERT_BIN_ARRAYS_EQUALS(
        expected_data,
        sizeof(expected_data),
        aws_event_stream_message_buffer(&message),
        aws_event_stream_message_total_length(&message),
        "buffers didn't match");

    aws_event_stream_message_clean_up(&message);

    return 0;
}

AWS_TEST_CASE(test_incoming_no_op_valid, s_test_incoming_no_op_valid_fn)

static int s_test_incoming_application_data_no_headers_valid_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    uint8_t expected_data[] = {0x00, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0x00, 0xfd, 0x52, 0x8c, 0x5a, 0x7b, 0x27, 0x66,
                               0x6f, 0x6f, 0x27, 0x3a, 0x27, 0x62, 0x61, 0x72, 0x27, 0x7d, 0xc3, 0x65, 0x39, 0x36};

    const char *test_str = "{'foo':'bar'}";
    struct aws_event_stream_message message;
    struct aws_byte_buf test_buf = aws_byte_buf_from_c_str(test_str);
    ASSERT_SUCCESS(
        aws_event_stream_message_init(&message, allocator, NULL, &test_buf),
        "Message validation should have succeeded");

    ASSERT_BIN_ARRAYS_EQUALS(
        expected_data,
        sizeof(expected_data),
        aws_event_stream_message_buffer(&message),
        aws_event_stream_message_total_length(&message),
        "buffers didn't match");

    aws_event_stream_message_clean_up(&message);

    return 0;
}

AWS_TEST_CASE(test_incoming_application_data_no_headers_valid, s_test_incoming_application_data_no_headers_valid_fn)

static int s_test_incoming_application_one_compressed_header_pair_valid_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    uint8_t expected_data[] = {0x00, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x20, 0x07, 0xFD, 0x83, 0x96, 0x0C,
                               'c',  'o',  'n',  't',  'e',  'n',  't',  '-',  't',  'y',  'p',  'e',  0x07,
                               0x00, 0x10, 'a',  'p',  'p',  'l',  'i',  'c',  'a',  't',  'i',  'o',  'n',
                               '/',  'j',  's',  'o',  'n',  0x7b, 0x27, 0x66, 0x6f, 0x6f, 0x27, 0x3a, 0x27,
                               0x62, 0x61, 0x72, 0x27, 0x7d, 0x8D, 0x9C, 0x08, 0xB1};

    const char *test_str = "{'foo':'bar'}";
    struct aws_event_stream_message message;

    struct aws_array_list headers;
    ASSERT_SUCCESS(aws_event_stream_headers_list_init(&headers, allocator), "Header initialization failed");

    const char *header_name = "content-type";
    const char *header_value = "application/json";

    ASSERT_SUCCESS(
        aws_event_stream_add_string_header(
            &headers, header_name, (int8_t)strlen(header_name), header_value, (uint16_t)strlen(header_value), 0),
        "Adding a header should have succeeded.");

    struct aws_byte_buf test_buf = aws_byte_buf_from_c_str(test_str);

    ASSERT_SUCCESS(
        aws_event_stream_message_init(&message, allocator, &headers, &test_buf),
        "Message validation should have succeeded");

    ASSERT_BIN_ARRAYS_EQUALS(
        expected_data,
        sizeof(expected_data),
        aws_event_stream_message_buffer(&message),
        aws_event_stream_message_total_length(&message),
        "buffers didn't match");

    aws_event_stream_headers_list_cleanup(&headers);
    aws_event_stream_message_clean_up(&message);

    return 0;
}

AWS_TEST_CASE(
    test_incoming_application_one_compressed_header_pair_valid,
    s_test_incoming_application_one_compressed_header_pair_valid_fn)

static int s_test_incoming_application_int32_header_valid_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    uint8_t expected_data[] = {0x00, 0x00, 0x00, 0x2B, 0x00, 0x00, 0x00, 0x0E, 0x34, 0x8B, 0xEC, 0x7B, 0x08, 'e',  'v',
                               'e',  'n',  't',  '-',  'i',  'd',  0x04, 0x00, 0x00, 0xA0, 0x0C, 0x7b, 0x27, 0x66, 0x6f,
                               0x6f, 0x27, 0x3a, 0x27, 0x62, 0x61, 0x72, 0x27, 0x7d, 0xD3, 0x89, 0x02, 0x85};

    const char *test_str = "{'foo':'bar'}";
    struct aws_event_stream_message message;

    struct aws_array_list headers;
    ASSERT_SUCCESS(aws_event_stream_headers_list_init(&headers, allocator), "Header initialization failed");

    const char *header_name = "event-id";

    ASSERT_SUCCESS(
        aws_event_stream_add_int32_header(&headers, header_name, (int8_t)strlen(header_name), 0x0000A00c),
        "Adding a header should have succeeded.");

    struct aws_byte_buf test_buf = aws_byte_buf_from_c_str(test_str);

    ASSERT_SUCCESS(aws_event_stream_message_init(&message, allocator, &headers, &test_buf), "buffers didn't match");

    ASSERT_BIN_ARRAYS_EQUALS(
        expected_data,
        sizeof(expected_data),
        aws_event_stream_message_buffer(&message),
        aws_event_stream_message_total_length(&message),
        "buffers didn't match");

    aws_array_list_clean_up(&headers);
    aws_event_stream_message_clean_up(&message);

    return 0;
}

AWS_TEST_CASE(test_incoming_application_int32_header_valid, s_test_incoming_application_int32_header_valid_fn)
