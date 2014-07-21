lib_LTLIBRARIES = liburbackupclient_fileservplugin.la
liburbackupclient_fileservplugin_la_SOURCES = dllmain.cpp ../stringtools.cpp bufmgr.cpp CampusThread.cpp CClientThread.cpp CriticalSection.cpp CTCPFileServ.cpp CUDPThread.cpp ../common/data.cpp FileServ.cpp FileServFactory.cpp log.cpp main.cpp map_buffer.cpp pluginmgr.cpp ../urbackupcommon/fileclient/tcpstack.cpp ChunkSendThread.cpp ../common/adler32.cpp ../md5.cpp
noinst_HEADERS = bufmgr.h CUDPThread.h FileServFactory.h IFileServ.h packet_ids.h socket_header.h CampusThread.h CriticalSection.h ../common/data.h FileServ.h log.h pluginmgr.h ../urbackupcommon/fileclient/tcpstack.h ../common/adler32.h CClientThread.h CTCPFileServ.h IFileServFactory.h map_buffer.h settings.h types.h chunk_settings.h ChunkSendThread.h ../md5.h
if WITH_FORTIFY
AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
AM_CXXFLAGS = -DLINUX