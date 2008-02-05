## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libmousepoll_la_LDFLAGS = $(PFLAGS)
libmousepoll_la_LIBADD = @COMPIZ_LIBS@
libmousepoll_la_SOURCES = mousepoll.c

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)

module_LTLIBRARIES = libmousepoll.la
