@echo off

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

call %~dp0\mariadb.conf.bat

IF NOT %MARIADB_ENABLED%==0 echo mariadb.bat mariadb_dump.sql
