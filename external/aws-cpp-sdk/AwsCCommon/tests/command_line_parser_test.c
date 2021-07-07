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
#include <aws/common/command_line_parser.h>
#include <aws/testing/aws_test_harness.h>

/* If this is tested from a dynamic library, the static state needs to be reset */
static void s_reset_static_state(void) {
    aws_cli_optind = 1;
}

static int s_test_short_argument_parse_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    struct aws_cli_option options[] = {
        {.name = NULL, .has_arg = AWS_CLI_OPTIONS_NO_ARGUMENT, .flag = NULL, .val = 'a'},
        {.name = "beeee", .has_arg = AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, .flag = NULL, .val = 'b'},
        {.name = NULL, .has_arg = AWS_CLI_OPTIONS_OPTIONAL_ARGUMENT, .flag = NULL, .val = 'c'},
        {.name = NULL, .has_arg = 0, .flag = NULL, .val = 0},
    };

    char *const args[] = {
        "prog-name",
        "-a",
        "-b",
        "bval",
        "-c",
    };
    int argc = 5;
    int longindex = 0;
    s_reset_static_state();
    int arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('a', arg);
    ASSERT_INT_EQUALS(0, longindex);
    ASSERT_INT_EQUALS(2, aws_cli_optind);
    arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('b', arg);
    ASSERT_STR_EQUALS("bval", aws_cli_optarg);
    ASSERT_INT_EQUALS(1, longindex);
    ASSERT_INT_EQUALS(4, aws_cli_optind);
    arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('c', arg);
    ASSERT_INT_EQUALS(2, longindex);
    ASSERT_INT_EQUALS(-1, aws_cli_getopt_long(argc, args, "ab:c", options, &longindex));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(short_argument_parse, s_test_short_argument_parse_fn)

static int s_test_long_argument_parse_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    struct aws_cli_option options[] = {
        {.name = "aaee", .has_arg = AWS_CLI_OPTIONS_NO_ARGUMENT, .flag = NULL, .val = 'a'},
        {.name = "beeee", .has_arg = AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, .flag = NULL, .val = 'b'},
        {.name = "cceeee", .has_arg = AWS_CLI_OPTIONS_OPTIONAL_ARGUMENT, .flag = NULL, .val = 'c'},
        {.name = NULL, .has_arg = 0, .flag = NULL, .val = 0},
    };

    char *const args[] = {
        "prog-name",
        "--aaee",
        "--beeee",
        "bval",
        "-cceeee",
    };
    int argc = 5;
    int longindex = 0;
    s_reset_static_state();
    int arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('a', arg);
    ASSERT_INT_EQUALS(0, longindex);
    ASSERT_INT_EQUALS(2, aws_cli_optind);
    arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('b', arg);
    ASSERT_STR_EQUALS("bval", aws_cli_optarg);
    ASSERT_INT_EQUALS(1, longindex);
    ASSERT_INT_EQUALS(4, aws_cli_optind);
    arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('c', arg);
    ASSERT_INT_EQUALS(2, longindex);

    ASSERT_INT_EQUALS(-1, aws_cli_getopt_long(argc, args, "ab:c", options, &longindex));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(long_argument_parse, s_test_long_argument_parse_fn)

static int s_test_unqualified_argument_parse_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    struct aws_cli_option options[] = {
        {.name = "aaee", .has_arg = AWS_CLI_OPTIONS_NO_ARGUMENT, .flag = NULL, .val = 'a'},
        {.name = "beeee", .has_arg = AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, .flag = NULL, .val = 'b'},
        {.name = "cceeee", .has_arg = AWS_CLI_OPTIONS_OPTIONAL_ARGUMENT, .flag = NULL, .val = 'c'},
        {.name = NULL, .has_arg = 0, .flag = NULL, .val = 0},
    };

    char *const args[] = {"prog-name", "-a", "--beeee", "bval", "-c", "operand"};
    int argc = 6;
    int longindex = 0;
    s_reset_static_state();
    int arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('a', arg);
    ASSERT_INT_EQUALS(0, longindex);
    ASSERT_INT_EQUALS(2, aws_cli_optind);
    arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('b', arg);
    ASSERT_STR_EQUALS("bval", aws_cli_optarg);
    ASSERT_INT_EQUALS(1, longindex);
    ASSERT_INT_EQUALS(4, aws_cli_optind);
    arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('c', arg);
    ASSERT_INT_EQUALS(2, longindex);

    ASSERT_INT_EQUALS(-1, aws_cli_getopt_long(argc, args, "ab:c", options, &longindex));
    ASSERT_TRUE(aws_cli_optind < argc);
    ASSERT_STR_EQUALS("operand", args[aws_cli_optind++]);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(unqualified_argument_parse, s_test_unqualified_argument_parse_fn)

static int s_test_unknown_argument_parse_fn(struct aws_allocator *allocator, void *ctx) {
    (void)allocator;
    (void)ctx;
    struct aws_cli_option options[] = {
        {.name = "aaee", .has_arg = AWS_CLI_OPTIONS_NO_ARGUMENT, .flag = NULL, .val = 'a'},
        {.name = "beeee", .has_arg = AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, .flag = NULL, .val = 'b'},
        {.name = "cceeee", .has_arg = AWS_CLI_OPTIONS_OPTIONAL_ARGUMENT, .flag = NULL, .val = 'c'},
        {.name = NULL, .has_arg = 0, .flag = NULL, .val = 0},
    };

    char *const args[] = {"prog-name", "-BOO!", "--beeee", "bval", "-c", "operand"};
    int argc = 6;
    int longindex = 0;
    s_reset_static_state();
    int arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('?', arg);
    ASSERT_INT_EQUALS(0, longindex);
    ASSERT_INT_EQUALS(2, aws_cli_optind);
    arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('b', arg);
    ASSERT_STR_EQUALS("bval", aws_cli_optarg);
    ASSERT_INT_EQUALS(1, longindex);
    ASSERT_INT_EQUALS(4, aws_cli_optind);
    arg = aws_cli_getopt_long(argc, args, "ab:c", options, &longindex);
    ASSERT_INT_EQUALS('c', arg);
    ASSERT_INT_EQUALS(2, longindex);

    ASSERT_INT_EQUALS(-1, aws_cli_getopt_long(argc, args, "ab:c", options, &longindex));
    ASSERT_TRUE(aws_cli_optind < argc);
    ASSERT_STR_EQUALS("operand", args[aws_cli_optind++]);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(unknown_argument_parse, s_test_unknown_argument_parse_fn)
