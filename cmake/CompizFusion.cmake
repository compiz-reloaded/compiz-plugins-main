#######################################################################
#
# Generic Compiz Fusion plugin cmake module
#
# Copyright : (C) 2008 by Dennis Kasprzyk
# E-mail    : onestone@opencompositing.org
#
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
#######################################################################
#
# This module provides the following macro:
#
# compiz_fusion_plugin (<plugin name>
#                       [PKGDEPS dep1 dep2 ...]
#                       [PLUGINDEPS plugin1 plugin2 ...]
#                       [LDFLAGSADD flag1 flag2 ...]
#                       [CFLAGSADD flag1 flag2 ...]
#                       [LIBRARIES lib1 lib2 ...]
#                       [LIBDIRS dir1 dir2 ...]
#                       [INCDIRS dir1 dir2 ...])
#
# PKGDEPS    = pkgconfig dependencies
# PLUGINDEPS = compiz plugin dependencies
# LDFLAGSADD = flags added to the link command
# CFLAGSADD  = flags added to the compile command
# LIBRARIES  = libraries added to link command
# LIBDIRS    = additional link directories
# INCDIRS    = additional include directories
#
# The following variables will be used by this macro:
#
# BUILD_GLOBAL=true Environment variable to install a plugin
#                   into the compiz directories
#
# CF_INSTALL_TYPE = (package | compiz | local (default))
#     package = Install into ${MAKE_INSTALL_PREFIX}
#     compiz  = Install into compiz prefix (=BUILD_GLOBAL=true)
#     local   = Install into home directory
#
# CF_MIN_COMPIZ_VERSION   = Minimal compiz version required for build
# CF_MIN_BCOP_VERSION     = Minimal bcop version required for build
#
# CF_PLUGIN_I18N_DIR      = Translation file directory
# CF_PLUGIN_INCLUDE_DIR   = Path to plugin header files
# CF_PLUGIN_PKGCONFIG_DIR = Path to plugin *.pc.in files
# CF_PLUGIN_XML_DIR       = Path to plugin *.xml[.in] files
#
# CF_DISABLE_SCHEMAS_INSTALL  = Disables gconf schema installation with gconftool
# CF_INSTALL_GCONF_SCHEMA_DIR = Installation path of the gconf schema file
#
# VERSION = package version that is added to a plugin pkg-version file
#
#######################################################################

if (CMAKE_MAJOR_VERSION GREATER 2 OR CMAKE_MAJOR_VERSION EQUAL 2 AND CMAKE_MINOR_VERSION GREATER 5)
cmake_policy (VERSION 2.4)
cmake_policy(SET CMP0000 OLD)  
cmake_policy(SET CMP0005 OLD)
endif (CMAKE_MAJOR_VERSION GREATER 2 OR CMAKE_MAJOR_VERSION EQUAL 2 AND CMAKE_MINOR_VERSION GREATER 5)

# add install prefix to pkgconfig search path
if ("" STREQUAL "$ENV{PKG_CONFIG_PATH}")
    set (ENV{PKG_CONFIG_PATH} "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig:${CMAKE_INSTALL_PREFIX}/share/pkgconfig")
