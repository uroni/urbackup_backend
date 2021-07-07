#!/bin/bash

SOURCE_FILES=`find source include tests -type f -name '*.h' -o -name '*.c'`
for i in $SOURCE_FILES
do
    clang-tidy -config="`cat .clang-tidy`" $i -- -Iinclude
    if [ $? -ne 1 ]
    then
        echo "$i failed clang-tidy check."
        FAIL=1
    fi
done
