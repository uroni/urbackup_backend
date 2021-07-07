# Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License").
# You may not use this file except in compliance with the License.
# A copy of the License is located at
#
#  http://aws.amazon.com/apache2.0
#
# or in the "license" file accompanying this file. This file is distributed
# on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
# express or implied. See the License for the specific language governing
# permissions and limitations under the License.

add_executable(aws-c-event-stream-pipe "event_stream_pipe.c")
aws_set_common_properties(aws-c-event-stream-pipe)
target_link_libraries(aws-c-event-stream-pipe ${CMAKE_PROJECT_NAME})
set_target_properties(aws-c-event-stream-pipe PROPERTIES LINKER_LANGUAGE C)
set_property(TARGET aws-c-event-stream-pipe PROPERTY C_STANDARD 99)

add_executable(aws-c-event-stream-write-test-case "event_stream_write_test_case.c")
aws_set_common_properties(aws-c-event-stream-write-test-case)
target_link_libraries(aws-c-event-stream-write-test-case ${CMAKE_PROJECT_NAME})
set_target_properties(aws-c-event-stream-write-test-case PROPERTIES LINKER_LANGUAGE C)
set_property(TARGET aws-c-event-stream-write-test-case PROPERTY C_STANDARD 99)
