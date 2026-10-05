call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="win32"
if %errorlevel% neq 0 exit /b %errorlevel% 

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="x64"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="ARM64"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="x64"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="win32"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="arm64"
if %errorlevel% neq 0 exit /b %errorlevel%

exit /b 0