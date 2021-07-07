/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/common/byte_buf.h>
#include <aws/common/condition_variable.h>
#include <aws/common/linked_list.h>
#include <aws/common/mutex.h>
#include <aws/common/ring_buffer.h>
#include <aws/common/thread.h>
#include <aws/testing/aws_test_harness.h>

static int s_test_1_to_1_acquire_release_wraps(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    struct aws_ring_buffer ring_buffer;
    size_t buf_size = 16;
    ASSERT_SUCCESS(aws_ring_buffer_init(&ring_buffer, allocator, buf_size));

    struct aws_byte_buf vended_buffer;
    AWS_ZERO_STRUCT(vended_buffer);

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 4, &vended_buffer));
    uint8_t *ptr = vended_buffer.buffer;
    ASSERT_UINT_EQUALS(4, vended_buffer.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer));
    aws_ring_buffer_release(&ring_buffer, &vended_buffer);

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 8, &vended_buffer));
    ASSERT_PTR_EQUALS(ptr, vended_buffer.buffer);
    ASSERT_UINT_EQUALS(8, vended_buffer.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer));
    aws_ring_buffer_release(&ring_buffer, &vended_buffer);

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 4, &vended_buffer));
    ASSERT_PTR_EQUALS(ptr, vended_buffer.buffer);
    ASSERT_UINT_EQUALS(4, vended_buffer.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer));
    aws_ring_buffer_release(&ring_buffer, &vended_buffer);

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 8, &vended_buffer));
    ASSERT_PTR_EQUALS(ptr, vended_buffer.buffer);
    ASSERT_UINT_EQUALS(8, vended_buffer.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer));
    aws_ring_buffer_release(&ring_buffer, &vended_buffer);

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 8, &vended_buffer));
    ASSERT_PTR_EQUALS(ptr, vended_buffer.buffer);
    ASSERT_UINT_EQUALS(8, vended_buffer.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer));
    aws_ring_buffer_release(&ring_buffer, &vended_buffer);

    aws_ring_buffer_clean_up(&ring_buffer);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(ring_buffer_1_to_1_acquire_release_wraps_test, s_test_1_to_1_acquire_release_wraps)

static int s_test_release_after_full(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    struct aws_ring_buffer ring_buffer;
    size_t buf_size = 16;
    ASSERT_SUCCESS(aws_ring_buffer_init(&ring_buffer, allocator, buf_size));

    struct aws_byte_buf vended_buffer_1;
    AWS_ZERO_STRUCT(vended_buffer_1);

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 12, &vended_buffer_1));
    uint8_t *ptr = vended_buffer_1.buffer;
    ASSERT_UINT_EQUALS(12, vended_buffer_1.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_1));

    struct aws_byte_buf vended_buffer_2;
    AWS_ZERO_STRUCT(vended_buffer_2);
    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 4, &vended_buffer_2));
    ASSERT_PTR_EQUALS(ptr + 12, vended_buffer_2.buffer);
    ASSERT_UINT_EQUALS(4, vended_buffer_2.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_2));

    ASSERT_ERROR(AWS_ERROR_OOM, aws_ring_buffer_acquire(&ring_buffer, 1, &vended_buffer_1));

    aws_ring_buffer_release(&ring_buffer, &vended_buffer_1);

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 8, &vended_buffer_2));
    ASSERT_PTR_EQUALS(ptr, vended_buffer_2.buffer);
    ASSERT_UINT_EQUALS(8, vended_buffer_2.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_2));
    aws_ring_buffer_release(&ring_buffer, &vended_buffer_2);

    aws_ring_buffer_clean_up(&ring_buffer);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(ring_buffer_release_after_full_test, s_test_release_after_full)

