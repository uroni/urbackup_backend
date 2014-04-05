cd "%~dp0"

mkdir data_service
copy /Y "..\Release Service\Server.exe" "data_service\urbackup_srv.exe"
if %errorlevel% neq 0 exit /b %errorlevel% 

mkdir data
copy /Y "..\Release Server\urbackupserver.dll" "data\urbackupserver.dll"
if %errorlevel% neq 0 exit /b %errorlevel% 

copy /Y "..\Release Server PreVista\urbackupserver.dll" "data\urbackupserver_prevista.dll"
if %errorlevel% neq 0 exit /b %errorlevel% 

copy /Y "..\release\fsimageplugin.dll" "data\fsimageplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel% 

mkdir data_common
mkdir data_common\urbackup
mkdir data_common\urbackup\www
copy /Y "..\urbackupserver\www\*" "data_common\urbackup\www\"
if %errorlevel% neq 0 exit /b %errorlevel% 

copy /Y "..\Release\downloadplugin.dll" "data\downloadplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\Release\httpserver.dll" "data\httpserver.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\Release\urlplugin.dll" "data\urlplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\urbackup\status.htm" "data_common\urbackup\status.htm"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\Release\cryptoplugin.dll" "data\cryptoplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

mkdir data_service_x64
copy /Y "..\x64\Release Service\Server.exe" "data_service_x64\urbackup_srv.exe"
if %errorlevel% neq 0 exit /b %errorlevel%

mkdir data_x64
copy /Y "..\x64\Release Server\urbackupserver.dll" "data_x64\urbackupserver.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\x64\Release Server PreVista\urbackupserver.dll" "data_x64\urbackupserver_prevista.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\x64\release\fsimageplugin.dll" "data_x64\fsimageplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\x64\release\downloadplugin.dll" "data_x64\downloadplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\x64\release\httpserver.dll" "data_x64\httpserver.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\x64\release\urlplugin.dll" "data_x64\urlplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\x64\Release\cryptoplugin.dll" "data_x64\cryptoplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%
