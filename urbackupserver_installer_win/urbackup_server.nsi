!define MUI_BRANDINGTEXT "UrBackup Server $version_short$"
!include "${NSISDIR}\Contrib\Modern UI\System.nsh"
!include WinVer.nsh
!include "x64.nsh"
!define MUI_ICON "backup-ok.ico"

SetCompressor /FINAL /SOLID lzma

CRCCheck On
Name "UrBackup Server $version_short$"
OutFile "UrBackup Server $version_short$.exe"
InstallDir "$PROGRAMFILES\UrBackupServer"
RequestExecutionLevel highest

!include "servicelib.nsh"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_LANGDLL_ALLLANGUAGES

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!define MUI_LANGDLL_REGISTRY_ROOT "HKCU" 
!define MUI_LANGDLL_REGISTRY_KEY "Software\UrBackupServer" 
!define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"


!define MUI_CUSTOMFUNCTION_GUIINIT myGuiInit


!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "French"

!insertmacro MUI_RESERVEFILE_LANGDLL

!define Unicode2Ansi "!insertmacro Unicode2Ansi"

!macro Unicode2Ansi String outVar
 System::Call 'kernel32::WideCharToMultiByte(i 0, i 0, w "${String}", i -1, t .s, i ${NSIS_MAX_STRLEN}, i 0, i 0) i'
 Pop "${outVar}"
!macroend  
 

Section "install"
	${If} ${RunningX64}
		!insertmacro DisableX64FSRedirection
		SetRegView 64
	${EndIf}
	
	${If} ${IsWinXP}
		MessageBox MB_OK "Sorry, installation on Windows XP is not supported."
		Quit
	${EndIf}
	
	${If} ${IsWin2003}
		MessageBox MB_OK "Sorry, installation on Windows Server 2003 is not supported."
		Quit
	${EndIf}
	
	${If} ${IsWin2000}
		MessageBox MB_OK "Sorry, installation on Windows 2000 is not supported."
		Quit
	${EndIf}
	
	SetOutPath "$TEMP"
	${If} ${RunningX64}
		; Push $R0
   		; ClearErrors
   		; ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{DA5E371C-6333-3D8A-93A4-6FD5B20BCC6E}" "Version"
	   	; IfErrors 0 VSRedistInstalled64
		; inetc::get "http://www.urserver.de/vc10/vcredist_x64.exe" $TEMP\vcredist_x64.exe
		; Pop $0
		; ExecWait '"$TEMP\vcredist_x64.exe" /q'  
		; Delete '$TEMP\vcredist_x64.exe'