static int s_test_acquire_up_to(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    struct aws_ring_buffer ring_buffer;
    size_t buf_size = 16;
    ASSERT_SUCCESS(aws_ring_buffer_init(&ring_buffer, allocator, buf_size));

    struct aws_byte_buf vended_buffer_1;
    AWS_ZERO_STRUCT(vended_buffer_1);

    ASSERT_SUCCESS(aws_ring_buffer_acquire_up_to(&ring_buffer, 1, 12, &vended_buffer_1));
    uint8_t *ptr = vended_buffer_1.buffer;
    ASSERT_UINT_EQUALS(12, vended_buffer_1.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_1));

    struct aws_byte_buf vended_buffer_2;
    AWS_ZERO_STRUCT(vended_buffer_2);

    /* only 4 are available, so this should error. */
    ASSERT_ERROR(AWS_ERROR_OOM, aws_ring_buffer_acquire_up_to(&ring_buffer, 5, 8, &vended_buffer_2));

    ASSERT_SUCCESS(aws_ring_buffer_acquire_up_to(&ring_buffer, 4, 8, &vended_buffer_2));
    ASSERT_PTR_EQUALS(ptr + 12, vended_buffer_2.buffer);
    ASSERT_UINT_EQUALS(4, vended_buffer_2.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_2));

    ASSERT_ERROR(AWS_ERROR_OOM, aws_ring_buffer_acquire_up_to(&ring_buffer, 1, 1, &vended_buffer_1));

    aws_ring_buffer_release(&ring_buffer, &vended_buffer_1);
    aws_ring_buffer_release(&ring_buffer, &vended_buffer_2);

    ASSERT_SUCCESS(aws_ring_buffer_acquire_up_to(&ring_buffer, 1, 8, &vended_buffer_1));
    ASSERT_PTR_EQUALS(ptr, vended_buffer_1.buffer);
    ASSERT_UINT_EQUALS(8, vended_buffer_1.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_1));

    ASSERT_SUCCESS(aws_ring_buffer_acquire_up_to(&ring_buffer, 1, 8, &vended_buffer_2));
    ASSERT_PTR_EQUALS(ptr + 8, vended_buffer_2.buffer);
    ASSERT_UINT_EQUALS(8, vended_buffer_2.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_2));

    aws_ring_buffer_release(&ring_buffer, &vended_buffer_1);
    aws_ring_buffer_release(&ring_buffer, &vended_buffer_2);

    aws_ring_buffer_clean_up(&ring_buffer);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(ring_buffer_acquire_up_to_test, s_test_acquire_up_to)

static int s_test_acquire_tail_always_chases_head(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    struct aws_ring_buffer ring_buffer;
    size_t buf_size = 16;
    ASSERT_SUCCESS(aws_ring_buffer_init(&ring_buffer, allocator, buf_size));

    struct aws_byte_buf vended_buffer_1;
    AWS_ZERO_STRUCT(vended_buffer_1);

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 12, &vended_buffer_1));
    uint8_t *ptr = vended_buffer_1.buffer;
    ASSERT_UINT_EQUALS(12, vended_buffer_1.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_1));

    struct aws_byte_buf vended_buffer_2;
    AWS_ZERO_STRUCT(vended_buffer_2);
    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 4, &vended_buffer_2));
    ASSERT_PTR_EQUALS(ptr + 12, vended_buffer_2.buffer);
    ASSERT_UINT_EQUALS(4, vended_buffer_2.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_2));

    ASSERT_ERROR(AWS_ERROR_OOM, aws_ring_buffer_acquire(&ring_buffer, 1, &vended_buffer_1));

    aws_ring_buffer_release(&ring_buffer, &vended_buffer_1);

    /* we should turn over right here*/
    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 8, &vended_buffer_1));
    ASSERT_PTR_EQUALS(ptr, vended_buffer_1.buffer);
    ASSERT_UINT_EQUALS(8, vended_buffer_1.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_1));

    aws_ring_buffer_release(&ring_buffer, &vended_buffer_2);

    ASSERT_ERROR(AWS_ERROR_OOM, aws_ring_buffer_acquire(&ring_buffer, 8, &vended_buffer_2));

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 7, &vended_buffer_2));
    ASSERT_PTR_EQUALS(ptr + 8, vended_buffer_2.buffer);
    ASSERT_UINT_EQUALS(7, vended_buffer_2.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_2));
    /* tail will flip here. */
    aws_ring_buffer_release(&ring_buffer, &vended_buffer_1);

    ASSERT_ERROR(AWS_ERROR_OOM, aws_ring_buffer_acquire(&ring_buffer, 8, &vended_buffer_1));

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 7, &vended_buffer_1));
    ASSERT_PTR_EQUALS(ptr, vended_buffer_1.buffer);
    ASSERT_UINT_EQUALS(7, vended_buffer_1.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_1));

    aws_ring_buffer_release(&ring_buffer, &vended_buffer_2);

    ASSERT_ERROR(AWS_ERROR_OOM, aws_ring_buffer_acquire(&ring_buffer, 8, &vended_buffer_2));

    ASSERT_SUCCESS(aws_ring_buffer_acquire(&ring_buffer, 7, &vended_buffer_2));
    ASSERT_PTR_EQUALS(ptr + 7, vended_buffer_2.buffer);
    ASSERT_UINT_EQUALS(7, vended_buffer_2.capacity);
    ASSERT_TRUE(aws_ring_buffer_buf_belongs_to_pool(&ring_buffer, &vended_buffer_2));
    aws_ring_buffer_clean_up(&ring_buffer);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(ring_buffer_acquire_tail_always_chases_head_test, s_test_acquire_tail_always_chases_head)

