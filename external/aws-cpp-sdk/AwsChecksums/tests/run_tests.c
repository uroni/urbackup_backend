/*
 * Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <crc_test.c>

int main(int argc, char *argv[]) {

    if (argc < 2) {
        int retVal = 0;
        retVal |= s_test_crc32c();
        retVal |= s_test_crc32();

        printf("Test run finished press any key to exit\n");
        // if this path is being run, it's manually from a console or IDE and the user likely want's to see the results.
        getchar();
        return retVal;
    }

    // I know this looks painful, but it's far less painful than a GTEST dependency and it integrates nicely with CTest.
    if (!strcmp(argv[1], "crc32c")) {
        return s_test_crc32c();
    }
    if (!strcmp(argv[1], "crc32")) {
        return s_test_crc32();
    }

    return 0;
}
