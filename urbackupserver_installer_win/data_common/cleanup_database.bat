@echo off

net session >nul 2>&1
if NOT %errorLevel% == 0 (
	echo Failure: Current permissions inadequate. Please run as administrator.
	pause
	exit 1
)

echo Stopping UrBackup Server...
net stop UrBackupWinServer

"%~dp0\urbackup_srv.exe" --cmdline --no-server --plugin urbackupserver.dll --app cleanup_database --loglevel debug --logfile app.log

echo Starting UrBackup Server...
net start UrBackupWinServer

exit 0