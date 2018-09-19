## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

if FOCUSPOLL_PLUGIN
libfocuspoll_la_LDFLAGS = $(PFLAGS)
libfocuspoll_la_LIBADD = @COMPIZ_LIBS@ @ATSPI2_LIBS@
libfocuspoll_la_SOURCES = focuspoll.cpp accessibilitywatcher.cpp focusinfo.cpp
endif

AM_CPPFLAGS =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	@ATSPI2_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-std=c++11

moduledir = $(plugindir)

if FOCUSPOLL_PLUGIN
module_LTLIBRARIES = libfocuspoll.la
endif
