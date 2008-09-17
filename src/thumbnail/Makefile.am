## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

libthumbnail_la_LDFLAGS = $(PFLAGS)
libthumbnail_la_LIBADD = @COMPIZ_LIBS@
nodist_libthumbnail_la_SOURCES = thumbnail_options.c \
			  thumbnail_options.h
dist_libthumbnail_la_SOURCES = thumbnail.c \
			  thumbnail_tex.h

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)


module_LTLIBRARIES = libthumbnail.la

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
