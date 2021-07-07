#Copyright 2010 - 2018 Amazon.com, Inc.or its affiliates.All Rights Reserved.
#
#Licensed under the Apache License, Version 2.0(the "License").
#You may not use this file except in compliance with the License.
#A copy of the License is located at
#
#http: // aws.amazon.com/apache2.0
#
# or in the "license" file accompanying this file.This file is distributed
#on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
#express or implied.See the License for the specific language governing
#permissions and limitations under the License.
include(AwsTestHarness)
enable_testing()

file(GLOB TEST_SRC "*.c")
file(GLOB TESTS ${TEST_SRC})

set(TEST_BINARY_NAME ${CMAKE_PROJECT_NAME}-tests)

add_test_case(test_incoming_no_op_valid)
add_test_case(test_incoming_application_data_no_headers_valid)
add_test_case(test_incoming_application_one_compressed_header_pair_valid)
add_test_case(test_incoming_application_int32_header_valid)

add_test_case(test_outgoing_no_op_valid)
add_test_case(test_outgoing_application_data_no_headers_valid)
add_test_case(test_outgoing_application_one_compressed_header_pair_valid)

add_test_case(test_streaming_decoder_incoming_no_op_valid_single_message)
add_test_case(test_streaming_decoder_incoming_application_no_headers)
add_test_case(test_streaming_decoder_incoming_application_one_compressed_header_pair_valid)
add_test_case(test_streaming_decoder_incoming_multiple_messages)

generate_test_driver(${TEST_BINARY_NAME})
