@echo off

net session >nul 2>&1
if NOT %errorLevel% == 0 (
	echo Failure: Current permissions inadequate. Please run as administrator.
	pause
	exit 1
)

set /p newpw=Please enter the new password for user 'admin': 

"%~dp0\urbackup_srv.exe" --cmdline --no-server --plugin urbackupserver.dll --set_admin_pw "%newpw%"
pause