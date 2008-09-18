## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libworkarounds_la_LDFLAGS = $(PFLAGS)
libworkarounds_la_LIBADD = @COMPIZ_LIBS@
nodist_libworkarounds_la_SOURCES = workarounds_options.c \
			    workarounds_options.h
dist_libworkarounds_la_SOURCES = workarounds.c

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

workarounds_options.h: ../../metadata/workarounds.xml
		$(BCOP_BIN) --header $@ $<

workarounds_options.c: ../../metadata/workarounds.xml workarounds_options.h
		$(BCOP_BIN) --source $@ $<

workarounds.c: workarounds_options.c workarounds_options.h
