set arch=x64
set data_dir=data_x64
set data_common=data_common
set data_python=data_python_x64
set version=1.3

cd "%~dp0"

call update_data.bat
if %errorlevel% neq 0 exit /b %errorlevel% 

heat dir %data_dir% -nologo -sfrag -suid -ag -srd -dir %data_dir% -out urbackup_files_data.wxs -cg UrBackupData -dr URBACKUPDIR -var env.data_dir
if %errorlevel% neq 0 exit /b %errorlevel% 

candle -denv.data_dir=%data_dir% -arch %arch% urbackup_files_data.wxs
if %errorlevel% neq 0 exit /b %errorlevel% 

heat dir data_common -nologo -sfrag -suid -ag -srd -dir data_common -out urbackup_files_data_common.wxs -cg UrBackupDataCommon -dr URBACKUPDIR -var env.data_common
if %errorlevel% neq 0 exit /b %errorlevel% 

candle -denv.data_common=%data_common% -arch %arch% urbackup_files_data_common.wxs
if %errorlevel% neq 0 exit /b %errorlevel% 

heat dir %data_python% -nologo -sfrag -suid -ag -srd -dir %data_python% -out urbackup_files_data_python.wxs -cg UrBackupDataPython -dr URBACKUPDIR -var env.data_python
if %errorlevel% neq 0 exit /b %errorlevel% 

candle -denv.data_python=%data_python% -arch %arch% urbackup_files_data_python.wxs
if %errorlevel% neq 0 exit /b %errorlevel% 


candle urbackup_server.wxs -ext WixFirewallExtension
if %errorlevel% neq 0 exit /b %errorlevel% 

light urbackup_server.wixobj urbackup_files_data_python.wixobj urbackup_files_data.wixobj urbackup_files_data_common.wixobj -ext WixFirewallExtension -ext WixUIExtension -out "UrBackup Server %version%(%arch%).msi"
if %errorlevel% neq 0 exit /b %errorlevel%