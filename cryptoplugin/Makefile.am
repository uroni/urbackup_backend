lib_LTLIBRARIES = libcryptoplugin.la
libcryptoplugin_la_SOURCES = dllmain.cpp AESDecryption.cpp CryptoFactory.cpp pluginmgr.cpp AESEncryption.cpp
noinst_HEADERS = AESEncryption.h AESDecryption.h IAESDecryption.h ICryptoFactory.h pluginmgr.h IAESEncryption.h CryptoFactory.h
AM_CXXFLAGS = -DLINUX
AM_LDFLAGS = -lcrypto++