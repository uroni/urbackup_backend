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

#include <aws/event-stream/event_stream.h>
#include <aws/testing/aws_test_harness.h>

static int s_test_outgoing_no_op_valid_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    uint8_t test_data[] = {
        0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x05, 0xc2, 0x48, 0xeb, 0x7d, 0x98, 0xc8, 0xff};

    struct aws_event_stream_message message;
    struct aws_byte_buf test_buf = aws_byte_buf_from_array(test_data, sizeof(test_data));
    ASSERT_SUCCESS(
        aws_event_stream_message_from_buffer(&message, NULL, &test_buf), "Message validation should have succeeded");

    ASSERT_INT_EQUALS(
        0x00000010, aws_event_stream_message_total_length(&message), "Message length should have been 0x10");
    ASSERT_INT_EQUALS(
        0x00000000, aws_event_stream_message_headers_len(&message), "Headers Length should have been 0x00");
    ASSERT_INT_EQUALS(
        0x05c248eb, aws_event_stream_message_prelude_crc(&message), "Prelude CRC should have been 0x05c248eb");
    ASSERT_INT_EQUALS(
        0x7d98c8ff, aws_event_stream_message_message_crc(&message), "Message CRC should have been 0x7d98c8ff");

    aws_event_stream_message_clean_up(&message);

    return 0;
}

AWS_TEST_CASE(test_outgoing_no_op_valid, s_test_outgoing_no_op_valid_fn)

static int s_test_outgoing_application_data_no_headers_valid_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    uint8_t test_data[] = {0x00, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0x00, 0xfd, 0x52, 0x8c, 0x5a, 0x7b, 0x27, 0x66,
                           0x6f, 0x6f, 0x27, 0x3a, 0x27, 0x62, 0x61, 0x72, 0x27, 0x7d, 0xc3, 0x65, 0x39, 0x36};

    struct aws_event_stream_message message;
    struct aws_byte_buf test_buf = aws_byte_buf_from_array(test_data, sizeof(test_data));

    ASSERT_SUCCESS(
        aws_event_stream_message_from_buffer(&message, NULL, &test_buf), "Message validation should have succeeded");

    ASSERT_INT_EQUALS(
        0x0000001D, aws_event_stream_message_total_length(&message), "Message length should have been 0x0000001D");
    ASSERT_INT_EQUALS(
        0x00000000, aws_event_stream_message_headers_len(&message), "Headers Length should have been 0x00");
    ASSERT_INT_EQUALS(
        0xfd528c5a, aws_event_stream_message_prelude_crc(&message), "Prelude CRC should have been 0xfd528c5a");

    const char *expected_str = "{'foo':'bar'}";
    ASSERT_INT_EQUALS(
        strlen(expected_str),
        aws_event_stream_message_payload_len(&message),
        "payload length should have been %d",
        (int)(strlen(expected_str)));

    ASSERT_BIN_ARRAYS_EQUALS(
        expected_str,
        strlen(expected_str),
        aws_event_stream_message_payload(&message),
        aws_event_stream_message_payload_len(&message),
        "payload should have been %s",
        expected_str);
    ASSERT_INT_EQUALS(
        0xc3653936, aws_event_stream_message_message_crc(&message), "Message CRC should have been 0xc3653936");

    aws_event_stream_message_clean_up(&message);

    return 0;
}

AWS_TEST_CASE(test_outgoing_application_data_no_headers_valid, s_test_outgoing_application_data_no_headers_valid_fn)

static int s_test_outgoing_application_one_compressed_header_pair_valid_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    uint8_t test_data[] = {0x00, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x20, 0x07, 0xFD, 0x83, 0x96, 0x0C,
                           'c',  'o',  'n',  't',  'e',  'n',  't',  '-',  't',  'y',  'p',  'e',  0x07,
                           0x00, 0x10, 'a',  'p',  'p',  'l',  'i',  'c',  'a',  't',  'i',  'o',  'n',
                           '/',  'j',  's',  'o',  'n',  0x7b, 0x27, 0x66, 0x6f, 0x6f, 0x27, 0x3a, 0x27,
                           0x62, 0x61, 0x72, 0x27, 0x7d, 0x8D, 0x9C, 0x08, 0xB1};

    struct aws_event_stream_message message;
    struct aws_byte_buf test_buf = aws_byte_buf_from_array(test_data, sizeof(test_data));

    ASSERT_SUCCESS(
        aws_event_stream_message_from_buffer(&message, allocator, &test_buf),
        "Message validation should have succeeded");

    ASSERT_INT_EQUALS(
        0x0000003D, aws_event_stream_message_total_length(&message), "Message length should have been 0x0000003D");
    ASSERT_INT_EQUALS(
        0x00000020, aws_event_stream_message_headers_len(&message), "Headers Length should have been 0x00000020");
    ASSERT_INT_EQUALS(
        0x07FD8396, aws_event_stream_message_prelude_crc(&message), "Prelude CRC should have been 0x07FD8396");

    const char *expected_str = "{'foo':'bar'}";
    ASSERT_INT_EQUALS(
        strlen(expected_str),
        aws_event_stream_message_payload_len(&message),
        "payload length should have been %d",
        (int)(strlen(expected_str)));

    ASSERT_BIN_ARRAYS_EQUALS(
        expected_str,
        strlen(expected_str),
        aws_event_stream_message_payload(&message),
        aws_event_stream_message_payload_len(&message),
        "payload should have been %s",
        expected_str);

    struct aws_array_list headers;
    ASSERT_SUCCESS(aws_event_stream_headers_list_init(&headers, allocator), "Header initialization failed");

    ASSERT_SUCCESS(aws_event_stream_message_headers(&message, &headers), "Header parsing should have succeeded");
    ASSERT_INT_EQUALS(1, headers.length, "There should be exactly one header found");
    struct aws_event_stream_header_value_pair header;
    ASSERT_SUCCESS(
        aws_array_list_front(&headers, &header),
        "accessing the first element of an array of size 1 should have succeeded");

    const char *content_type = "content-type";
    const char *content_type_value = "application/json";

    struct aws_byte_buf header_name_buf = aws_event_stream_header_name(&header);
    ASSERT_BIN_ARRAYS_EQUALS(
        content_type,
        strlen(content_type),
        header_name_buf.buffer,
        header_name_buf.len,
        "header name should have been %s",
        content_type);

    struct aws_byte_buf header_value_buf = aws_event_stream_header_value_as_string(&header);

    ASSERT_BIN_ARRAYS_EQUALS(
        content_type_value,
        strlen(content_type_value),
        header_value_buf.buffer,
        header_value_buf.len,
        "header value should have been %s",
        content_type_value);

    ASSERT_INT_EQUALS(
        0x8D9C08B1, aws_event_stream_message_message_crc(&message), "Message CRC should have been 0x8D9C08B1");

    aws_event_stream_headers_list_cleanup(&headers);
    aws_event_stream_message_clean_up(&message);

    return 0;
}

AWS_TEST_CASE(
    test_outgoing_application_one_compressed_header_pair_valid,
    s_test_outgoing_application_one_compressed_header_pair_valid_fn)
