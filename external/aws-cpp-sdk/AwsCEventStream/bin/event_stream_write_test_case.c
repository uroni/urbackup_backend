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

#include <aws/common/encoding.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    define DELIM "\\"
#else
#    define DELIM "/"
#endif

/**
 * 4996 is to disable unsafe function sprintf vs sprintf_s
 * 4310 is to disable type casting to smaller type at line 215, which is needed to avoid gcc overflow warning when casting int to int8_t
 */
#ifdef _MSC_VER
#pragma warning (disable: 4996 4310)
#endif

static void write_negative_test_case(
    const char *root_dir,
    const char *test_name,
    const uint8_t *buffer,
    size_t buffer_size,
    const char *err_msg) {
    size_t dir_len = strlen(root_dir);
    size_t encoded_len = strlen("encoded") + strlen("negative") + strlen(test_name) + 2;
    size_t decoded_len = strlen("decoded") + strlen("negative") + strlen(test_name) + 2;

    char *enc_output_file = (char *)malloc(dir_len + 1 + encoded_len + 1);
    sprintf(enc_output_file, "%s%s%s%s%s%s%s", root_dir, DELIM, "encoded", DELIM, "negative", DELIM, test_name);

    char *dec_output_file = (char *)malloc(dir_len + 1 + decoded_len + 1);
    sprintf(dec_output_file, "%s%s%s%s%s%s%s", root_dir, DELIM, "decoded", DELIM, "negative", DELIM, test_name);

    FILE *enc = fopen(enc_output_file, "w");
    if (!enc) {
        fprintf(stderr, "couldn't write to %s", enc_output_file);
        exit(-1);
    }

    fwrite(buffer, sizeof(uint8_t), buffer_size, enc);

    fflush(enc);
    fclose(enc);

    FILE *dec = fopen(dec_output_file, "w");
    if (!dec) {
        fprintf(stderr, "couldn't write to %s", dec_output_file);
        exit(-1);
    }

    fwrite(err_msg, sizeof(char), strlen(err_msg), dec);

    fflush(dec);
    fclose(dec);

    free(enc_output_file);
    free(dec_output_file);
}

static void write_positive_test_case(
    const char *root_dir,
    const char *test_name,
    struct aws_event_stream_message *message) {
    size_t dir_len = strlen(root_dir);
    size_t encoded_len = strlen("encoded") + strlen("positive") + strlen(test_name) + 2;
    size_t decoded_len = strlen("decoded") + strlen("positive") + strlen(test_name) + 2;

    char *enc_output_file = (char *)malloc(dir_len + 1 + encoded_len + 1);
    sprintf(enc_output_file, "%s%s%s%s%s%s%s", root_dir, DELIM, "encoded", DELIM, "positive", DELIM, test_name);

    char *dec_output_file = (char *)malloc(dir_len + 1 + decoded_len + 1);
    sprintf(dec_output_file, "%s%s%s%s%s%s%s", root_dir, DELIM, "decoded", DELIM, "positive", DELIM, test_name);

    FILE *enc = fopen(enc_output_file, "w");
    if (!enc) {
        fprintf(stderr, "couldn't write to %s", enc_output_file);
        exit(-1);
    }

    fwrite(
        aws_event_stream_message_buffer(message), sizeof(uint8_t), aws_event_stream_message_total_length(message), enc);

    fflush(enc);
    fclose(enc);

    FILE *dec = fopen(dec_output_file, "w");
    if (!dec) {
        fprintf(stderr, "couldn't write to %s", dec_output_file);
        exit(-1);
    }

    aws_event_stream_message_to_debug_str(dec, message);

    fflush(dec);
    fclose(dec);
    free(enc_output_file);
    free(dec_output_file);
}

