/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 * http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <android/log.h>

#include <aws/common/logging.h>
#include <aws/common/string.h>

#include <stdarg.h>

#define LOGCAT_MAX_BUFFER_SIZE (4 * 1024)

struct logcat_format_data {
    char *buffer;
    size_t bytes_written;
    size_t total_length;
    const char *format;
};

static size_t s_advance_and_clamp_index(size_t current_index, int amount, size_t maximum) {
    size_t next_index = current_index + amount;
    if (next_index > maximum) {
        next_index = maximum;
    }

    return next_index;
}

/* Override this for Android, as time and log level are taken care of by logcat */
static int s_logcat_format(struct logcat_format_data *formatting_data, va_list args) {
    size_t current_index = 0;

    if (formatting_data->total_length == 0) {
        return aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
    }

    /*
     * Use this length for all but the last write, so we guarantee room for the newline even if we get truncated
     */
    size_t fake_total_length = formatting_data->total_length - 1;

    if (current_index < fake_total_length) {
        /*
         * Add thread id and user content separator (" - ")
         */
        aws_thread_id_t current_thread_id = aws_thread_current_thread_id();
        char thread_id[AWS_THREAD_ID_T_REPR_BUFSZ];
        if (aws_thread_id_t_to_string(current_thread_id, thread_id, AWS_THREAD_ID_T_REPR_BUFSZ)) {
            return AWS_OP_ERR;
        }
        int thread_id_written =
            snprintf(formatting_data->buffer + current_index, fake_total_length - current_index, "[%s] ", thread_id);
        if (thread_id_written < 0) {
            return aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
        }
        current_index = s_advance_and_clamp_index(current_index, thread_id_written, fake_total_length);
    }

    if (current_index < fake_total_length) {
        int separator_written =
            snprintf(formatting_data->buffer + current_index, fake_total_length - current_index, " - ");
        current_index = s_advance_and_clamp_index(current_index, separator_written, fake_total_length);
    }

    if (current_index < fake_total_length) {
        /*
         * Now write the actual data requested by the user
         */

        int written_count = vsnprintf(
            formatting_data->buffer + current_index, fake_total_length - current_index, formatting_data->format, args);

        if (written_count < 0) {
            return aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
        }

        current_index = s_advance_and_clamp_index(current_index, written_count, fake_total_length);
    }

    /*
     * End with a newline.
     */
    int newline_written_count =
        snprintf(formatting_data->buffer + current_index, formatting_data->total_length - current_index, "\n");
    if (newline_written_count < 0) {
        return aws_raise_error(AWS_ERROR_UNKNOWN); /* we saved space, so this would be crazy */
    }

    formatting_data->bytes_written = current_index + newline_written_count;

    return AWS_OP_SUCCESS;
}

static struct aws_logger_logcat { enum aws_log_level level; } s_logcat_impl;

static int s_logcat_log(
    struct aws_logger *logger,
    enum aws_log_level log_level,
    aws_log_subject_t subject,
    const char *format,
    ...) {
    (void)logger;

    va_list format_args;
    va_start(format_args, format);

    char buffer[LOGCAT_MAX_BUFFER_SIZE];
    struct logcat_format_data fmt = {
        .buffer = buffer,
        .total_length = AWS_ARRAY_SIZE(buffer),
        .format = format,
    };

    int result = s_logcat_format(&fmt, format_args);

    va_end(format_args);

    if (result != AWS_OP_SUCCESS) {
        return AWS_OP_ERR;
    }

    /* ANDROID_LOG_VERBOSE = 2, ANDROID_LOG_FATAL = 7 */
    const int prio = 0x8 - log_level;
    __android_log_write(prio, aws_log_subject_name(subject), buffer);

    return AWS_OP_SUCCESS;
}

static enum aws_log_level s_logcat_get_log_level(struct aws_logger *logger, aws_log_subject_t subject) {
    (void)subject;
    struct aws_logger_logcat *impl = logger->p_impl;
    return impl->level;
}

static void s_logcat_clean_up(struct aws_logger *logger) {
    logger->p_impl = NULL;
}

static struct aws_logger_vtable s_logcat_vtable = {
    .log = s_logcat_log,
    .get_log_level = s_logcat_get_log_level,
    .clean_up = s_logcat_clean_up,
};

int aws_logger_init_logcat(
    struct aws_logger *logger,
    struct aws_allocator *allocator,
    struct aws_logger_standard_options *options) {

    logger->allocator = allocator;
    logger->vtable = &s_logcat_vtable;
    logger->p_impl = &s_logcat_impl;
    s_logcat_impl.level = options->level;

    return AWS_OP_SUCCESS;
}
