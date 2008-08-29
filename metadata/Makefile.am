xmldir       = $(metadatadir)

if JPEG_PLUGIN
jpegxml = imgjpeg.xml.in
endif

if RESIZEINFO_PLUGIN
resizeinfoxml = resizeinfo.xml.in
endif

if SCALEADDON_PLUGIN
scaleaddonxml = scaleaddon.xml.in
endif

if TEXT_PLUGIN
textxml = text.xml.in
endif

if WALL_PLUGIN
wallxml = wall.xml.in
endif

xml_in_files = 	         \
	animation.xml.in      \
	colorfilter.xml.in    \
	expo.xml.in           \
	ezoom.xml.in          \
	mag.xml.in            \
	mousepoll.xml.in      \
	neg.xml.in            \
	opacify.xml.in        \
	put.xml.in            \
	ring.xml.in           \
	session.xml.in        \
	shift.xml.in          \
	snap.xml.in           \
	staticswitcher.xml.in \
	thumbnail.xml.in      \
	vpswitch.xml.in       \
	winrules.xml.in       \
	workarounds.xml.in    \
	$(jpegxml)            \
	$(resizeinfoxml)      \
	$(scaleaddonxml)      \
	$(textxml)            \
	$(wallxml)

xml_files    = $(xml_in_files:.xml.in=.xml)
xml_DATA     = $(xml_files)

@INTLTOOL_XML_RULE@

if USE_SCHEMAS
schemadir   = $(GCONF_SCHEMA_FILE_DIR)
schema_files = $(patsubst %.xml.in,compiz-%.schemas,$(xml_in_files))
schema_DATA  = $(schema_files)

%.schemas: $(xml_files)
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

