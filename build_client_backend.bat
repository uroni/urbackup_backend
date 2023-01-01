call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="win32"  /p:vcpkgTriplet="x86-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel% 

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="x64"  /p:vcpkgTriplet="x64-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="x64"  /p:vcpkgTriplet="x64-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="win32"  /p:vcpkgTriplet="x86-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

exit /b 0