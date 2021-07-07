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

struct test_decoder_data {
    struct aws_event_stream_message_prelude latest_prelude;
    char latest_header_name[100];
    char latest_header_value[100];
    uint8_t *latest_payload;
    size_t written;
    struct aws_allocator *alloc;
    int latest_error;
};

static void s_decoder_test_on_payload_segment(
    struct aws_event_stream_streaming_decoder *decoder,
    struct aws_byte_buf *data,
    int8_t final_segment,
    void *user_data) {
    (void)final_segment;
    (void)decoder;
    struct test_decoder_data *decoder_data = (struct test_decoder_data *)user_data;
    memcpy(decoder_data->latest_payload + decoder_data->written, data->buffer, data->len);
    decoder_data->written += data->len;
}

static void s_decoder_test_on_prelude_received(
    struct aws_event_stream_streaming_decoder *decoder,
    struct aws_event_stream_message_prelude *prelude,
    void *user_data) {

    (void)decoder;
    struct test_decoder_data *decoder_data = (struct test_decoder_data *)user_data;
    decoder_data->latest_prelude = *prelude;

    if (decoder_data->latest_payload) {
        aws_mem_release(decoder_data->alloc, decoder_data->latest_payload);
    }

    const size_t payload_size = decoder_data->latest_prelude.total_len - AWS_EVENT_STREAM_PRELUDE_LENGTH -
                                AWS_EVENT_STREAM_TRAILER_LENGTH - decoder_data->latest_prelude.headers_len;

    if (payload_size) {
        decoder_data->latest_payload = aws_mem_acquire(decoder_data->alloc, payload_size);
    } else {
        decoder_data->latest_payload = NULL;
    }
    decoder_data->written = 0;
}

static void s_decoder_test_header_received(
    struct aws_event_stream_streaming_decoder *decoder,
    struct aws_event_stream_message_prelude *prelude,
    struct aws_event_stream_header_value_pair *header,
    void *user_data) {
    (void)decoder;
    (void)prelude;
    struct test_decoder_data *decoder_data = (struct test_decoder_data *)user_data;
    memset(decoder_data->latest_header_name, 0, sizeof(decoder_data->latest_header_name));
    memcpy(decoder_data->latest_header_name, header->header_name, (size_t)header->header_name_len);
    memset(decoder_data->latest_header_value, 0, sizeof(decoder_data->latest_header_value));
    memcpy(decoder_data->latest_header_value, header->header_value.variable_len_val, header->header_value_len);
}

static void s_decoder_test_on_error(
    struct aws_event_stream_streaming_decoder *decoder,
    struct aws_event_stream_message_prelude *prelude,
    int error_code,
    const char *message,
    void *user_data) {

    (void)decoder;
    (void)prelude;
    (void)message;
    struct test_decoder_data *decoder_data = (struct test_decoder_data *)user_data;
    decoder_data->latest_error = error_code;
}

static int s_test_streaming_decoder_incoming_no_op_valid_single_message_fn(struct aws_allocator *allocator, void *ctx) {
    uint8_t test_data[] = {
        0x00,
        0x00,
        0x00,
        0x10,
        0x00,
        0x00,
        0x00,
        0x00,
        0x05,
        0xc2,
        0x48,
        0xeb,
        0x7d,
        0x98,
        0xc8,
        0xff,
    };

    (void)ctx;
    struct test_decoder_data decoder_data = {.latest_payload = 0, .written = 0, .alloc = allocator, .latest_error = 0};

    struct aws_event_stream_streaming_decoder decoder;
    aws_event_stream_streaming_decoder_init(
        &decoder,
        allocator,
        s_decoder_test_on_payload_segment,
        s_decoder_test_on_prelude_received,
        s_decoder_test_header_received,
        s_decoder_test_on_error,
        &decoder_data);

    struct aws_byte_buf test_buf = aws_byte_buf_from_array(test_data, sizeof(test_data));
    ASSERT_SUCCESS(
        aws_event_stream_streaming_decoder_pump(&decoder, &test_buf), "Message validation should have succeeded");
    ASSERT_SUCCESS(decoder_data.latest_error, "No Error callback shouldn't have been called");

    ASSERT_INT_EQUALS(0x00000010, decoder_data.latest_prelude.total_len, "Message length should have been 0x10");
    ASSERT_INT_EQUALS(0x00000000, decoder_data.latest_prelude.headers_len, "Headers Length should have been 0x00");
    ASSERT_INT_EQUALS(0x05c248eb, decoder_data.latest_prelude.prelude_crc, "Prelude CRC should have been 0x8c335472");
    ASSERT_INT_EQUALS(0, decoder_data.written, "No payload data should have been written");

    if (decoder_data.latest_payload) {
        aws_mem_release(allocator, decoder_data.latest_payload);
    }

    aws_event_stream_streaming_decoder_clean_up(&decoder);

    return 0;
}