; VSRedistInstalled64:
		File "..\deps\redist\vc_redist_2015.x64.exe"
		ExecWait '"$TEMP\vc_redist_2015.x64.exe" /q /norestart' $0
		${If} $0 != '0'
		${If} $0 != '3010'
		${If} $0 != '1638'
		${If} $0 != '8192'
		${If} $0 != '1641'
		${If} $0 != '1046'
			ExecWait '"$TEMP\vc_redist_2015.x64.exe" /passive /norestart' $0
			${If} $0 != '0'
			${If} $0 != '3010'
				MessageBox MB_OK "Unable to install Visual Studio 2015 runtime. UrBackup needs that runtime."
				Quit
			${EndIf}
			${EndIf}
		${EndIf}
		${EndIf}
		${EndIf}
		${EndIf}
		${EndIf}
		${EndIf}
			
	${Else}
		; ReadRegStr $0 HKLM "SOFTWARE\Microsoft\VisualStudio\10.0\VC\Runtimes\x86" 'Installed'
		; ${If} $0 != '1'
			; ReadRegStr $0 HKLM "SOFTWARE\Microsoft\VisualStudio\10.0\VC\VCRedist\x86" 'Installed'
			; ${If} $0 != '1'
				; inetc::get "http://www.urserver.de/vc10/vcredist_x86.exe" $TEMP\vcredist_x86.exe
				; Pop $0
				; ExecWait '"$TEMP\vcredist_x86.exe" /q'   
				; Delete '$TEMP\vcredist_x86.exe'
			; ${EndIf}
		; ${EndIf}
		File "..\deps\redist\vc_redist_2015.x86.exe"
		ExecWait '"$TEMP\vc_redist_2015.x86.exe" /q /norestart' $0
		${If} $0 != '0'
		${If} $0 != '3010'
		${If} $0 != '1638'
		${If} $0 != '8192'
		${If} $0 != '1641'
		${If} $0 != '1046'
			ExecWait '"$TEMP\vc_redist_2015.x86.exe"  /passive /norestart' $0
			${If} $0 != '0'
			${If} $0 != '3010'
				MessageBox MB_OK "Unable to install Visual Studio 2015 runtime. UrBackup needs that runtime."
				Quit
			${EndIf}
			${EndIf}
		${EndIf}
		${EndIf}
		${EndIf}
		${EndIf}
		${EndIf}
		${EndIf}
	${EndIf}
	
	SetOutPath "$INSTDIR\imdisk"
	File /r "..\deps\redist\imdiskinst\*"
	System::Call 'Kernel32::SetEnvironmentVariable(t, t)i ("IMDISK_SILENT_SETUP", "1").r0'
	nsExec::ExecToLog '"$INSTDIR\imdisk\install.cmd"'
	
	!insertmacro SERVICE running "UrBackupWinServer" ""
	Pop $0
	${If} $0 == "true"
		!insertmacro SERVICE stop "UrBackupWinServer" ""
		!insertmacro SERVICE waitfor "UrBackupWinServer" "status=stopped"
	${EndIf}
	
	Sleep 500
	
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\UrBackupServer" "DisplayName" "UrBackupServer (remove only)"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\UrBackupServer" "UninstallString" "$INSTDIR\Uninstall.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\UrBackupServer" "Path" "$INSTDIR"
	
	SetOutPath "$INSTDIR"
	File "data_common\args.txt"
	File "data_common\license.txt"
	File "data_common\cleanup.bat"
	File "data_common\remove_unknown.bat"
	File "data_common\reset_pw.bat"
	File "data_common\cleanup_database.bat"
	File "data_common\defrag_database.bat"
	File "data_common\urbackup_ecdsa409k1.pub"
	File "data_common\repair_database.bat"
	File "data_common\export_auth_log.bat"
	File "data_common\uncompress_image.bat"
	File "data_common\assemble_disk_image.bat"
	
	WriteUninstaller "$INSTDIR\Uninstall.exe"
	
	${IfNot} ${RunningX64} 
		File "data\fsimageplugin.dll"
		File "data\urbackupserver.dll"
		File "data\httpserver.dll"
		File "data\fileservplugin.dll"
		File "data\cryptoplugin.dll"
		File "data_service\urbackup_srv.exe"
		File "data\urlplugin.dll"
		SetOutPath "$INSTDIR"
	${Else}
		File "data_x64\fsimageplugin.dll"
		File "data_x64\urbackupserver.dll"
		File "data_x64\httpserver.dll"
		File "data_x64\fileservplugin.dll"
		File "data_service_x64\urbackup_srv.exe"
		File "data_x64\cryptoplugin.dll"
		File "data_x64\urlplugin.dll"
		SetOutPath "$INSTDIR"
	${EndIf}
	SetOutPath "$INSTDIR\urbackup"
	File "data_common\urbackup\backup_server_init.sql"
	File "data_common\urbackup\status.htm"
	File "data_common\urbackup\dataplan_db.txt"
	SetOutPath "$INSTDIR\urbackup\www"
	File "data_common\urbackup\www\favicon.ico"
	File "data_common\urbackup\www\*.htm"
	SetOutPath "$INSTDIR\urbackup\www\swf"
	File "data_common\urbackup\www\swf\*"
	SetOutPath "$INSTDIR\urbackup\www\images"
	File "data_common\urbackup\www\images\*.png"
	File "data_common\urbackup\www\images\*.gif"
	Delete "$INSTDIR\urbackup\www\css\*.*"
	SetOutPath "$INSTDIR\urbackup\www\css"	
	File "data_common\urbackup\www\css\*.css"
	Delete "$INSTDIR\urbackup\www\js\*.*"
	SetOutPath "$INSTDIR\urbackup\www\js"
	File "data_common\urbackup\www\js\*.js"
	SetOutPath "$INSTDIR\urbackup\www\fonts"
	File "data_common\urbackup\www\fonts\*"
	
	CreateDirectory "$SMPROGRAMS\UrBackup Server"
	CreateShortCut "$SMPROGRAMS\UrBackup Server\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
	CreateShortCut "$SMPROGRAMS\UrBackup Server\UrBackup Server Interface.lnk" "http://localhost:55414" "" "$INSTDIR\urbackup\www\favico.ico" 0
	
	liteFirewallW::AddRule "$INSTDIR\urbackup_srv.exe" "UrBackup Windows Server"
	Pop $0
	
	!insertmacro SERVICE installed "UrBackupWinServer" ""
	Pop $0
	${If} $0 != "true"
		!insertmacro SERVICE create "UrBackupWinServer" 'path="$INSTDIR\urbackup_srv.exe";autostart=1;interact=0;display=UrBackup Windows Server;description=UrBackup Windows Server;'
	${EndIf}	
	!insertmacro SERVICE start "UrBackupWinServer" ""
	
	${If} ${RunningX64}
		!insertmacro EnableX64FSRedirection
		SetRegView 32
	${EndIf}
	
SectionEnd

Section "Uninstall"
	${If} ${RunningX64}
		!insertmacro DisableX64FSRedirection
		SetRegView 64
	${EndIf}
	
	System::Call 'Kernel32::SetEnvironmentVariable(t, t)i ("IMDISK_SILENT_SETUP", "1").r0'
	nsExec::ExecToLog '"$INSTDIR\imdisk\uninstall_imdisk.cmd"'
	
	!insertmacro SERVICE stop "UrBackupWinServer" ""
	!insertmacro SERVICE waitfor "UrBackupWinServer" "status=stopped"
	!insertmacro SERVICE delete "UrBackupWinServer" ""
	
	Sleep 500

	RMDir /r "$INSTDIR\*.*"
	RMDir "$INSTDIR"
	
	Delete "$SMPROGRAMS\UrBackup Server\*.*"
	RMDir  "$SMPROGRAMS\UrBackup Server"

	
	DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\UrBackupServer" 
	
	${If} ${RunningX64}
		!insertmacro EnableX64FSRedirection
		SetRegView 32
	${EndIf}
	
	DeleteRegKey /ifempty HKCU "Software\UrBackupServer"
	
SectionEnd

Function .onInstSuccess
	
FunctionEnd

Function un.onInit
	!insertmacro MUI_UNGETLANGUAGE
FunctionEnd
 
Function un.onUninstSuccess
  ;MessageBox MB_OK "UrBackup Server wurde erfolgreich deinstalliert."
FunctionEnd

Function myGuiInit
	;MessageBox MB_OK "blub"
	;${If} ${RunningX64}
	;	!insertmacro DisableX64FSRedirection
	;	SetRegView 64
	;${EndIf}
FunctionEnd

Function .onInit
	${If} ${RunningX64}
		strcpy $INSTDIR "$PROGRAMFILES64\UrBackupServer"
	${EndIf}
	!insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd


