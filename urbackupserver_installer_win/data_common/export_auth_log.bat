@echo off

net session >nul 2>&1
if NOT %errorLevel% == 0 (
	echo Failure: Current permissions inadequate. Please run as administrator.
	pause
	exit /b 1
)

"%~dp0\urbackup_srv.exe" --cmdline --no-server --plugin urbackupserver.dll --app export_auth_log --loglevel debug --logfile app.log

exit /b 0