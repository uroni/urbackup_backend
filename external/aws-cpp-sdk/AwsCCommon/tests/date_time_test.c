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
#include <aws/common/date_time.h>

#include <aws/common/byte_buf.h>

#include <aws/testing/aws_test_harness.h>

static int s_test_rfc822_utc_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    const char *valid_utc_dates[] = {
        "Wed, 02 Oct 2002 08:05:09 GMT",
        "Wed, 02 Oct 2002 08:05:09 UT",
        "Wed, 02 Oct 2002 08:05:09 Z",
        "Wed, 02 Oct 2002 08:05:09 UTC",
    };

    for (size_t i = 0; i < 4; ++i) {
        struct aws_date_time date_time;
        const char *date_str = valid_utc_dates[i];
        struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

        ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_RFC822));
        ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
        ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
        ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
        ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
        ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
        ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
        ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

        uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
        AWS_ZERO_ARRAY(date_output);
        struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
        str_output.len = 0;
        ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_RFC822, &str_output));

        const char *expected_long_str = "Wed, 02 Oct 2002 08:05:09 GMT";
        struct aws_byte_buf expected_long_buf = aws_byte_buf_from_c_str(expected_long_str);

        ASSERT_BIN_ARRAYS_EQUALS(expected_long_buf.buffer, expected_long_buf.len, str_output.buffer, str_output.len);

        AWS_ZERO_ARRAY(date_output);
        str_output.len = 0;
        ASSERT_SUCCESS(aws_date_time_to_utc_time_short_str(&date_time, AWS_DATE_FORMAT_RFC822, &str_output));

        const char *expected_short_str = "Wed, 02 Oct 2002";
        struct aws_byte_buf expected_short_buf = aws_byte_buf_from_c_str(expected_short_str);

        ASSERT_BIN_ARRAYS_EQUALS(expected_short_buf.buffer, expected_short_buf.len, str_output.buffer, str_output.len);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_utc_parsing, s_test_rfc822_utc_parsing_fn)

static int s_test_rfc822_utc_parsing_auto_detect_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "Wed, 02 Oct 2002 08:05:09 GMT";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_AUTO_DETECT));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_RFC822, &str_output));

    ASSERT_BIN_ARRAYS_EQUALS(date_buf.buffer, date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_utc_parsing_auto_detect, s_test_rfc822_utc_parsing_auto_detect_fn)

static int s_test_rfc822_local_time_east_of_gmt_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "Wed, 02 Oct 2002 09:35:09 +0130";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_RFC822));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_RFC822, &str_output));

    const char *expected_str = "Wed, 02 Oct 2002 08:05:09 GMT";
    struct aws_byte_buf expected_buf = aws_byte_buf_from_c_str(expected_str);

    ASSERT_BIN_ARRAYS_EQUALS(expected_buf.buffer, expected_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_local_time_east_of_gmt_parsing, s_test_rfc822_local_time_east_of_gmt_parsing_fn)

static int s_test_rfc822_local_time_west_of_gmt_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "Wed, 02 Oct 2002 07:05:09 -0100";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_RFC822));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_RFC822, &str_output));

    const char *expected_str = "Wed, 02 Oct 2002 08:05:09 GMT";
    struct aws_byte_buf expected_buf = aws_byte_buf_from_c_str(expected_str);

    ASSERT_BIN_ARRAYS_EQUALS(expected_buf.buffer, expected_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_local_time_west_of_gmt_parsing, s_test_rfc822_local_time_west_of_gmt_parsing_fn)

static int s_test_rfc822_utc_two_digit_year_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "Wed, 02 Oct 02 08:05:09 GMT";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_RFC822));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_RFC822, &str_output));

    const char *expected_date_str = "Wed, 02 Oct 2002 08:05:09 GMT";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_utc_two_digit_year_parsing, s_test_rfc822_utc_two_digit_year_parsing_fn)

static int s_test_rfc822_utc_no_dow_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "02 Oct 02 08:05:09 GMT";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_RFC822));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_RFC822, &str_output));

    const char *expected_date_str = "Wed, 02 Oct 2002 08:05:09 GMT";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_utc_no_dow_parsing, s_test_rfc822_utc_no_dow_parsing_fn)

