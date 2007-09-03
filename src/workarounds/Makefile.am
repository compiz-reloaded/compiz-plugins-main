## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libworkarounds_la_LDFLAGS = $(PFLAGS)
libworkarounds_la_LIBADD = @COMPIZ_LIBS@
libworkarounds_la_SOURCES = workarounds_options.c \
			    workarounds_options.h \
			    workarounds.c

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)

module_LTLIBRARIES = libworkarounds.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
