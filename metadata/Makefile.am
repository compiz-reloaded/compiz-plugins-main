metadir = $(metadatadir)
meta_DATA =	         \
	animation.xml	 \
	expo.xml	 \
	imgjpeg.xml 	 \
	neg.xml          \
	opacify.xml      \
	put.xml 	 \
	resizeinfo.xml	 \
	ring.xml	 \
	snap.xml	 \
	text.xml	 \
	thumbnail.xml 	 \
	wall.xml	 \
	winrules.xml     \
	scaleaddon.xml


EXTRA_DIST = $(meta_DATA)

if USE_SCHEMAS
schemadir   = $(GCONF_SCHEMA_FILE_DIR)
schema_DATA = $(patsubst %.xml,compiz-%.schemas,$(meta_DATA))

%.schemas: $(meta_DATA)
	xsltproc -o $@ $(xsltdir)/schemas.xslt $(subst compiz-,,$*).xml;

if GCONF_SCHEMAS_INSTALL
install-data-local:
	if test -z "$(DESTDIR)" ; then \
               GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(schema_DATA); \
       fi
endif
endif
