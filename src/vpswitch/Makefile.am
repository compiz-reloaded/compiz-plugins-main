## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libvpswitch_la_LDFLAGS = $(PFLAGS)
libvpswitch_la_LIBADD = @COMPIZ_LIBS@
nodist_libvpswitch_la_SOURCES = vpswitch_options.c vpswitch_options.h
dist_libvpswitch_la_SOURCES = vpswitch.c

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libvpswitch.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