else ("" STREQUAL "$ENV{PKG_CONFIG_PATH}")
    set (ENV{PKG_CONFIG_PATH}
         "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig:${CMAKE_INSTALL_PREFIX}/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif ("" STREQUAL "$ENV{PKG_CONFIG_PATH}")

include (FindPkgConfig)

# unsets the given variable
macro (_cf_unset var)
    set (${var} "" CACHE INTERNAL "")
endmacro (_cf_unset)

# sets the given variable
macro (_cf_set var value)
    set (${var} ${value} CACHE INTERNAL "")
endmacro (_cf_set)

# determinate installation directories
macro (_prepare_directories)
    pkg_check_modules (COMPIZ compiz)
    if ("${CF_INSTALL_TYPE}" STREQUAL "package")
	set (PLUGIN_BUILDTYPE global)
	set (PLUGIN_PREFIX    ${CMAKE_INSTALL_PREFIX})
	set (PLUGIN_LIBDIR    ${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}/compiz)
	set (PLUGIN_INCDIR    ${CMAKE_INSTALL_PREFIX}/include/compiz)
	set (PLUGIN_PKGDIR    ${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX}/pkgconfig)
	set (PLUGIN_XMLDIR    ${CMAKE_INSTALL_PREFIX}/share/compiz)
	set (PLUGIN_IMAGEDIR  ${CMAKE_INSTALL_PREFIX}/share/compiz)
	set (PLUGIN_DATADIR   ${CMAKE_INSTALL_PREFIX}/share/compiz)
	if (NOT DEFINED CF_INSTALL_GCONF_SCHEMA_DIR)
            set (PLUGIN_SCHEMADIR "${CMAKE_INSTALL_PREFIX}/share/gconf/schemas")
        else (NOT DEFINED CF_INSTALL_GCONF_SCHEMA_DIR)
	    set (PLUGIN_SCHEMADIR "${CF_INSTALL_GCONF_SCHEMA_DIR}")
	endif (NOT DEFINED CF_INSTALL_GCONF_SCHEMA_DIR)

    elseif ("${CF_INSTALL_TYPE}" STREQUAL "compiz" OR
	    "$ENV{BUILD_GLOBAL}" STREQUAL "true")
	set (PLUGIN_BUILDTYPE global)
	set (PLUGIN_PREFIX    ${COMPIZ_PREFIX})
	set (PLUGIN_LIBDIR    ${COMPIZ_LIBDIR}/compiz)
	set (PLUGIN_INCDIR    ${COMPIZ_INCLUDEDIR})
	set (PLUGIN_PKGDIR    ${COMPIZ_LIBDIR}/pkgconfig)
	set (PLUGIN_XMLDIR    ${COMPIZ_PREFIX}/share/compiz)
	set (PLUGIN_IMAGEDIR  ${COMPIZ_PREFIX}/share/compiz)
	set (PLUGIN_DATADIR   ${COMPIZ_PREFIX}/share/compiz)
	if (NOT DEFINED CF_INSTALL_GCONF_SCHEMA_DIR)
            set (PLUGIN_SCHEMADIR "${COMPIZ_PREFIX}/share/gconf/schemas")
        else (NOT DEFINED CF_INSTALL_GCONF_SCHEMA_DIR)
	    set (PLUGIN_SCHEMADIR "${CF_INSTALL_GCONF_SCHEMA_DIR}")
	endif (NOT DEFINED CF_INSTALL_GCONF_SCHEMA_DIR)
	
	if (NOT "${CMAKE_BUILD_TYPE}")
	    set (CMAKE_BUILD_TYPE debug)
	endif (NOT "${CMAKE_BUILD_TYPE}")	
    else ("${CF_INSTALL_TYPE}" STREQUAL "compiz" OR
	  "$ENV{BUILD_GLOBAL}" STREQUAL "true")
	set (PLUGIN_BUILDTYPE local)
	set (PLUGIN_PREFIX    $ENV{HOME}/.compiz)
	set (PLUGIN_LIBDIR    $ENV{HOME}/.compiz/plugins)
	set (PLUGIN_XMLDIR    $ENV{HOME}/.compiz/metadata)
	set (PLUGIN_IMAGEDIR  $ENV{HOME}/.compiz/images)
	set (PLUGIN_DATADIR   $ENV{HOME}/.compiz/data)

	if (NOT DEFINED CF_INSTALL_GCONF_SCHEMA_DIR)
            set (PLUGIN_SCHEMADIR "$ENV{HOME}/.gconf/schemas")
        else (NOT DEFINED CF_INSTALL_GCONF_SCHEMA_DIR)
	    set (PLUGIN_SCHEMADIR "${CF_INSTALL_GCONF_SCHEMA_DIR}")
	endif (NOT DEFINED CF_INSTALL_GCONF_SCHEMA_DIR)
	
	if (NOT "${CMAKE_BUILD_TYPE}")
	    set (CMAKE_BUILD_TYPE debug)
	endif (NOT "${CMAKE_BUILD_TYPE}")
    endif ("${CF_INSTALL_TYPE}" STREQUAL "package")
endmacro (_prepare_directories)

# parse plugin macro parameter
macro (_get_plugin_parameters _prefix)
    set (_current_var _foo)
    set (_supported_var PKGDEPS PLUGINDEPS LDFLAGSADD CFLAGSADD LIBRARIES LIBDIRS INCDIRS)
    foreach (_val ${_supported_var})
	set (${_prefix}_${_val})
    endforeach (_val)
    foreach (_val ${ARGN})
	set (_found FALSE)
	foreach (_find ${_supported_var})
	    if ("${_find}" STREQUAL "${_val}")
		set (_found TRUE)
	    endif ("${_find}" STREQUAL "${_val}")
	endforeach (_find)
	
	if (_found)
	    set (_current_var ${_prefix}_${_val})
	else (_found)
	    list (APPEND ${_current_var} ${_val})
	endif (_found)
    endforeach (_val)
endmacro (_get_plugin_parameters)

# check pkgconfig dependencies
macro (_check_plugin_pkg_deps _prefix)
    set (${_prefix}_HAS_PKG_DEPS TRUE)
    foreach (_val ${ARGN})
	string (TOUPPER ${_val} _name)
	pkg_check_modules (_${_name} ${_val})

	if (_${_name}_FOUND)
	    list (APPEND ${_prefix}_LIBDIRS "${_${_name}_LIBRARY_DIRS}")
	    list (APPEND ${_prefix}_LIBRARIES "${_${_name}_LIBRARIES}")
	    list (APPEND ${_prefix}_INCDIRS "${_${_name}_INCLUDE_DIRS}")
	else (_${_name}_FOUND)
	    set (${_prefix}_HAS_PKG_DEPS FALSE)
	    _cf_set (CF_${_prefix}_MISSING_DEPS "${CF_${_prefix}_MISSING_DEPS} ${_val}")
	    set(__pkg_config_checked__${_name} 0 CACHE INTERNAL "" FORCE)
	endif (_${_name}_FOUND)
    endforeach (_val)
endmacro (_check_plugin_pkg_deps)

# check plugin dependencies
macro (_check_plugin_plugin_deps _prefix)
    set (${_prefix}_HAS_PLUGIN_DEPS TRUE)
    foreach (_val ${ARGN})
	string (TOUPPER ${_val} _name)
	
	find_file (
	    _plugin_dep_${_val}
	    compiz-${_val}.h
	    PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CF_PLUGIN_INCLUDE_DIR}
		  ${CMAKE_CURRENT_SOURCE_DIR}/../${_val}
	    NO_DEFAULT_PATH
	)
	
	if (_plugin_dep_${_val})
	    file (RELATIVE_PATH _relative ${CMAKE_CURRENT_SOURCE_DIR} ${_plugin_dep_${_val}})
	    get_filename_component (_plugin_inc_dir ${_relative} PATH)
	    list (APPEND ${_prefix}_INCDIRS ${_plugin_inc_dir})
	else (_plugin_dep_${_val})
	    # fallback to pkgconfig
	    pkg_check_modules (_${_name} compiz-${_val})
	    if (_${_name}_FOUND)
		list (APPEND ${_prefix}_INCDIRS "${_${_name}_INCLUDE_DIRS}")
	    else (_${_name}_FOUND)
		set (${_prefix}_HAS_PLUGIN_DEPS FALSE)
		_cf_set (CF_${_prefix}_MISSING_DEPS "${CF_${_prefix}_MISSING_DEPS} compiz-${_val}")
		set(__pkg_config_checked__${_name} 0 CACHE INTERNAL "" FORCE)
	    endif (_${_name}_FOUND)
	endif (_plugin_dep_${_val})

    endforeach (_val)
