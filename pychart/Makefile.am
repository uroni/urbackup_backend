lib_LTLIBRARIES = liburbackupserver_pychart.la
liburbackupserver_pychart_la_SOURCES = dllmain.cpp pluginmgr.cpp Pychart.cpp PychartFactory.cpp test.cpp ../stringtools.cpp
liburbackupserver_pychart_la_LIBADD = $(BOOST_LDFLAGS) $(BOOST_PYTHON_LDFLAGS) -l$(PYTHON_LIB) -lboost_python
noinst_HEADERS = IPychartFactory.h IPychart.h pluginmgr.h PychartFactory.h Pychart.h
ACLOCAL_AMFLAGS=-I m4
AM_CXXFLAGS = -DLINUX $(BOOST_CPPFLAGS) -I$(PYTHON_INCLUDE_DIR)
AM_LDFLAGS = 
install-data-local: $(srcdir)/pychart.py
	$(INSTALL_DATA) -D $(srcdir)/pychart.py $(DESTDIR)$(localstatedir)/pychart/pychart.py
EXTRA_DIST=pychart.py	