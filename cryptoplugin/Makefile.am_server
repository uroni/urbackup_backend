lib_LTLIBRARIES = liburbackupserver_cryptoplugin.la
liburbackupserver_cryptoplugin_la_SOURCES = dllmain.cpp AESDecryption.cpp CryptoFactory.cpp pluginmgr.cpp AESEncryption.cpp ZlibCompression.cpp ZlibDecompression.cpp
liburbackupserver_cryptoplugin_la_LIBADD = $(CRYPTOPP_LIBS)
noinst_HEADERS = AESEncryption.h AESDecryption.h IAESDecryption.h ICryptoFactory.h pluginmgr.h IAESEncryption.h CryptoFactory.h IZlibCompression.h IZlibDecompression.h ZlibCompression.h ZlibDecompression.h
AM_CXXFLAGS = -DLINUX $(CRYPTOPP_CPPFLAGS)
AM_LDFLAGS = $(CRYPTOPP_LDFLAGS)
ACLOCAL_AMFLAGS = -I m4