endmacro (_check_plugin_plugin_deps)

# does the plugin require bcop
macro (_is_bcop_plugin _plugin _return _file)
    file (READ ${_file} _xml_content)
    if ("${_xml_content}" MATCHES "useBcop=\"true\"")
	set (${_return} TRUE)
    else ("${_xml_content}" MATCHES "useBcop=\"true\"")
	set (${_return} FALSE)
    endif ("${_xml_content}" MATCHES "useBcop=\"true\"")

endmacro (_is_bcop_plugin)

# prepare bcop build
macro (_init_bcop _plugin _file)
    exec_program (${PKG_CONFIG_EXECUTABLE} 
		  ARGS "--variable=bin bcop" 
		  OUTPUT_VARIABLE BCOP_EXECUTABLE)
    add_custom_command (
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}_options.h
        COMMAND ${BCOP_EXECUTABLE}
                    --header ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}_options.h
                    ${_file}
        DEPENDS ${_file}
    )
    add_custom_command (
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}_options.c
        COMMAND ${BCOP_EXECUTABLE}
                    --source ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}_options.c
                    ${_file}
                    ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}_options.h
        DEPENDS ${_file}
                ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}_options.h
    )
    set (BCOP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}_options.h;${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}_options.c")
endmacro (_init_bcop)

# translate metadata file
macro (_init_translation _plugin _file)
    find_program (_INTLTOOL_MERGE_EXECUTABLE intltool-merge)

    if (_INTLTOOL_MERGE_EXECUTABLE 
	AND CF_PLUGIN_I18N_DIR 
	AND EXISTS ${CF_PLUGIN_I18N_DIR})
	add_custom_command (
	    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}.xml
	    COMMAND ${_INTLTOOL_MERGE_EXECUTABLE} -x -u -c
		    ${CF_PLUGIN_I18N_DIR}/.intltool-merge-cache
		    ${CF_PLUGIN_I18N_DIR}
		    ${_file}
		    ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}.xml
	    DEPENDS ${_file}
	)
    else (_INTLTOOL_MERGE_EXECUTABLE 
	  AND CF_PLUGIN_I18N_DIR 
	  AND EXISTS ${CF_PLUGIN_I18N_DIR})
    	add_custom_command (
	    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}.xml
	    COMMAND cat ${_file} | 
		    sed -e 's;<_;<;g' -e 's;</_;</;g' > 
		    ${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}.xml
	    DEPENDS ${_file}
	)
    endif (_INTLTOOL_MERGE_EXECUTABLE 
	   AND CF_PLUGIN_I18N_DIR 
	   AND EXISTS ${CF_PLUGIN_I18N_DIR})

    set (TRANSLATION_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/${_plugin}.xml")
endmacro (_init_translation)

# generate gconf schema
macro (_init_gconf_schema _plugin _xml)
    if (DEFINED CF_MIN_COMPIZ_VERSION)
	pkg_check_modules (_COMPIZ_GCONF compiz-gconf>=${CF_MIN_COMPIZ_VERSION})
    else (DEFINED CF_MIN_COMPIZ_VERSION)
	pkg_check_modules (_COMPIZ_GCONF compiz-gconf)
    endif (DEFINED CF_MIN_COMPIZ_VERSION)

    find_program (_XSLTPROC_EXECUTABLE xsltproc)

    if (_COMPIZ_GCONF_FOUND AND _XSLTPROC_EXECUTABLE)
	exec_program (${PKG_CONFIG_EXECUTABLE}
		      ARGS "--variable=xsltdir compiz-gconf" 
		      OUTPUT_VARIABLE _SCHEMA_XSLT_DIR)
	add_custom_command (
	    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/compiz-${_plugin}.schema
	    COMMAND ${_XSLTPROC_EXECUTABLE}
	            ${_SCHEMA_XSLT_DIR}/schemas.xslt
	            ${_xml} >
	            ${CMAKE_CURRENT_BINARY_DIR}/generated/compiz-${_plugin}.schema
	    DEPENDS ${_xml}
	)

	find_program (_GCONFTOOL_EXECUTABLE gconftool-2)
	if (_GCONFTOOL_EXECUTABLE AND NOT DEFINED CF_DISABLE_SCHEMAS_INSTALL)
	    install (CODE "
		    if (\"\$ENV{USER}\" STREQUAL \"root\")
			exec_program (${_GCONFTOOL_EXECUTABLE}
			    ARGS \"--get-default-source\"
			    OUTPUT_VARIABLE ENV{GCONF_CONFIG_SOURCE})
			exec_program (${_GCONFTOOL_EXECUTABLE}
			    ARGS \"--makefile-install-rule ${CMAKE_CURRENT_BINARY_DIR}/generated/compiz-${_plugin}.schema > /dev/null\")
		    else (\"\$ENV{USER}\" STREQUAL \"root\")
			exec_program (${_GCONFTOOL_EXECUTABLE}
			    ARGS \"--install-schema-file=${CMAKE_CURRENT_BINARY_DIR}/generated/compiz-${_plugin}.schema > /dev/null\")
		    endif (\"\$ENV{USER}\" STREQUAL \"root\")
		    ")
	endif (_GCONFTOOL_EXECUTABLE AND NOT DEFINED CF_DISABLE_SCHEMAS_INSTALL)
	install (
	    FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/compiz-${_plugin}.schema"
	    DESTINATION "${PLUGIN_SCHEMADIR}"
	)

	set (SCHEMA_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/generated/compiz-${_plugin}.schema")
    endif (_COMPIZ_GCONF_FOUND AND _XSLTPROC_EXECUTABLE)
endmacro (_init_gconf_schema)

macro (compiz_fusion_add_uninstall)
   if (NOT _cf_uninstall_rule_created)
	_cf_set(_cf_uninstall_rule_created TRUE)

	set (_file "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake")

	file (WRITE  ${_file} "if (NOT EXISTS \"${CMAKE_BINARY_DIR}/install_manifest.txt\")\n")
	file (APPEND ${_file} "  message (FATAL_ERROR \"Cannot find install manifest: \\\"${CMAKE_BINARY_DIR}/install_manifest.txt\\\"\")\n")
	file (APPEND ${_file} "endif (NOT EXISTS \"${CMAKE_BINARY_DIR}/install_manifest.txt\")\n\n")
	file (APPEND ${_file} "file (READ \"${CMAKE_BINARY_DIR}/install_manifest.txt\" files)\n")
	file (APPEND ${_file} "string (REGEX REPLACE \"\\n\" \";\" files \"\${files}\")\n")
	file (APPEND ${_file} "foreach (file \${files})\n")
	file (APPEND ${_file} "  message (STATUS \"Uninstalling \\\"\${file}\\\"\")\n")
	file (APPEND ${_file} "  if (EXISTS \"\${file}\")\n")
	file (APPEND ${_file} "    exec_program(\n")
	file (APPEND ${_file} "      \"${CMAKE_COMMAND}\" ARGS \"-E remove \\\"\${file}\\\"\"\n")
	file (APPEND ${_file} "      OUTPUT_VARIABLE rm_out\n")
	file (APPEND ${_file} "      RETURN_VALUE rm_retval\n")
	file (APPEND ${_file} "      )\n")
	file (APPEND ${_file} "    if (\"\${rm_retval}\" STREQUAL 0)\n")
	file (APPEND ${_file} "    else (\"\${rm_retval}\" STREQUAL 0)\n")
	file (APPEND ${_file} "      message (FATAL_ERROR \"Problem when removing \\\"\${file}\\\"\")\n")
	file (APPEND ${_file} "    endif (\"\${rm_retval}\" STREQUAL 0)\n")
	file (APPEND ${_file} "  else (EXISTS \"\${file}\")\n")
	file (APPEND ${_file} "    message (STATUS \"File \\\"\${file}\\\" does not exist.\")\n")
	file (APPEND ${_file} "  endif (EXISTS \"\${file}\")\n")
	file (APPEND ${_file} "endforeach (file)\n")

	add_custom_target(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake")

    endif (NOT _cf_uninstall_rule_created)
endmacro (compiz_fusion_add_uninstall)

# main macro
macro (compiz_fusion_plugin plugin)
    string (TOUPPER ${plugin} _PLUGIN)

    # check for compiz
    if (DEFINED CF_MIN_COMPIZ_VERSION)
	pkg_check_modules (COMPIZ REQUIRED compiz>=${CF_MIN_COMPIZ_VERSION})
    else (DEFINED CF_MIN_COMPIZ_VERSION)
	pkg_check_modules (COMPIZ REQUIRED compiz)
    endif (DEFINED CF_MIN_COMPIZ_VERSION)

    _get_plugin_parameters (${_PLUGIN} ${ARGN})
    _prepare_directories ()

    find_file (
	_${plugin}_xml_in ${plugin}.xml.in
	PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CF_PLUGIN_XML_DIR}
	NO_DEFAULT_PATH
    )
    if (_${plugin}_xml_in)
	set (_${plugin}_xml ${_${plugin}_xml_in})
    else (_${plugin}_xml_in)
	find_file (
	    _${plugin}_xml ${plugin}.xml
	    PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CF_PLUGIN_XML_DIR}
	    NO_DEFAULT_PATH
        )
    endif (_${plugin}_xml_in)
	
    if (_${plugin}_xml)
	# do we need bcop for our plugin
	_is_bcop_plugin (${plugin} _needs_bcop ${_${plugin}_xml})
    endif (_${plugin}_xml)

    if (_needs_bcop)
	if (DEFINED CF_MIN_BCOP_VERSION)
	    list (APPEND ${_PLUGIN}_PKGDEPS bcop>=${CF_MIN_BCOP_VERSION})
	else (DEFINED CF_MIN_BCOP_VERSION)
	    list (APPEND ${_PLUGIN}_PKGDEPS bcop)
	endif (DEFINED CF_MIN_BCOP_VERSION)
    endif (_needs_bcop)


    set (${_PLUGIN}_HAS_PKG_DEPS)
    set (${_PLUGIN}_HAS_PLUGIN_DEPS)

    # check dependencies
    _cf_unset (CF_${_PLUGIN}_MISSING_DEPS)
    _check_plugin_pkg_deps (${_PLUGIN} ${${_PLUGIN}_PKGDEPS})
    _check_plugin_plugin_deps (${_PLUGIN} ${${_PLUGIN}_PLUGINDEPS})

    if (COMPIZ_FOUND AND ${_PLUGIN}_HAS_PKG_DEPS AND ${_PLUGIN}_HAS_PLUGIN_DEPS)

	_cf_set (CF_${_PLUGIN}_BUILD TRUE)

	if (NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/generated)
	    file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
	endif (NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/generated)
	
	if (_${plugin}_xml_in)
	    # translate xml
	    _init_translation (${plugin} ${_${plugin}_xml_in})
	    set (_translated_xml ${CMAKE_CURRENT_BINARY_DIR}/generated/${plugin}.xml)
	else (_${plugin}_xml_in)
	    if (_${plugin}_xml)
		set (_translated_xml ${_${plugin}_xml})
	    endif (_${plugin}_xml)
	endif (_${plugin}_xml_in)
	
	if (_${plugin}_xml)
	    # do we need bcop for our plugin
	    _is_bcop_plugin (${plugin} _needs_bcop ${_${plugin}_xml})
	    if (_needs_bcop)
		# initialize everything we need for bcop
		_init_bcop (${plugin} ${_${plugin}_xml})
	    endif (_needs_bcop)
	endif (_${plugin}_xml)
	
	if (_needs_bcop)
	    # initialize everything we need for bcop
	    _init_bcop (${plugin} ${_${plugin}_xml})
	endif (_needs_bcop)
	
	if (_translated_xml)
	    # generate gconf schema
	    _init_gconf_schema (${plugin} ${_translated_xml})

	    # install xml
	    install (
		FILES ${_translated_xml}
		DESTINATION ${PLUGIN_XMLDIR}
	    )
	endif (_translated_xml)
	
	find_file (
	    _${plugin}_hdr compiz-${plugin}.h
	    PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CF_PLUGIN_INCLUDE_DIR}
	    NO_DEFAULT_PATH
	)
	
	find_file (
	    _${plugin}_pkg compiz-${plugin}.pc.in
	    PATHS ${CMAKE_CURRENT_SOURCE_DIR} ${CF_PLUGIN_PKGCONFIG_DIR}
	    NO_DEFAULT_PATH
	)

	# generate pkgconfig file and install it and the plugin header file
	if (_${plugin}_hdr AND _${plugin}_pkg)
	    if ("${PLUGIN_BUILDTYPE}" STREQUAL "local")
		message (STATUS "[WARNING] The plugin ${plugin} might be needed by other plugins. Install it systemwide.")
	    else ("${PLUGIN_BUILDTYPE}" STREQUAL "local")
		set (prefix ${PLUGIN_PREFIX})
		set (libdir ${PLUGIN_LIBDIR})
		set (includedir ${PLUGIN_INCDIR})
		if (NOT VERSION)
		    set (VERSION 0.0.1-git)
		endif (NOT VERSION)
		configure_file (
		    ${_${plugin}_pkg}
		    ${CMAKE_CURRENT_BINARY_DIR}/generated/compiz-${plugin}.pc
		)
		
		install (
		    FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/compiz-${plugin}.pc
		    DESTINATION ${PLUGIN_PKGDIR}
		)
		install (
		    FILES ${_${plugin}_hdr}
		    DESTINATION ${PLUGIN_INCDIR}
		)
	    endif ("${PLUGIN_BUILDTYPE}" STREQUAL "local")
	endif (_${plugin}_hdr AND _${plugin}_pkg)

	# install plugin data files
	if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/data)
	    install (
		DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data
		DESTINATION ${PLUGIN_DATADIR}
	    )
	endif (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/data)

	# install plugin image files
	if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/images)
	    install (
		DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/images
		DESTINATION ${PLUGIN_IMAGEDIR}
	    )
	endif (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/images)
	
	# find files for build
	file (GLOB _h_files "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
	file (GLOB _c_files "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
	file (GLOB _cpp_files "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
	file (GLOB _cxx_files "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx")

	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wnested-externs")

	set (_cflags "-Wall -Wpointer-arith  -fno-strict-aliasing")
	

	add_definitions (-DPREFIX='\"${PLUGIN_PREFIX}\"'
			 -DIMAGEDIR='\"${PLUGIN_IMAGEDIR}\"'
			 -DDATADIR='\"${PLUGIN_DATADIR}\"')

	include_directories (
            ${COMPIZ_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CF_PLUGIN_INCLUDE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/generated
            ${${_PLUGIN}_INCDIRS}
	)

	link_directories (
            ${COMPIZ_LINK_DIRS}
            ${${_PLUGIN}_LIBDIRS}
	)

	add_library (
            ${plugin} SHARED ${_c_files}
			     ${_cpp_files}
			     ${_cxx_files}
			     ${_h_files}
			     ${BCOP_SOURCES}
			     ${TRANSLATION_SOURCES}
			     ${SCHEMA_SOURCES}
	)

	set_target_properties (
		${plugin} PROPERTIES
		COMPILE_FLAGS "${_cflags} ${${_PLUGIN}_CFLAGSADD}"
		LINK_FLAGS "${${_PLUGIN}_LDFLAGSADD}"
	)

	target_link_libraries (
	    ${plugin} ${COMPIZ_LIBRARIES}
		      ${${_PLUGIN}_LIBRARIES}
	)

	install (
	    TARGETS ${plugin}
	    LIBRARY DESTINATION ${PLUGIN_LIBDIR}
	)

	compiz_fusion_add_uninstall ()

    else (COMPIZ_FOUND AND ${_PLUGIN}_HAS_PKG_DEPS AND ${_PLUGIN}_HAS_PLUGIN_DEPS)
	message (STATUS "[WARNING] One or more dependencies for compiz plugin ${plugin} not found. Skipping plugin.")
	message (STATUS "Missing dependencies :${CF_${_PLUGIN}_MISSING_DEPS}")
	_cf_set (CF_${_PLUGIN}_BUILD FALSE)
    endif (COMPIZ_FOUND AND ${_PLUGIN}_HAS_PKG_DEPS AND ${_PLUGIN}_HAS_PLUGIN_DEPS)
endmacro (compiz_fusion_plugin)