AWS_TEST_CASE(
    test_streaming_decoder_incoming_no_op_valid_single_message,
    s_test_streaming_decoder_incoming_no_op_valid_single_message_fn)

static int s_test_streaming_decoder_incoming_application_no_headers_fn(struct aws_allocator *allocator, void *ctx) {
    uint8_t test_data[] = {
        0x00, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0x00, 0xfd, 0x52, 0x8c, 0x5a, 0x7b, 0x27, 0x66,
        0x6f, 0x6f, 0x27, 0x3a, 0x27, 0x62, 0x61, 0x72, 0x27, 0x7d, 0xc3, 0x65, 0x39, 0x36,
    };

    (void)ctx;
    struct test_decoder_data decoder_data = {.latest_payload = 0, .written = 0, .alloc = allocator, .latest_error = 0};

    struct aws_event_stream_streaming_decoder decoder;
    aws_event_stream_streaming_decoder_init(
        &decoder,
        allocator,
        s_decoder_test_on_payload_segment,
        s_decoder_test_on_prelude_received,
        s_decoder_test_header_received,
        s_decoder_test_on_error,
        &decoder_data);

    struct aws_byte_buf test_buf = aws_byte_buf_from_array(test_data, sizeof(test_data));

    ASSERT_SUCCESS(
        aws_event_stream_streaming_decoder_pump(&decoder, &test_buf), "Message validation should have succeeded");
    ASSERT_SUCCESS(decoder_data.latest_error, "No Error callback shouldn't have been called");

    ASSERT_INT_EQUALS(0x0000001D, decoder_data.latest_prelude.total_len, "Message length should have been 0x1D");
    ASSERT_INT_EQUALS(0x00000000, decoder_data.latest_prelude.headers_len, "Headers Length should have been 0x00");
    ASSERT_INT_EQUALS(0xfd528c5a, decoder_data.latest_prelude.prelude_crc, "Prelude CRC should have been 0xfd528c5a");

    const char *expected_str = "{'foo':'bar'}";
    size_t payload_len = decoder_data.latest_prelude.total_len - AWS_EVENT_STREAM_PRELUDE_LENGTH -
                         AWS_EVENT_STREAM_TRAILER_LENGTH - decoder_data.latest_prelude.headers_len;
    ASSERT_INT_EQUALS(
        strlen(expected_str), payload_len, "payload length should have been %d", (int)(strlen(expected_str)));

    ASSERT_BIN_ARRAYS_EQUALS(
        expected_str,
        strlen(expected_str),
        decoder_data.latest_payload,
        payload_len,
        "payload should have been %s",
        expected_str);

    if (decoder_data.latest_payload) {
        aws_mem_release(allocator, decoder_data.latest_payload);
    }

    aws_event_stream_streaming_decoder_clean_up(&decoder);

    return 0;
}

AWS_TEST_CASE(
    test_streaming_decoder_incoming_application_no_headers,
    s_test_streaming_decoder_incoming_application_no_headers_fn)

