call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat"

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="win32"
if %errorlevel% neq 0 exit /b %errorlevel% 

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="x64"
if %errorlevel% neq 0 exit /b %errorlevel% 

"%~dp0urbackupserver_installer_win/generate_msi.bat"

%"C:\Program Files (x86)\NSIS\makensis.exe" "%~dp0urbackupserver_installer_win/urbackup_server.nsi"

exit 0