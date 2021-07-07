#!/bin/sh -x

set -e

for path in "$@"
do
    INCLUDE_PATHS="$INCLUDE_PATHS -I $path"
done

cppcheck                                                    \
                                                            \
--enable=all --std=c99 --language=c                         \
--template='[{file}:{line}]: ({severity},{id}){message}'    \
--force --error-exitcode=-1                                 \
                                                            \
-I include                                                  \
$INCLUDE_PATHS                                              \
                                                            \
--suppress=missingIncludeSystem                             \
--suppress=unusedFunction                                   \
--suppress=allocaCalled --suppress=obsoleteFunctionsalloca  \
                                                            \
-q .
