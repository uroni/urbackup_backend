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

#define AWS_STATIC_LOG_LEVEL 6

#include "logging_test_utilities.h"

/**
 * A log testing callback that makes a LOGF_ call for each level.
 *
 * Because AWS_STATIC_LOG_LEVEL is 6 (TRACE) in this translation unit, we do not expect any
 * of the log calls to be removed at compile time.
 *
 */
DECLARE_LOGF_ALL_LEVELS_FUNCTION(s_logf_all_levels_trace_cutoff)

TEST_LEVEL_FILTER(AWS_LL_TRACE, "123456", s_logf_all_levels_trace_cutoff)
