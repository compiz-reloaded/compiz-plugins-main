## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libbs_la_LDFLAGS = $(PFLAGS)
libbs_la_LIBADD = @COMPIZ_LIBS@
libbs_la_SOURCES = bs.c

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libbs.la

