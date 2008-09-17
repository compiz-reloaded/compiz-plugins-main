## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

if JPEG_PLUGIN
libimgjpeg_la_LDFLAGS = $(PFLAGS)
libimgjpeg_la_LIBADD = @COMPIZ_LIBS@ -ljpeg
nodist_libimgjpeg_la_SOURCES = imgjpeg_options.c imgjpeg_options.h
dist_libimgjpeg_la_SOURCES = imgjpeg.c
endif

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)

if JPEG_PLUGIN
module_LTLIBRARIES = libimgjpeg.la
endif

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