static int s_test_rfc822_utc_dos_prevented_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "Weddkasdiweijbnawei8eriojngsdgasdgsdf1gasd8asdgfasdfgsdikweisdfksdnsdksdklas"
                           "dfsdklasdfdfsdfsdfsdfsadfasdafsdfgjjfgghdfgsdfsfsdfsdfasdfsdfasdfsdfasdfsdf";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_ERROR(
        AWS_ERROR_OVERFLOW_DETECTED, aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_RFC822));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_utc_dos_prevented, s_test_rfc822_utc_dos_prevented_fn)

static int s_test_rfc822_invalid_format_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "Wed, 02 Oct 2002";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_ERROR(
        AWS_ERROR_INVALID_DATE_STR, aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_RFC822));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_invalid_format, s_test_rfc822_invalid_format_fn)

static int s_test_rfc822_invalid_tz_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "Wed, 02 Oct 2002 08:05:09 DST";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_ERROR(
        AWS_ERROR_INVALID_DATE_STR, aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_RFC822));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_invalid_tz, s_test_rfc822_invalid_tz_fn)

static int s_test_rfc822_invalid_auto_format_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "Wed, 02 Oct 2002";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_ERROR(
        AWS_ERROR_INVALID_DATE_STR, aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_AUTO_DETECT));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(rfc822_invalid_auto_format, s_test_rfc822_invalid_auto_format_fn)

static int s_test_iso8601_utc_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "2002-10-02T08:05:09.000Z";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_ISO_8601));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_ISO_8601, &str_output));

    const char *expected_date_str = "2002-10-02T08:05:09Z";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    AWS_ZERO_ARRAY(date_output);
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_short_str(&date_time, AWS_DATE_FORMAT_ISO_8601, &str_output));

    const char *expected_short_str = "2002-10-02";
    struct aws_byte_buf expected_short_buf = aws_byte_buf_from_c_str(expected_short_str);

    ASSERT_BIN_ARRAYS_EQUALS(expected_short_buf.buffer, expected_short_buf.len, str_output.buffer, str_output.len);
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_utc_parsing, s_test_iso8601_utc_parsing_fn)

static int s_test_iso8601_basic_utc_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "20021002T080509000Z";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_ISO_8601_BASIC));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_ISO_8601_BASIC, &str_output));

    const char *expected_date_str = "20021002T080509Z";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    AWS_ZERO_ARRAY(date_output);
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_short_str(&date_time, AWS_DATE_FORMAT_ISO_8601_BASIC, &str_output));

    const char *expected_short_str = "20021002";
    struct aws_byte_buf expected_short_buf = aws_byte_buf_from_c_str(expected_short_str);

    ASSERT_BIN_ARRAYS_EQUALS(expected_short_buf.buffer, expected_short_buf.len, str_output.buffer, str_output.len);
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_basic_utc_parsing, s_test_iso8601_basic_utc_parsing_fn)

static int s_test_iso8601_utc_parsing_auto_detect_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "2002-10-02T08:05:09.000Z";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_AUTO_DETECT));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_ISO_8601, &str_output));

    const char *expected_date_str = "2002-10-02T08:05:09Z";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_utc_parsing_auto_detect, s_test_iso8601_utc_parsing_auto_detect_fn)

static int s_test_iso8601_basic_utc_parsing_auto_detect_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "20021002T080509000Z";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_AUTO_DETECT));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_ISO_8601_BASIC, &str_output));

    const char *expected_date_str = "20021002T080509Z";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_basic_utc_parsing_auto_detect, s_test_iso8601_basic_utc_parsing_auto_detect_fn)

static int s_test_iso8601_utc_no_colon_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "2002-10-02T080509.000Z";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_ISO_8601));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_ISO_8601, &str_output));

    const char *expected_date_str = "2002-10-02T08:05:09Z";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_utc_no_colon_parsing, s_test_iso8601_utc_no_colon_parsing_fn)

