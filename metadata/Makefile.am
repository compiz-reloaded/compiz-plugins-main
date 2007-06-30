xmldir       = $(metadatadir)
xml_in_files = 	         \
	animation.xml.in	 \
	expo.xml.in	 \
	imgjpeg.xml.in 	 \
	neg.xml.in          \
	opacify.xml.in      \
	put.xml.in 	 \
	resizeinfo.xml.in	 \
	ring.xml.in	 \
	snap.xml.in	 \
	text.xml.in	 \
	thumbnail.xml.in 	 \
	wall.xml.in	 \
	winrules.xml.in     \
	scaleaddon.xml.in   \
	vpswitch.xml.in


xml_files    = $(xml_in_files:.xml.in=.xml)
xml_DATA     = $(xml_files)

@INTLTOOL_XML_RULE@

if USE_SCHEMAS
schemadir   = $(GCONF_SCHEMA_FILE_DIR)
schema_files = $(patsubst %.xml.in,compiz-%.schemas,$(xml_in_files))
schema_DATA  = $(schema_files)

%.schemas: $(meta_DATA)
	xsltproc -o $@ $(xsltdir)/schemas.xslt $(subst compiz-,,$*).xml;

if GCONF_SCHEMAS_INSTALL
install-data-local:
	if test -z "$(DESTDIR)" ; then \
               GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(schema_DATA); \
       fi
endif
endif

EXTRA_DIST =		\
	$(xml_files)    \
	$(xml_in_files) \
	$(schema_files)

DISTCLEANFILES =	\
	$(xml_files)    \
	$(schema_files)