static int s_test_streaming_decoder_incoming_application_one_compressed_header_pair_valid_fn(
    struct aws_allocator *allocator,
    void *ctx) {
    (void)ctx;
    uint8_t test_data[] = {
        0x00, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x20, 0x07, 0xFD, 0x83, 0x96, 0x0C, 'c',  'o',  'n',
        't',  'e',  'n',  't',  '-',  't',  'y',  'p',  'e',  0x07, 0x00, 0x10, 'a',  'p',  'p',  'l',
        'i',  'c',  'a',  't',  'i',  'o',  'n',  '/',  'j',  's',  'o',  'n',  0x7b, 0x27, 0x66, 0x6f,
        0x6f, 0x27, 0x3a, 0x27, 0x62, 0x61, 0x72, 0x27, 0x7d, 0x8D, 0x9C, 0x08, 0xB1,
    };

    struct test_decoder_data decoder_data = {
        .latest_payload = 0,
        .written = 0,
        .alloc = allocator,
        .latest_error = 0,
    };

    struct aws_event_stream_streaming_decoder decoder;
    aws_event_stream_streaming_decoder_init(
        &decoder,
        allocator,
        s_decoder_test_on_payload_segment,
        s_decoder_test_on_prelude_received,
        s_decoder_test_header_received,
        s_decoder_test_on_error,
        &decoder_data);

    struct aws_byte_buf test_buf = aws_byte_buf_from_array(test_data, sizeof(test_data));

    ASSERT_SUCCESS(
        aws_event_stream_streaming_decoder_pump(&decoder, &test_buf), "Message validation should have succeeded");
    ASSERT_SUCCESS(decoder_data.latest_error, "No Error callback shouldn't have been called");

    ASSERT_INT_EQUALS(0x0000003D, decoder_data.latest_prelude.total_len, "Message length should have been 0x3D");
    ASSERT_INT_EQUALS(0x00000020, decoder_data.latest_prelude.headers_len, "Headers Length should have been 0x20");
    ASSERT_INT_EQUALS(0x07FD8396, decoder_data.latest_prelude.prelude_crc, "Prelude CRC should have been 0x07FD8396");

    const char *content_type = "content-type";
    const char *content_type_value = "application/json";

    ASSERT_BIN_ARRAYS_EQUALS(
        content_type,
        strlen(content_type),
        decoder_data.latest_header_name,
        strlen(decoder_data.latest_header_name),
        "header name should have been %s",
        content_type);
    ASSERT_BIN_ARRAYS_EQUALS(
        content_type_value,
        strlen(content_type_value),
        decoder_data.latest_header_value,
        strlen(decoder_data.latest_header_value),
        "header value should have been %s",
        content_type_value);

    const char *expected_str = "{'foo':'bar'}";
    size_t payload_len = decoder_data.latest_prelude.total_len - AWS_EVENT_STREAM_PRELUDE_LENGTH -
                         AWS_EVENT_STREAM_TRAILER_LENGTH - decoder_data.latest_prelude.headers_len;
    ASSERT_INT_EQUALS(
        strlen(expected_str), payload_len, "payload length should have been %d", (int)(strlen(expected_str)));

    ASSERT_BIN_ARRAYS_EQUALS(
        expected_str,
        strlen(expected_str),
        decoder_data.latest_payload,
        payload_len,
        "payload should have been %s",
        expected_str);

    if (decoder_data.latest_payload) {
        aws_mem_release(allocator, decoder_data.latest_payload);
    }

    return 0;
}

AWS_TEST_CASE(
    test_streaming_decoder_incoming_application_one_compressed_header_pair_valid,
    s_test_streaming_decoder_incoming_application_one_compressed_header_pair_valid_fn)

