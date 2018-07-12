iconsdir = $(pluginiconsdir)/scalable/apps
icons_DATA =				\
	plugin-animation.svg		\
	plugin-colorfilter.svg		\
	plugin-expo.svg			\
	plugin-ezoom.svg		\
	plugin-imgjpeg.svg		\
	plugin-mag.svg			\
	plugin-mousepoll.svg		\
	plugin-neg.svg			\
	plugin-opacify.svg		\
	plugin-put.svg			\
	plugin-resizeinfo.svg		\
	plugin-ring.svg			\
	plugin-scaleaddon.svg		\
	plugin-session.svg		\
	plugin-shift.svg		\
	plugin-snap.svg			\
	plugin-staticswitcher.svg	\
	plugin-text.svg			\
	plugin-thumbnail.svg		\
	plugin-titleinfo.svg		\
	plugin-vpswitch.svg		\
	plugin-winrules.svg		\
	plugin-workarounds.svg

install-data-hook:
	if test x$(DESTDIR) = x; then \
		$(UPDATE_ICON_CACHE) -f -t $(pluginiconsdir); \
	fi
