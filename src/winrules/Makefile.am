## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libwinrules_la_LDFLAGS = $(PFLAGS)
libwinrules_la_LIBADD = @COMPIZ_LIBS@
libwinrules_la_SOURCES = winrules_options.c winrules.c

INCLUDES =                              \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(datadir)"'          \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)


module_LTLIBRARIES = libwinrules.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
