call "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"

git reset --hard
python build\replace_versions.py
if %errorlevel% neq 0 exit /b %errorlevel% 

copy /Y "%~dp0server-license.txt" "%~dp0urbackupserver_installer_win\data_common\server-license.txt"
if %errorlevel% neq 0 exit /b %errorlevel% 

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="win32"  /p:vcpkgTriplet="x86-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel% 

msbuild UrBackupBackend.sln /p:Configuration=Release /p:Platform="x64"  /p:vcpkgTriplet="x64-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="x64"  /p:vcpkgTriplet="x64-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

msbuild CompiledServer.vcxproj /p:Configuration="Release Service" /p:Platform="win32" /p:vcpkgTriplet="x86-windows-static-md"
if %errorlevel% neq 0 exit /b %errorlevel%

call "%~dp0urbackupserver_installer_win/generate_msi.bat"
if %errorlevel% neq 0 exit /b %errorlevel%

"C:\Program Files (x86)\NSIS\makensis.exe" "%~dp0urbackupserver_installer_win/urbackup_server.nsi"
if %errorlevel% neq 0 exit /b %errorlevel%

if NOT "%STORE_SYMBOLS%" == "true" GOTO skip_symbols

echo|set /p="set build_revision=" > "build_revision.bat"
git rev-parse HEAD >> "build_revision.bat"
call build_revision.bat

cd "%~dp0"
FOR /F "tokens=*" %%G IN (pdb_dirs_server.txt) DO symstore add /compress /r /f "%~dp0%%G" /s "C:\symstore" /t "UrBackup Server /v "%build_revision%" /c "Release"

:skip_symbols

exit /b 0