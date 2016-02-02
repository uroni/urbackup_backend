cd "%~dp0"

mkdir data_service
copy /Y "..\Release Service\Server.exe" "data_service\urbackup_srv.exe"
if %errorlevel% neq 0 exit /b %errorlevel% 

mkdir data
copy /Y "..\Release Server\urbackupserver.dll" "data\urbackupserver.dll"
if %errorlevel% neq 0 exit /b %errorlevel% 

copy /Y "..\release\fsimageplugin.dll" "data\fsimageplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel% 

copy /Y "..\release\fileservplugin.dll" "data\fileservplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

mkdir data_common

copy /Y "..\server-license.txt" "data_common\license.txt"
if %errorlevel% neq 0 exit /b %errorlevel% 

mkdir data_common\urbackup
mkdir data_common\urbackup\www
mkdir data_common\urbackup\www\js
mkdir data_common\urbackup\www\css
mkdir data_common\urbackup\www\images
mkdir data_common\urbackup\www\fonts
copy /Y "..\urbackupserver\www\*" "data_common\urbackup\www\"
if %errorlevel% neq 0 exit /b %errorlevel% 
copy /Y "..\urbackupserver\www\js\*.js" "data_common\urbackup\www\js\"
if %errorlevel% neq 0 exit /b %errorlevel% 
copy /Y "..\urbackupserver\www\css\*.css" "data_common\urbackup\www\css\"
if %errorlevel% neq 0 exit /b %errorlevel% 
copy /Y "..\urbackupserver\www\fonts\*" "data_common\urbackup\www\fonts\"
if %errorlevel% neq 0 exit /b %errorlevel% 
copy /Y "..\urbackupserver\www\images\*.png" "data_common\urbackup\www\images\"
if %errorlevel% neq 0 exit /b %errorlevel% 
copy /Y "..\urbackupserver\www\images\*.gif" "data_common\urbackup\www\images\"
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

copy /Y "..\x64\release\httpserver.dll" "data_x64\httpserver.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\x64\release\urlplugin.dll" "data_x64\urlplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\x64\Release\cryptoplugin.dll" "data_x64\cryptoplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%

copy /Y "..\x64\release\fileservplugin.dll" "data_x64\fileservplugin.dll"
if %errorlevel% neq 0 exit /b %errorlevel%
