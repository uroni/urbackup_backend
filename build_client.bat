call checkout_client.bat
if %errorlevel% neq 0 exit /b %errorlevel%
cd %~dp0
call build_client_backend.bat
if %errorlevel% neq 0 exit /b %errorlevel%
cd %~dp0\client
call build_client.bat
if %errorlevel% neq 0 exit /b %errorlevel%

if NOT "%STORE_SYMBOLS%" == "true" GOTO skip_symbols

echo|set /p="set build_revision=" > "build_revision.bat"
git rev-parse HEAD >> "build_revision.bat"
call build_revision.bat

FOR /F "tokens=*" %%G IN (pdb_dirs_client.txt) DO symstore add /compress /r /f "%~dp0%%G" /s "C:\symstore" /t "UrBackup Client /v "%build_revision%" /c "Release"

:skip_symbols

exit 0