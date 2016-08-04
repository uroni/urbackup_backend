call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat"

call update_deps.bat

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="win32"
if %errorlevel% neq 0 exit /b %errorlevel% 

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="x64"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild UrBackupBackend.sln /p:Configuration="Release Service" /p:Platform="x64"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild UrBackupBackend.sln /p:Configuration="Release Service" /p:Platform="win32"
if %errorlevel% neq 0 exit /b %errorlevel%

exit /b 0