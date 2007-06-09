## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libthumbnail_la_LDFLAGS = $(PFLAGS)
libthumbnail_la_LIBADD = @COMPIZ_LIBS@
libthumbnail_la_SOURCES = thumbnail_options.c thumbnail_options.h thumbnail.c thumbnail_tex.h

INCLUDES =                              \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(datadir)"'          \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          \
	-I$(top_srcdir)/include

moduledir = $(plugindir)


module_LTLIBRARIES = libthumbnail.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
