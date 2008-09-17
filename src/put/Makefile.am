## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libput_la_LDFLAGS = $(PFLAGS)
libput_la_LIBADD = @COMPIZ_LIBS@
nodist_libput_la_SOURCES = put_options.c put_options.h
dist_libput_la_SOURCES = put.c

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

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
