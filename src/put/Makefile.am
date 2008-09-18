## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libput_la_LDFLAGS = $(PFLAGS)
libput_la_LIBADD = @COMPIZ_LIBS@
nodist_libput_la_SOURCES = put_options.c put_options.h
dist_libput_la_SOURCES = put.c

BUILT_SOURCES = $(nodist_libput_la_SOURCES)

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libput.la

CLEANFILES = *_options.c *_options.h

put_options.h: ../../metadata/put.xml.in
		$(BCOP_BIN) --header $@ $<

put_options.c: ../../metadata/put.xml.in put_options.h
		$(BCOP_BIN) --source $@ $<
