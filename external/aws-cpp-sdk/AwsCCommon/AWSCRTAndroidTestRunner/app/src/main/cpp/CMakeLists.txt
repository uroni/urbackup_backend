# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

# AWS lib
set(path_to_common "${CMAKE_CURRENT_LIST_DIR}/../../../../..")
get_filename_component(path_to_common ${path_to_common} ABSOLUTE)

# This is required in order to append /lib/cmake to each element in CMAKE_PREFIX_PATH
set(AWS_MODULE_DIR "/${CMAKE_INSTALL_LIBDIR}/cmake")
string(REPLACE ";" "${AWS_MODULE_DIR};" AWS_MODULE_PATH "${CMAKE_PREFIX_PATH}${AWS_MODULE_DIR}")
# Append that generated list to the module search path
list(APPEND CMAKE_MODULE_PATH ${AWS_MODULE_PATH})

list(APPEND CMAKE_MODULE_PATH "${path_to_common}/cmake")
include(AwsFindPackage)
set(IN_SOURCE_BUILD ON)
set(BUILD_SHARED_LIBS ON)

# We will generate our own tests, the tests that are there depend on CTest
set(ALLOW_CROSS_COMPILED_TESTS ON)
set(BUILD_TESTING ON)
add_subdirectory(${path_to_common} ${CMAKE_CURRENT_BINARY_DIR}/aws-c-common)
aws_use_package(aws-c-common)

function(import_tests test_cmakelists)
    get_property(TEST_CASES GLOBAL PROPERTY AWS_TEST_CASES)

    # Generate Kotlin test classes
    get_filename_component(testrunner_path "../../androidTest/java/software/amazon/awssdk/crt/awscrtandroidtestrunner" ABSOLUTE)
    foreach(name IN LISTS TEST_CASES)
        set(TEST_NAME "${name}")
        configure_file(
            "${testrunner_path}/NativeTest.kt.in"
            "${testrunner_path}/tests/NativeTest_${name}.kt"
        )
    endforeach()
endfunction()

file(GLOB test_src "${path_to_common}/tests/*.c")
file(GLOB test_logging_src
        "${path_to_common}/tests/logging/logging_test_utilities.c"
        "${path_to_common}/tests/logging/test_logger.c")
set(test_src ${test_src} ${test_logging_src})
import_tests(${path_to_common}/tests/CMakeLists.txt)

# JNI Lib
add_library(native-lib SHARED native-lib.cpp ${test_src})
find_library(log-lib log)
target_include_directories(native-lib PUBLIC
        "${path_to_common}/include"
        "${path_to_common}/tests"
        "${CMAKE_CURRENT_BINARY_DIR}/aws-c-common/generated/include")
target_compile_definitions(native-lib PRIVATE AWS_UNSTABLE_TESTING_API=1)
target_link_libraries(native-lib  ${log-lib} ${DEP_AWS_LIBS})
