call checkout_client.bat
if %errorlevel% neq 0 exit /b %errorlevel%
cd %~dp0
call build_client_backend.bat
if %errorlevel% neq 0 exit /b %errorlevel%
cd %~dp0\client
call build_client.bat
if %errorlevel% neq 0 exit /b %errorlevel%
exit 0