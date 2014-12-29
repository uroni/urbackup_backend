REM Copyright (c) 2014 Martin Raiber 
REM
REM Permission is hereby granted, free of charge, to any person obtaining a 
REM copy of this software and associated documentation files (the 
REM "Software"), to deal in the Software without restriction, including 
REM without limitation the rights to use, copy, modify, merge, publish, 
REM distribute, sublicense, and/or sell copies of the Software, and to 
REM permit persons to whom the Software is furnished to do so, subject to 
REM the following conditions: 
REM
REM The above copyright notice and this permission notice shall be included 
REM in all copies or substantial portions of the Software. 
REM
REM THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS 
REM OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
REM MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
REM IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
REM CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
REM TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
REM SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 

@echo off

call %~dp0\mariadb.conf.bat

echo Starting backup of MariaDB at %DATE% %TIME%... 1>&2
"%MARIADB_DUMP%" --user=%MARIADB_BACKUP_USER% --password=%MARIADB_BACKUP_PASSWORD% --all-databases
set RETVAL=%ERRORLEVEL%
echo Backup of MariaDB finished at %DATE% %TIME%. 1>&2
exit /b %RETVAL%