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
#include <process.h>

/**
 * this is just the value it's hard coded to in windows NT and later
 * see https://docs.microsoft.com/en-us/windows/win32/sysinfo/kernel-objects
 * for more information.
 */
static const size_t s_max_handles = 1 << 24;

int aws_get_pid(void) {
    return _getpid();
}

size_t aws_get_soft_limit_io_handles(void) {
    return s_max_handles;
}

size_t aws_get_hard_limit_io_handles(void) {
    return s_max_handles;
}

int aws_set_soft_limit_io_handles(size_t max_handles) {
    (void)max_handles;

    return aws_raise_error(AWS_ERROR_UNIMPLEMENTED);
}
