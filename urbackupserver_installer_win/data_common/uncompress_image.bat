@echo off

set decompressFile=%1
if [%1]==[] set decompressFile="SelectViaGUI"

"%~dp0\urbackup_srv.exe" --cmdline --no-server --plugin fsimageplugin.dll --decompress %decompressFile% --output_fn %2
pause