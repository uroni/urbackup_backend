lib_LTLIBRARIES = libpychart.la
libpychart_la_SOURCES = dllmain.cpp pluginmgr.cpp Pychart.cpp PychartFactory.cpp test.cpp
ACLOCAL_AMFLAGS=-I m4
AM_CXXFLAGS = -DLINUX $(BOOST_CPPFLAGS) -I$(PYTHON_INCLUDE_DIR)
AM_LDFLAGS = $(BOOST_LDFLAGS) $(BOOST_PYTHON_LDFLAGS) -lpython2.6 -lboost_python
install-data-local: $(srcdir)/pychart.py
	mkdir $(localstatedir)/pychart || true
	cp $(srcdir)/pychart.py $(localstatedir)/pychart/
	