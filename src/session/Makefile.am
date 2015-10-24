## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libsession_la_LDFLAGS = $(PFLAGS)
libsession_la_LIBADD = @COMPIZ_LIBS@
libsession_la_SOURCES = session.c

AM_CPPFLAGS =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libsession.la