int main(void) {
    struct aws_array_list headers;
    struct aws_allocator *alloc = aws_default_allocator();
    aws_event_stream_headers_list_init(&headers, alloc);

    struct aws_event_stream_message msg;
    aws_event_stream_message_init(&msg, alloc, &headers, NULL);

    write_positive_test_case(".", "empty_message", &msg);

    struct aws_byte_buf payload = aws_byte_buf_from_c_str("{'foo':'bar'}");

    aws_event_stream_message_clean_up(&msg);
    aws_event_stream_message_init(&msg, alloc, &headers, &payload);

    write_positive_test_case(".", "payload_no_headers", &msg);
    aws_event_stream_message_clean_up(&msg);

    static const char content_type[] = "content-type";
    static const char json[] = "application/json";

    aws_event_stream_add_string_header(&headers, content_type, sizeof(content_type) - 1, json, sizeof(json) - 1, 0);
    aws_event_stream_message_init(&msg, alloc, &headers, &payload);

    write_positive_test_case(".", "payload_one_str_header", &msg);

    /* corrupt length */
    uint32_t original_length = aws_event_stream_message_total_length(&msg);
    uint8_t *buffer_cpy = aws_mem_acquire(alloc, original_length);
    memcpy(buffer_cpy, aws_event_stream_message_buffer(&msg), original_length);
    aws_write_u32(original_length + 1, buffer_cpy);

    write_negative_test_case(".", "corrupted_length", buffer_cpy, original_length, "Prelude checksum mismatch");
    aws_mem_release(alloc, buffer_cpy);

    /* corrupt header length */
    buffer_cpy = aws_mem_acquire(alloc, original_length);
    memcpy(buffer_cpy, aws_event_stream_message_buffer(&msg), original_length);

    uint32_t original_hdr_len = aws_event_stream_message_headers_len(&msg);

    aws_write_u32(original_hdr_len + 1, buffer_cpy + 4);

    write_negative_test_case(".", "corrupted_header_len", buffer_cpy, original_length, "Prelude checksum mismatch");
    aws_mem_release(alloc, buffer_cpy);

    /* corrupt headers */
    buffer_cpy = aws_mem_acquire(alloc, original_length);
    memcpy(buffer_cpy, aws_event_stream_message_buffer(&msg), original_length);

    uint32_t hdr_len = aws_event_stream_message_headers_len(&msg);
    buffer_cpy[hdr_len + AWS_EVENT_STREAM_PRELUDE_LENGTH + 1] = 'a';

    write_negative_test_case(".", "corrupted_headers", buffer_cpy, original_length, "Message checksum mismatch");
    aws_mem_release(alloc, buffer_cpy);

    buffer_cpy = aws_mem_acquire(alloc, original_length);
    memcpy(buffer_cpy, aws_event_stream_message_buffer(&msg), original_length);
    aws_event_stream_message_clean_up(&msg);

    /* corrupt payload */
    aws_event_stream_message_init(&msg, alloc, NULL, &payload);
    ((uint8_t *)aws_event_stream_message_payload(&msg))[0] = '[';
    write_negative_test_case(
        ".",
        "corrupted_payload",
        aws_event_stream_message_buffer(&msg),
        aws_event_stream_message_total_length(&msg),
        "Message checksum mismatch");

    aws_event_stream_message_clean_up(&msg);

    /* int header */
    static const char event_type[] = "event-type";

    aws_array_list_clear(&headers);
    aws_event_stream_add_int32_header(&headers, event_type, sizeof(event_type) - 1, 0x0000A00C);
    aws_event_stream_message_init(&msg, alloc, &headers, &payload);

    write_positive_test_case(".", "int32_header", &msg);
    aws_event_stream_message_clean_up(&msg);

    aws_array_list_clear(&headers);

    /* one of every header type */
    aws_event_stream_add_int32_header(&headers, event_type, sizeof(event_type) - 1, 0x0000A00C);
    aws_event_stream_add_string_header(&headers, content_type, sizeof(content_type) - 1, json, sizeof(json) - 1, 0);

    static const char bool_false[] = "bool false";
    aws_event_stream_add_bool_header(&headers, bool_false, sizeof(bool_false) - 1, 0);

    static const char bool_true[] = "bool true";
    aws_event_stream_add_bool_header(&headers, bool_true, sizeof(bool_true) - 1, 1);

    static const char byte_hdr[] = "byte";
    aws_event_stream_add_byte_header(&headers, byte_hdr, sizeof(byte_hdr) - 1, (int8_t)0xcf);

    static const char byte_buf_hdr[] = "byte buf";
    static const char byte_buf[] = "I'm a little teapot!";

    aws_event_stream_add_bytebuf_header(
        &headers, byte_buf_hdr, sizeof(byte_buf_hdr) - 1, (uint8_t *)byte_buf, sizeof(byte_buf) - 1, 0);

    static const char timestamp_hdr[] = "timestamp";
    aws_event_stream_add_timestamp_header(&headers, timestamp_hdr, sizeof(timestamp_hdr) - 1, 8675309);

    static const char int16_hdr[] = "int16";
    aws_event_stream_add_int16_header(&headers, int16_hdr, sizeof(int16_hdr) - 1, 42);

    static const char int64_hdr[] = "int64";
    aws_event_stream_add_int64_header(&headers, int64_hdr, sizeof(int64_hdr) - 1, 42424242);

    static const char uuid_hdr[] = "uuid";
    static const uint8_t uuid[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    aws_event_stream_add_uuid_header(&headers, uuid_hdr, sizeof(uuid_hdr) - 1, (uint8_t *)uuid);
    aws_event_stream_message_init(&msg, alloc, &headers, &payload);

    struct aws_event_stream_message sanity_check_message;
    struct aws_byte_buf message_buffer =
        aws_byte_buf_from_array(aws_event_stream_message_buffer(&msg), aws_event_stream_message_total_length(&msg));

    int err = aws_event_stream_message_from_buffer(&sanity_check_message, alloc, &message_buffer);

    if (err) {
        fprintf(stderr, "failed to parse what should have been a valid message\n");
        exit(-1);
    }

    write_positive_test_case(".", "all_headers", &msg);
    aws_event_stream_message_clean_up(&msg);
    aws_event_stream_headers_list_cleanup(&headers);

    return 0;
}
