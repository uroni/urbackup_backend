# escape=`

FROM microsoft/windowsservercore:ltsc2016

CMD [ "cmd.exe" ]

ADD https://download.microsoft.com/download/6/A/A/6AA4EDFF-645B-48C5-81CC-ED5963AEAD48/vc_redist.x64.exe /vc_redist.x64.exe
ADD https://s3.amazonaws.com/elasticurl-pipeline/cd-builds/elasticurl/v0.2.15/win64/elasticurl.exe /bin/elasticurl.exe

# Add C:\bin (Elasticurl) to PATH
# RUN setx /M PATH "%PATH%;C:\bin" && set "PATH=%PATH%;c:\bin"

# Install vcredist and delete installer
RUN start /wait C:\vc_redist.x64.exe /quiet /norestart && del /q c:\vc_redist.x64.exe && `
    # Install chocolatey
    "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -InputFormat None -ExecutionPolicy Bypass -Command "iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))" && SET "PATH=%PATH%;%ALLUSERSPROFILE%\chocolatey\bin" && `
    choco install -y git 7zip && `
    # install cmake
    choco install cmake --installargs 'ADD_CMAKE_TO_PATH=""System""' -y && `
    # install python
    choco install -y python3 --install-arguments "/InstallDir:C:\\python3 /PrependPath=1" && `
    # update path and test
    refreshenv && `
    git --version && `
    cmake --version && `
    python --version

# Install Visual C++ Build Tools, as per: https://chocolatey.org/packages/visualcpp-build-tools
RUN choco install visualcpp-build-tools --version 14.0.25420.1 -y && `	
    # Add msbuild to PATH	
    setx /M PATH "%PATH%;C:\Program Files (x86)\MSBuild\14.0\bin" && `
    refreshenv && `
    # Test msbuild can be accessed without path	
    msbuild -version
