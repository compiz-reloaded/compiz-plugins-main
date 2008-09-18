## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libezoom_la_LDFLAGS = $(PFLAGS)
libezoom_la_LIBADD = @COMPIZ_LIBS@ -lGLU
nodist_libezoom_la_SOURCES = ezoom_options.c ezoom_options.h
dist_libezoom_la_SOURCES = ezoom.c

BUILT_SOURCES = $(nodist_libezoom_la_SOURCES)

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(datadir)"'          \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libezoom.la

CLEANFILES = *_options.c *_options.h

ezoom_options.h: ../../metadata/ezoom.xml.in
		$(BCOP_BIN) --header $@ $<

ezoom_options.c: ../../metadata/ezoom.xml.in ezoom_options.h
		$(BCOP_BIN) --source $@ $<
