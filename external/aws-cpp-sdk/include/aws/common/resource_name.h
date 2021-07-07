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
#pragma once

#include <aws/common/byte_buf.h>
#include <aws/common/common.h>

struct aws_resource_name {
    struct aws_byte_cursor partition;
    struct aws_byte_cursor service;
    struct aws_byte_cursor region;
    struct aws_byte_cursor account_id;
    struct aws_byte_cursor resource_id;
};

AWS_EXTERN_C_BEGIN

/**
    Given an ARN "Amazon Resource Name" represented as an in memory a
    structure representing the parts
*/
AWS_COMMON_API
int aws_resource_name_init_from_cur(struct aws_resource_name *arn, const struct aws_byte_cursor *string);

/**
    Calculates the space needed to write an ARN to a byte buf
*/
AWS_COMMON_API
int aws_resource_name_length(const struct aws_resource_name *arn, size_t *size);

/**
    Serializes an ARN structure into the lexical string format
*/
AWS_COMMON_API
int aws_byte_buf_append_resource_name(struct aws_byte_buf *buf, const struct aws_resource_name *arn);

AWS_EXTERN_C_END
