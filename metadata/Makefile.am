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

xml_in_files = 	         \
	animation.xml.in      \
	colorfilter.xml.in    \
	expo.xml.in           \
	ezoom.xml.in          \
	focuspoll.xml.in      \
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
	titleinfo.xml.in      \
	vpswitch.xml.in       \
	winrules.xml.in       \
	workarounds.xml.in    \
	$(jpegxml)            \
	$(resizeinfoxml)      \
	$(scaleaddonxml)      \
	$(textxml)

xml_files    = $(xml_in_files:.xml.in=.xml)
xml_DATA     = $(xml_files)

@INTLTOOL_XML_RULE@

EXTRA_DIST =		\
	$(xml_in_files)

DISTCLEANFILES =	\
	$(xml_files)

