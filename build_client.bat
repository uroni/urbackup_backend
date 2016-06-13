call update_deps.bat

call checkout_client.bat
if %errorlevel% neq 0 exit /b %errorlevel%

cd %~dp0

git reset --hard
python build\replace_versions.py
if %errorlevel% neq 0 exit /b %errorlevel% 

call build_client_backend.bat
if %errorlevel% neq 0 exit /b %errorlevel%
cd %~dp0\client
call build_client.bat
if %errorlevel% neq 0 exit /b %errorlevel%

if NOT "%STORE_SYMBOLS%" == "true" GOTO skip_symbols

echo|set /p="set build_revision=" > "build_revision.bat"
git rev-parse HEAD >> "build_revision.bat"
call build_revision.bat

cd "%~dp0"

copy /Y "Release Server 2003\urbackupclient_server03.dll" "Release Server 2003\urbackup_server03.dll"
copy /Y "x64\Release Server 2003\urbackupclient_server03.dll" "x64\Release Server 2003\urbackup_server03.dll"

copy /Y "Release\urbackupclient.dll" "Release\urbackup.dll"
copy /Y "x64\Release\urbackupclient.dll" "x64\Release\urbackup.dll"

copy /Y "Release WinXP\urbackupclient_xp.dll" "Release Server 2003\urbackup_xp.dll"


FOR /F "tokens=*" %%G IN (pdb_dirs_client.txt) DO symstore add /compress /r /f "%~dp0%%G" /s "C:\symstore" /t "UrBackup Client /v "%build_revision%" /c "Release"

:skip_symbols

exit 0