struct mt_test_data {
    struct aws_ring_buffer ring_buf;
    struct aws_linked_list buffer_queue;
    struct aws_mutex mutex;
    struct aws_condition_variable termination_signal;
    int consumer_count;
    int max_count;
    bool consumer_finished;
    bool match_failed;
};

struct mt_test_buffer_node {
    struct aws_linked_list_node node;
    struct aws_byte_buf buf;
};

/* why so high? because the up_to allocs can get REALLY fragmented. */
#define MT_BUFFER_COUNT 60
#define MT_TEST_BUFFER_SIZE 16

static void s_consumer_thread(void *args) {
    struct mt_test_data *test_data = args;

    while (test_data->consumer_count < test_data->max_count) {
        aws_mutex_lock(&test_data->mutex);

        struct aws_linked_list_node *node = NULL;

        if (!aws_linked_list_empty(&test_data->buffer_queue)) {
            node = aws_linked_list_pop_front(&test_data->buffer_queue);
        }

        aws_mutex_unlock(&test_data->mutex);

        if (!node) {
            continue;
        }

        struct mt_test_buffer_node *buffer_node = AWS_CONTAINER_OF(node, struct mt_test_buffer_node, node);

        char counter_data[MT_TEST_BUFFER_SIZE + 1];
        AWS_ZERO_ARRAY(counter_data);

        size_t written = 0;
        int num_to_write = test_data->consumer_count++;
        /* all this does is print count out as far as it can to fill the buffer. */
        while (written < buffer_node->buf.capacity) {
            int bytes_written =
                snprintf(counter_data + written, buffer_node->buf.capacity - written, "%d", num_to_write);

            if (bytes_written > 0 && bytes_written < (int)(buffer_node->buf.capacity - written)) {
                written += bytes_written;
            } else {
                break;
            }
        }

        int not_matched = memcmp(buffer_node->buf.buffer, counter_data, written);

        if (not_matched) {
            fprintf(stderr, "match failed!\n");
            fprintf(stderr, "produced buffer was ");
            fwrite(buffer_node->buf.buffer, 1, buffer_node->buf.capacity, stderr);
            fprintf(stderr, " but we were expecting %s\n", counter_data);
            test_data->match_failed = true;
            aws_ring_buffer_release(&test_data->ring_buf, &buffer_node->buf);
            break;
        }

        aws_ring_buffer_release(&test_data->ring_buf, &buffer_node->buf);
    }

    aws_mutex_lock(&test_data->mutex);
    test_data->consumer_finished = true;
    aws_mutex_unlock(&test_data->mutex);

    aws_condition_variable_notify_one(&test_data->termination_signal);
}

static bool s_termination_predicate(void *args) {
    struct mt_test_data *test_data = args;

    return test_data->consumer_finished;
}

static int s_acquire_up_to_wrapper(struct aws_ring_buffer *ring_buf, size_t requested, struct aws_byte_buf *dest) {
    if (requested >= 4) {
        return aws_ring_buffer_acquire_up_to(ring_buf, 4, requested, dest);
    }

    return aws_ring_buffer_acquire_up_to(ring_buf, 1, requested, dest);
}

