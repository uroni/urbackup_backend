#ifndef AWS_COMMON_FIFO_CACHE_H
#define AWS_COMMON_FIFO_CACHE_H
/*
 * Copyright 2010-2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/common/cache.h>

AWS_EXTERN_C_BEGIN

/**
 * Initializes the first-in-first-out cache. Sets up the underlying linked hash table.
 * Once `max_items` elements have been added, the oldest(first-in) item will
 * be removed. For the other parameters, see aws/common/hash_table.h. Hash table
 * semantics of these arguments are preserved.
 */
AWS_COMMON_API
struct aws_cache *aws_cache_new_fifo(
    struct aws_allocator *allocator,
    aws_hash_fn *hash_fn,
    aws_hash_callback_eq_fn *equals_fn,
    aws_hash_callback_destroy_fn *destroy_key_fn,
    aws_hash_callback_destroy_fn *destroy_value_fn,
    size_t max_items);

AWS_EXTERN_C_END

#endif /* AWS_COMMON_FIFO_CACHE_H */
