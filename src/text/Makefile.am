## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

if TEXT_PLUGIN
libtext_la_LDFLAGS = $(PFLAGS)
libtext_la_LIBADD = @COMPIZ_LIBS@ @TEXT_LIBS@
libtext_la_SOURCES = text.c
endif

INCLUDES =                              \
	@COMPIZ_CFLAGS@                  \
	@TEXT_CFLAGS@			\
	-DDATADIR='"$(datadir)"'          \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)

if TEXT_PLUGIN
module_LTLIBRARIES = libtext.la
endif

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
