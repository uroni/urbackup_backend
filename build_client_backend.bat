call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="win32"  /p:vcpkgTriplet="x86-windows-static-md" /p:VcpkgCurrentTriplet="x86-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel% 

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="x64"  /p:vcpkgTriplet="x64-windows-static-md" /p:VcpkgCurrentTriplet="x64-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="ARM64"  /p:vcpkgTriplet="arm64-windows-static-md" /p:VcpkgCurrentTriplet="arm64-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="x64"  /p:vcpkgTriplet="x64-windows-static-md"  /p:VcpkgCurrentTriplet="x64-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="win32"  /p:vcpkgTriplet="x86-windows-static-md" /p:VcpkgCurrentTriplet="x86-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="arm64"  /p:vcpkgTriplet="arm64-windows-static-md" /p:VcpkgCurrentTriplet="arm64-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

exit /b 0