## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libvpswitch_la_LDFLAGS = $(PFLAGS)
libvpswitch_la_LIBADD = @COMPIZ_LIBS@
nodist_libvpswitch_la_SOURCES = vpswitch_options.c vpswitch_options.h
dist_libvpswitch_la_SOURCES = vpswitch.c

BUILT_SOURCES = $(nodist_libvpswitch_la_SOURCES)

AM_CPPFLAGS =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libvpswitch.la

CLEANFILES = *_options.c *_options.h

vpswitch_options.h: ../../metadata/vpswitch.xml.in
		$(BCOP_BIN) --header $@ $<

vpswitch_options.c: ../../metadata/vpswitch.xml.in vpswitch_options.h
		$(BCOP_BIN) --source $@ $<
