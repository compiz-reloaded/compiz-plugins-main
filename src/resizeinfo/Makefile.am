## Process this file with automake to produce Makefile.in
PFLAGS=-module -avoid-version -no-undefined

if RESIZEINFO_PLUGIN
libresizeinfo_la_LDFLAGS = $(PFLAGS)
libresizeinfo_la_LIBADD = @COMPIZ_LIBS@ @PANGO_LIBS@
nodist_libresizeinfo_la_SOURCES = resizeinfo_options.c
			   resizeinfo_options.h \
dist_libresizeinfo_la_SOURCES = resizeinfo.c
endif

INCLUDES =                              \
	-I$(top_srcdir)/include         \
	@COMPIZ_CFLAGS@                  \
	@PANGO_CFLAGS@		\
	-DDATADIR='"$(compdatadir)"'        \
	-DLIBDIR='"$(libdir)"'              \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DIMAGEDIR='"$(imagedir)"'          

moduledir = $(plugindir)

if RESIZEINFO_PLUGIN
module_LTLIBRARIES = libresizeinfo.la
endif

CLEANFILES = *_options.c *_options.h

%_options.h: ../../metadata/%.xml
		$(BCOP_BIN) --header $@ $<

%_options.c: ../../metadata/%.xml %_options.h
		$(BCOP_BIN) --source $@ $<
