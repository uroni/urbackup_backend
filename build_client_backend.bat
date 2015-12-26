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

msbuild UrBackupBackend.sln /p:Configuration="Release x64" /p:Platform="x64"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild urbackupclient\urbackupclient.vcxproj /p:Configuration="Release Server 2003" /p:Platform="win32"
if %errorlevel% neq 0 exit /b %errorlevel%

mkdir "Release Server 2003"
copy /Y "urbackupclient\Release Server 2003\*" "Release Server 2003\"

msbuild urbackupclient\urbackupclient.vcxproj /p:Configuration="Release Server 2003" /p:Platform="x64"
if %errorlevel% neq 0 exit /b %errorlevel%

mkdir "x64\Release Server 2003"
copy /Y "urbackupclient\x64\Release Server 2003\*" "x64\Release Server 2003\"

msbuild urbackupclient\urbackupclient.vcxproj /p:Configuration="Release WinXP" /p:Platform="win32"
if %errorlevel% neq 0 exit /b %errorlevel%

mkdir "Release WinXP"
copy /Y "urbackupclient\Release WinXP\*" "Release WinXP\"

exit /b 0