static int s_test_iso8601_date_only_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "2002-10-02";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_ISO_8601));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(0, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(0, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(0, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_ISO_8601, &str_output));

    const char *expected_date_str = "2002-10-02T00:00:00Z";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_date_only_parsing, s_test_iso8601_date_only_parsing_fn)

static int s_test_iso8601_basic_date_only_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "20021002";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_SUCCESS(aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_ISO_8601_BASIC));
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(0, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(0, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(0, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_ISO_8601_BASIC, &str_output));

    const char *expected_date_str = "20021002T000000Z";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_basic_date_only_parsing, s_test_iso8601_basic_date_only_parsing_fn)

static int s_test_iso8601_utc_dos_prevented_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "Weddkasdiweijbnawei8eriojngsdgasdgsdf1gasd8asdgfasdfgsdikweisdfksdnsdksdklas"
                           "dfsdklasdfdfsdfsdfsdfsadfasdafsdfgjjfgghdfgsdfsfsdfsdfasdfsdfasdfsdfasdfsdf";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_ERROR(
        AWS_ERROR_OVERFLOW_DETECTED, aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_ISO_8601));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_utc_dos_prevented, s_test_iso8601_utc_dos_prevented_fn)

static int s_test_iso8601_invalid_format_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "2002-10-02T";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_ERROR(
        AWS_ERROR_INVALID_DATE_STR, aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_ISO_8601));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_invalid_format, s_test_iso8601_invalid_format_fn)

static int s_test_iso8601_invalid_auto_format_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;
    const char *date_str = "2002-10-02T";
    struct aws_byte_buf date_buf = aws_byte_buf_from_c_str(date_str);

    ASSERT_ERROR(
        AWS_ERROR_INVALID_DATE_STR, aws_date_time_init_from_str(&date_time, &date_buf, AWS_DATE_FORMAT_AUTO_DETECT));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(iso8601_invalid_auto_format, s_test_iso8601_invalid_auto_format_fn)

static int s_test_unix_epoch_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;

    aws_date_time_init_epoch_secs(&date_time, 1033545909.0);
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_ISO_8601, &str_output));

    const char *expected_date_str = "2002-10-02T08:05:09Z";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(unix_epoch_parsing, s_test_unix_epoch_parsing_fn)

static int s_test_millis_parsing_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;

    struct aws_date_time date_time;

    aws_date_time_init_epoch_millis(&date_time, 1033545909000);
    ASSERT_INT_EQUALS(AWS_DATE_DAY_OF_WEEK_WEDNESDAY, aws_date_time_day_of_week(&date_time, false));
    ASSERT_UINT_EQUALS(2, aws_date_time_month_day(&date_time, false));
    ASSERT_UINT_EQUALS(AWS_DATE_MONTH_OCTOBER, aws_date_time_month(&date_time, false));
    ASSERT_UINT_EQUALS(2002, aws_date_time_year(&date_time, false));
    ASSERT_UINT_EQUALS(8, aws_date_time_hour(&date_time, false));
    ASSERT_UINT_EQUALS(5, aws_date_time_minute(&date_time, false));
    ASSERT_UINT_EQUALS(9, aws_date_time_second(&date_time, false));

    uint8_t date_output[AWS_DATE_TIME_STR_MAX_LEN];
    AWS_ZERO_ARRAY(date_output);
    struct aws_byte_buf str_output = aws_byte_buf_from_array(date_output, sizeof(date_output));
    str_output.len = 0;
    ASSERT_SUCCESS(aws_date_time_to_utc_time_str(&date_time, AWS_DATE_FORMAT_ISO_8601, &str_output));

    const char *expected_date_str = "2002-10-02T08:05:09Z";
    struct aws_byte_buf expected_date_buf = aws_byte_buf_from_c_str(expected_date_str);
    ASSERT_BIN_ARRAYS_EQUALS(expected_date_buf.buffer, expected_date_buf.len, str_output.buffer, str_output.len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(millis_parsing, s_test_millis_parsing_fn)
