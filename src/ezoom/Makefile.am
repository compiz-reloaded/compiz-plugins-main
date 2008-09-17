## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libezoom_la_LDFLAGS = $(PFLAGS)
libezoom_la_LIBADD = @COMPIZ_LIBS@ -lGLU
nodist_libezoom_la_SOURCES = ezoom_options.c ezoom_options.h
dist_libezoom_la_SOURCES = ezoom.c

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

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
