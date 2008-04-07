project (compiz-fusion-plugins-main)

set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include (cmake/addons.cmake)
include (cmake/CompizFusion.cmake)

get_version ()

set (CF_INSTALL_TYPE package)

set (CF_MIN_COMPIZ_VERSION ${VERSION})
set (CF_MIN_BCOP_VERSION ${VERSION})

set (CF_PLUGIN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set (CF_PLUGIN_PKGCONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set (CF_PLUGIN_XML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/metadata")
set (CF_PLUGIN_I18N_DIR "${CMAKE_CURRENT_SOURCE_DIR}/po")

get_version ()
compiz_fusion_add_uninstall ()
cf_add_plugins (src)
cf_install_data_files (images)
cf_install_data_files (data)
cf_print_configure_results (src)