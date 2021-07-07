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

#include <aws/common/process.h>

#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

int aws_get_pid(void) {
    return (int)getpid();
}

size_t aws_get_soft_limit_io_handles(void) {
    struct rlimit rlimit;
    AWS_ZERO_STRUCT(rlimit);

    AWS_FATAL_ASSERT(
        !getrlimit(RLIMIT_NOFILE, &rlimit) &&
        "getrlimit() should never fail for RLIMIT_NOFILE regardless of user permissions");
    return rlimit.rlim_cur;
}

size_t aws_get_hard_limit_io_handles(void) {
    struct rlimit rlimit;
    AWS_ZERO_STRUCT(rlimit);

    AWS_FATAL_ASSERT(
        !getrlimit(RLIMIT_NOFILE, &rlimit) &&
        "getrlimit() should never fail for RLIMIT_NOFILE regardless of user permissions");
    return rlimit.rlim_max;
}

int aws_set_soft_limit_io_handles(size_t max_handles) {
    size_t hard_limit = aws_get_hard_limit_io_handles();

    if (max_handles > hard_limit) {
        return aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
    }

    struct rlimit rlimit = {
        .rlim_cur = max_handles,
        .rlim_max = hard_limit,
    };

    if (setrlimit(RLIMIT_NOFILE, &rlimit)) {
        return aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
    }

    return AWS_OP_SUCCESS;
}
