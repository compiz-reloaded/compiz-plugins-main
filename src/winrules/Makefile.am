## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libwinrules_la_LDFLAGS = $(PFLAGS)
libwinrules_la_LIBADD = @COMPIZ_LIBS@
libwinrules_la_SOURCES = winrules.c

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libwinrules.la

CLEANFILES = *_options.c *_options.h

winrules_options.h: ../../metadata/winrules.xml.in
		$(BCOP_BIN) --header $@ $<

winrules_options.c: ../../metadata/winrules.xml.in winrules_options.h
		$(BCOP_BIN) --source $@ $<

winrules.c: winrules_options.c winrules_options.h
