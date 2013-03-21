set arch=x64
set data_dir=data_x64
set data_common=data_common
set data_python=data_python_x64
set version=1.2-1

call update_data.bat

heat dir %data_dir% -nologo -sfrag -suid -ag -srd -dir %data_dir% -out urbackup_files_data.wxs -cg UrBackupData -dr URBACKUPDIR -var env.data_dir
candle -denv.data_dir=%data_dir% -arch %arch% urbackup_files_data.wxs
heat dir data_common -nologo -sfrag -suid -ag -srd -dir data_common -out urbackup_files_data_common.wxs -cg UrBackupDataCommon -dr URBACKUPDIR -var env.data_common
candle -denv.data_common=%data_common% -arch %arch% urbackup_files_data_common.wxs
heat dir %data_python% -nologo -sfrag -suid -ag -srd -dir %data_python% -out urbackup_files_data_python.wxs -cg UrBackupDataPython -dr URBACKUPDIR -var env.data_python
candle -denv.data_python=%data_python% -arch %arch% urbackup_files_data_python.wxs


candle urbackup_server.wxs -ext WixFirewallExtension
light urbackup_server.wixobj urbackup_files_data_python.wixobj urbackup_files_data.wixobj urbackup_files_data_common.wixobj -ext WixFirewallExtension -ext WixUIExtension -out "UrBackup Server %version%(%arch%).msi"
pause