static int s_test_streaming_decoder_incoming_multiple_messages_fn(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    uint8_t test_data[] = {
        /* message 1 */
        0x00,
        0x00,
        0x00,
        0x10,
        0x00,
        0x00,
        0x00,
        0x00,
        0x05,
        0xc2,
        0x48,
        0xeb,
        0x7d,
        0x98,
        0xc8,
        0xff,
        /* message 2 */
        0x00,
        0x00,
        0x00,
        0x3D,
        0x00,
        0x00,
        0x00,
        0x20,
        0x07,
        0xFD,
        0x83,
        0x96,
        0x0C,
        'c',
        'o',
        'n',
        't',
        'e',
        'n',
        't',
        '-',
        't',
        'y',
        'p',
        'e',
        0x07,
        0x00,
        0x10,
        'a',
        'p',
        'p',
        'l',
        'i',
        'c',
        'a',
        't',
        'i',
        'o',
        'n',
        '/',
        'j',
        's',
        'o',
        'n',
        0x7b,
        0x27,
        0x66,
        0x6f,
        0x6f,
        0x27,
        0x3a,
        0x27,
        0x62,
        0x61,
        0x72,
        0x27,
        0x7d,
        0x8D,
        0x9C,
        0x08,
        0xB1,
    };

    size_t first_message_size = 0x10;
    size_t read_size = 7; /* make this a weird number to force edge case coverage in the parser.
                                This will fall into the middle of message boundaries and preludes. */

    struct test_decoder_data decoder_data = {.latest_payload = 0, .written = 0, .alloc = allocator, .latest_error = 0};

    struct aws_event_stream_streaming_decoder decoder;
    aws_event_stream_streaming_decoder_init(
        &decoder,
        allocator,
        s_decoder_test_on_payload_segment,
        s_decoder_test_on_prelude_received,
        s_decoder_test_header_received,
        s_decoder_test_on_error,
        &decoder_data);

    size_t current_written = 0;
    int err_code = 0;
    while (current_written < first_message_size && !err_code) {
        struct aws_byte_buf test_buf = aws_byte_buf_from_array(test_data + current_written, read_size);
        err_code = aws_event_stream_streaming_decoder_pump(&decoder, &test_buf);
        current_written += read_size;
    }

    /* we should have written into the second message, but prior to the new prelude being found.
       check first message was parsed correctly */
    ASSERT_SUCCESS(err_code, "Message validation should have succeeded");
    ASSERT_SUCCESS(decoder_data.latest_error, "No Error callback shouldn't have been called");

    ASSERT_INT_EQUALS(0x00000010, decoder_data.latest_prelude.total_len, "Message length should have been 0x10");
    ASSERT_INT_EQUALS(0x00000000, decoder_data.latest_prelude.headers_len, "Headers Length should have been 0x00");
    ASSERT_INT_EQUALS(0x05c248eb, decoder_data.latest_prelude.prelude_crc, "Prelude CRC should have been 0x8c335472");
    ASSERT_INT_EQUALS(0, decoder_data.written, "No payload data should have been written");

    while (current_written < sizeof(test_data) && !err_code) {
        size_t to_write =
            current_written + read_size < sizeof(test_data) ? read_size : sizeof(test_data) - current_written;
        struct aws_byte_buf test_buf = aws_byte_buf_from_array(test_data + current_written, to_write);
        err_code = aws_event_stream_streaming_decoder_pump(&decoder, &test_buf);
        current_written += to_write;
    }

    /* Second message should have been found and fully parsed at this point. */
    ASSERT_SUCCESS(err_code, "Message validation should have succeeded");
    ASSERT_SUCCESS(decoder_data.latest_error, "No Error callback shouldn't have been called");

    ASSERT_INT_EQUALS(0x0000003D, decoder_data.latest_prelude.total_len, "Message length should have been 0x3D");
    ASSERT_INT_EQUALS(0x00000020, decoder_data.latest_prelude.headers_len, "Headers Length should have been 0x20");
    ASSERT_INT_EQUALS(0x07FD8396, decoder_data.latest_prelude.prelude_crc, "Prelude CRC should have been 0x07FD8396");

    const char *content_type = "content-type";
    const char *content_type_value = "application/json";

    ASSERT_BIN_ARRAYS_EQUALS(
        content_type,
        strlen(content_type),
        decoder_data.latest_header_name,
        strlen(decoder_data.latest_header_name),
        "header name should have been %s",
        content_type);
    ASSERT_BIN_ARRAYS_EQUALS(
        content_type_value,
        strlen(content_type_value),
        decoder_data.latest_header_value,
        strlen(decoder_data.latest_header_value),
        "header value should have been %s",
        content_type_value);

    const char *expected_str = "{'foo':'bar'}";
    size_t payload_len = decoder_data.latest_prelude.total_len - AWS_EVENT_STREAM_PRELUDE_LENGTH -
                         AWS_EVENT_STREAM_TRAILER_LENGTH - decoder_data.latest_prelude.headers_len;
    ASSERT_INT_EQUALS(
        strlen(expected_str), payload_len, "payload length should have been %d", (int)(strlen(expected_str)));

    ASSERT_BIN_ARRAYS_EQUALS(
        expected_str,
        strlen(expected_str),
        decoder_data.latest_payload,
        payload_len,
        "payload should have been %s",
        expected_str);

    if (decoder_data.latest_payload) {
        aws_mem_release(allocator, decoder_data.latest_payload);
    }

    aws_event_stream_streaming_decoder_clean_up(&decoder);

    return 0;
}

AWS_TEST_CASE(test_streaming_decoder_incoming_multiple_messages, s_test_streaming_decoder_incoming_multiple_messages_fn)