static int s_test_acquire_any_muti_threaded(
    struct aws_allocator *allocator,
    int (*acquire_fn)(struct aws_ring_buffer *, size_t, struct aws_byte_buf *)) {
    /* spin up a consumer thread, let current thread be the producer. Let them fight it out and give a chance
     * for race conditions to happen and explode the universe. */

    struct mt_test_data test_data = {
        .match_failed = false,
        .consumer_count = 0,
        .mutex = AWS_MUTEX_INIT,
        .max_count = 1000000,
        .consumer_finished = false,
        .termination_signal = AWS_CONDITION_VARIABLE_INIT,
    };

    static struct mt_test_buffer_node s_buffer_nodes[MT_BUFFER_COUNT];

    /* 3 16 byte acquirable buffers + 15 bytes == 63 */
    ASSERT_SUCCESS(aws_ring_buffer_init(&test_data.ring_buf, allocator, 3 * MT_TEST_BUFFER_SIZE + 15));
    aws_linked_list_init(&test_data.buffer_queue);

    struct aws_thread consumer_thread;

    ASSERT_SUCCESS(aws_thread_init(&consumer_thread, allocator));
    ASSERT_SUCCESS(aws_thread_launch(&consumer_thread, s_consumer_thread, &test_data, NULL));

    int counter = 0;

    /* consumer_finished isn't protected and we don't need it to be immediately and it won't rip,
     * we just need it eventually if the consumer thread fails prematurely. */
    while (counter < test_data.max_count && !test_data.consumer_finished) {
        struct aws_byte_buf dest;
        AWS_ZERO_STRUCT(dest);

        if (!acquire_fn(&test_data.ring_buf, MT_TEST_BUFFER_SIZE, &dest)) {
            size_t written = 0;
            memset(dest.buffer, 0, dest.capacity);
            /* all this does is print count out as far as it can to fill the buffer. */
            while (written < dest.capacity) {
                int bytes_written = snprintf((char *)dest.buffer + written, dest.capacity - written, "%d", counter);

                if (bytes_written > 0 && bytes_written < (int)(dest.capacity - written)) {
                    written += bytes_written;
                } else {
                    break;
                }
            }

            int index = counter % MT_BUFFER_COUNT;
            s_buffer_nodes[index].buf = dest;
            counter++;

            aws_mutex_lock(&test_data.mutex);
            aws_linked_list_push_back(&test_data.buffer_queue, &s_buffer_nodes[index].node);
            aws_mutex_unlock(&test_data.mutex);
        }
    }

    aws_mutex_lock(&test_data.mutex);
    aws_condition_variable_wait_pred(
        &test_data.termination_signal, &test_data.mutex, s_termination_predicate, &test_data);
    aws_mutex_unlock(&test_data.mutex);

    aws_thread_join(&consumer_thread);

    aws_ring_buffer_clean_up(&test_data.ring_buf);
    aws_thread_clean_up(&consumer_thread);

    ASSERT_FALSE(test_data.match_failed);

    return AWS_OP_SUCCESS;
}

static int s_test_acquire_multi_threaded(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_test_acquire_any_muti_threaded(allocator, aws_ring_buffer_acquire);
}

AWS_TEST_CASE(ring_buffer_acquire_multi_threaded_test, s_test_acquire_multi_threaded)

static int s_test_acquire_up_to_multi_threaded(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;

    return s_test_acquire_any_muti_threaded(allocator, s_acquire_up_to_wrapper);
}

AWS_TEST_CASE(ring_buffer_acquire_up_to_multi_threaded_test, s_test_acquire_up_to_multi_threaded)

#define RING_BUFFER_ALLOCATOR_CAPACITY (16 * 1024)

static int s_test_ring_buffer_allocator(struct aws_allocator *allocator, void *ctx) {
    (void)ctx;
    struct aws_ring_buffer ring_buffer;
    struct aws_allocator rb_allocator;
    ASSERT_SUCCESS(aws_ring_buffer_init(&ring_buffer, allocator, RING_BUFFER_ALLOCATOR_CAPACITY));
    ASSERT_SUCCESS(aws_ring_buffer_allocator_init(&rb_allocator, &ring_buffer));

    for (int cycles = 0; cycles < 10; ++cycles) {
        size_t total_size = 0;
        const size_t chunk_size = (cycles + 1) * 16;
        int chunk_count = 0;
        AWS_VARIABLE_LENGTH_ARRAY(void *, chunks, RING_BUFFER_ALLOCATOR_CAPACITY / (16 + sizeof(size_t)));
        while ((total_size + chunk_size) <= RING_BUFFER_ALLOCATOR_CAPACITY) {
            void *chunk = aws_mem_calloc(&rb_allocator, 1, chunk_size);
            if (chunk == NULL) {
                ASSERT_TRUE(chunk_count > 0);
                break;
            }
            chunks[chunk_count++] = chunk;
            total_size += chunk_size;
        }

        for (int idx = 0; idx < chunk_count; ++idx) {
            aws_mem_release(&rb_allocator, chunks[idx]);
        }
    }

    aws_ring_buffer_allocator_clean_up(&rb_allocator);
    aws_ring_buffer_clean_up(&ring_buffer);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(ring_buffer_allocator_test, s_test_ring_buffer_allocator);
