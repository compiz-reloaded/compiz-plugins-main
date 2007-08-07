## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libvpswitch_la_LDFLAGS = $(PFLAGS)
libvpswitch_la_LIBADD = @COMPIZ_LIBS@
libvpswitch_la_SOURCES = vpswitch_options.c vpswitch_options.h vpswitch.c

INCLUDES =                              \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)


module_LTLIBRARIES = libvpswitch.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
