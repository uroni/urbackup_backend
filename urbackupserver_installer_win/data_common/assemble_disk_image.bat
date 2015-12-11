@echo off

set sourceImages="%1"
if [%1]==[] set sourceImages="SelectViaGUI"

"%~dp0\urbackup_srv.exe" --cmdline --no-server --plugin fsimageplugin.dll --assemble "%sourceImages%" --output_fn "%2"
pause