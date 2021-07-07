
mkdir build
cd build
cmake %* -DCMAKE_BUILD_TYPE="RelWithDebInfo" -DCMAKE_INSTALL_PREFIX=../../install ../ || goto error
msbuild.exe aws-checksums.vcxproj /p:Configuration=RelWithDebInfo || goto error
msbuild.exe aws-checksums-tests.vcxproj /p:Configuration=RelWithDebInfo || goto error
ctest -V

goto :EOF

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%
