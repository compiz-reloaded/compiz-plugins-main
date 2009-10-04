## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libkdecompat_la_LDFLAGS = $(PFLAGS)
libkdecompat_la_LIBADD = @COMPIZ_LIBS@
nodist_libkdecompat_la_SOURCES = kdecompat_options.c kdecompat_options.h
dist_libkdecompat_la_SOURCES = kdecompat.c

BUILT_SOURCES = $(nodist_libkdecompat_la_SOURCES)

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'

moduledir = $(plugindir)

module_LTLIBRARIES = libkdecompat.la

CLEANFILES = *_options.c *_options.h

kdecompat_options.h: ../../metadata/kdecompat.xml.in
	$(BCOP_BIN) --header $@ $<

kdecompat_options.c: ../../metadata/kdecompat.xml.in kdecompat_options.h
	$(BCOP_BIN) --source $@ $<

