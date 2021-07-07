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

void aws_cache_destroy(struct aws_cache *cache) {
    AWS_PRECONDITION(cache);
    cache->vtable->destroy(cache);
}

int aws_cache_find(struct aws_cache *cache, const void *key, void **p_value) {
    AWS_PRECONDITION(cache);
    return cache->vtable->find(cache, key, p_value);
}

int aws_cache_put(struct aws_cache *cache, const void *key, void *p_value) {
    AWS_PRECONDITION(cache);
    return cache->vtable->put(cache, key, p_value);
}

int aws_cache_remove(struct aws_cache *cache, const void *key) {
    AWS_PRECONDITION(cache);
    return cache->vtable->remove(cache, key);
}

void aws_cache_clear(struct aws_cache *cache) {
    AWS_PRECONDITION(cache);
    cache->vtable->clear(cache);
}

size_t aws_cache_get_element_count(const struct aws_cache *cache) {
    AWS_PRECONDITION(cache);
    return cache->vtable->get_element_count(cache);
}

void aws_cache_base_default_destroy(struct aws_cache *cache) {
    aws_linked_hash_table_clean_up(&cache->table);
    aws_mem_release(cache->allocator, cache);
}

int aws_cache_base_default_find(struct aws_cache *cache, const void *key, void **p_value) {
    return (aws_linked_hash_table_find(&cache->table, key, p_value));
}

int aws_cache_base_default_remove(struct aws_cache *cache, const void *key) {
    /* allocated cache memory and the linked list entry will be removed in the
     * callback. */
    return aws_linked_hash_table_remove(&cache->table, key);
}

void aws_cache_base_default_clear(struct aws_cache *cache) {
    /* clearing the table will remove all elements. That will also deallocate
     * any cache entries we currently have. */
    aws_linked_hash_table_clear(&cache->table);
}

size_t aws_cache_base_default_get_element_count(const struct aws_cache *cache) {
    return aws_linked_hash_table_get_element_count(&cache->table);
}
