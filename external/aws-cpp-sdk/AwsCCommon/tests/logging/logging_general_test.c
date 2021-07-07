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

#include "logging_test_utilities.h"

DECLARE_LOGF_ALL_LEVELS_FUNCTION(s_logf_all_levels)

/**
 * These tests check the dynamic (run-time) filtering capabilities of the logging
 * system.
 *
 * In each case, we use the test logger and invoke a log operation at each supported level.
 * Using the level (integer value) itself as the log text, we can easily check to see
 * what got filtered and what got through.
 *
 * For example, when the log level is AWS_LL_INFO, we expect
 *   {AWS_LL_FATAL, AWS_LL_ERROR, AWS_LL_WARN, AWS_LL_INFO} calls to all go through ("1234")
 * but
 *   {AWS_LL_DEBUG, AWS_LL_TRACE} to be filtered out ("56")
 */
TEST_LEVEL_FILTER(AWS_LL_TRACE, "123456", s_logf_all_levels)
TEST_LEVEL_FILTER(AWS_LL_DEBUG, "12345", s_logf_all_levels)
TEST_LEVEL_FILTER(AWS_LL_INFO, "1234", s_logf_all_levels)
TEST_LEVEL_FILTER(AWS_LL_WARN, "123", s_logf_all_levels)
TEST_LEVEL_FILTER(AWS_LL_ERROR, "12", s_logf_all_levels)
TEST_LEVEL_FILTER(AWS_LL_FATAL, "1", s_logf_all_levels)
TEST_LEVEL_FILTER(AWS_LL_NONE, "", s_logf_all_levels)
