macro (_format_string str length return)
    string (LENGTH "${str}" _str_len)
    math (EXPR _add_chr "${length} - ${_str_len}")
    set (${return} "${str}")
    while (_add_chr GREATER 0)
	set (${return} "${${return}} ")
	math (EXPR _add_chr "${_add_chr} - 1")
    endwhile (_add_chr GREATER 0)
endmacro (_format_string str length return)

string (ASCII 27 _escape)

macro (_color_message _str)
    if (CMAKE_COLOR_MAKEFILE)
	message (${_str})
    else (CMAKE_COLOR_MAKEFILE)
	string (REGEX REPLACE "${_escape}.[0123456789;]*m" "" __str ${_str})
	message (${__str})
    endif (CMAKE_COLOR_MAKEFILE)
endmacro (_color_message)

macro (_get_plugin_stats folder)
    _color_message ("\n${_escape}[4mPlugin configure check results:${_escape}[0m\n")
    file (
	GLOB _plugins_in 
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${folder}" 
	"${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*/CMakeLists.txt"
    )
    foreach (_plugin ${_plugins_in})
	file (READ "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/${_plugin}" _file)
	if (_file MATCHES "^.*compiz_fusion_plugin ?\\(([^\\) ]*).*$")
	    string (
		REGEX REPLACE
		"^.*compiz_fusion_plugin ?\\(([^\\) ]*).*$" "\\1"
		_plugin_name ${_file}
	    )
	else (_file MATCHES "^.*compiz_fusion_plugin ?\\(([^\\) ]*).*$")
	    get_filename_component (_plugin_name ${_plugin} PATH)
	endif (_file MATCHES "^.*compiz_fusion_plugin ?\\(([^\\) ]*).*$")

	string (TOUPPER ${_plugin_name} _PLUGIN)
	_format_string (${_plugin_name} 14 _plugin_name)

	if (CF_DISABLE_PLUGIN_${_PLUGIN})
	    _color_message ("  ${_plugin_name}: ${_escape}[1;34mDisabled${_escape}[0m")
	else (CF_DISABLE_PLUGIN_${_PLUGIN})
	    if (CF_${_PLUGIN}_BUILD)
		_color_message ("  ${_plugin_name}: ${_escape}[1;32mYes${_escape}[0m")
	    else (CF_${_PLUGIN}_BUILD)
		_color_message ("  ${_plugin_name}: ${_escape}[1;31mNo${_escape}[0m (Missing dependencies :${CF_${_PLUGIN}_MISSING_DEPS})")
	    endif (CF_${_PLUGIN}_BUILD)
	endif (CF_DISABLE_PLUGIN_${_PLUGIN})
    endforeach (_plugin ${_plugins_in})
    message ("")
endmacro (_get_plugin_stats)

macro (cf_print_configure_header)
    _format_string ("${PROJECT_NAME}" 40 _project)
    _format_string ("${VERSION}" 40 _version)
    _color_message ("\n${_escape}[40;37m************************************************************${_escape}[0m")
    _color_message ("${_escape}[40;37m* ${_escape}[1;31mCompiz ${_escape}[1;37mFusion ${_escape}[0;40;34mBuildsystem${_escape}[0m${_escape}[40;37m                                *${_escape}[0m")
    _color_message ("${_escape}[40;37m*                                                          *${_escape}[0m")
    _color_message ("${_escape}[40;37m* Package : ${_escape}[32m${_project} ${_escape}[37m      *${_escape}[0m")
    _color_message ("${_escape}[40;37m* Version : ${_escape}[32m${_version} ${_escape}[37m      *${_escape}[0m")
    _color_message ("${_escape}[40;37m************************************************************${_escape}[0m")
endmacro (cf_print_configure_header)

macro (cf_print_configure_footer)
    _color_message ("${_escape}[40;37m************************************************************${_escape}[0m\n")
endmacro (cf_print_configure_footer)


macro (cf_print_plugin_configure_results folder)
    cf_print_configure_header ()
    _get_plugin_stats (${folder})
    cf_print_configure_footer ()
endmacro (cf_print_plugin_configure_results)

macro (cf_add_plugins folder)
    file (
	GLOB _plugins_in 
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${folder}" 
	"${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*/CMakeLists.txt"
    )

    foreach (_plugin ${_plugins_in})
        get_filename_component (_plugin_dir ${_plugin} PATH)
        add_subdirectory (${folder}/${_plugin_dir})
    endforeach (_plugin ${_plugins_in})
endmacro (cf_add_plugins)

macro (cf_install_plugin_data_files folder)

    file (
	GLOB_RECURSE _files_in
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${folder}" 
	"${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*"
    )

    set (_file_names)
    set (_file_filter "(^\\.)|(\\.am$)|(CMakeLists.txt)|(Makefile)" CACHED)
    set (_dir_filter "(^\\.)|(/\\.)" CACHED)

    foreach (_file ${_files_in})
	get_filename_component (_file_name ${_file} NAME)
	get_filename_component (_file_dir ${_file} PATH)
	if (NOT "${_file_name}" MATCHES "${_file_filter}"
	    AND NOT "${_file_dir}" MATCHES "${_dir_filter}")
	    install (
		FILES ${CMAKE_CURRENT_SOURCE_DIR}/${folder}/${_file}
		DESTINATION ${CMAKE_INSTALL_PREFIX}/share/compiz/${_file_dir}
            )
	endif (NOT "${_file_name}" MATCHES "${_file_filter}"
	       AND NOT "${_file_dir}" MATCHES "${_dir_filter}")
    endforeach (_file ${_files_in})

endmacro (cf_install_plugin_data_files)

macro (get_version)
    file (READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _file)
    string (
	REGEX REPLACE
	"^.*VERSION=([^\n]*).*$" "\\1"
	_version ${_file}
    )
    set (VERSION ${_version})
endmacro (get_version)


macro (cf_add_package_generation description)
    include(InstallRequiredSystemLibraries)

    set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "${description}")
    set (CPACK_PACKAGE_VENDOR "Compiz Fusion")
    set (CPACK_PACKAGE_VERSION "${VERSION}")
    set (CPACK_SOURCE_PACKAGE_FILE_NAME "${PROJECT_NAME}-${VERSION}")

    set (CPACK_SOURCE_GENERATOR "TGZ;TBZ2")
    set (CPACK_SOURCE_IGNORE_FILES  "\\\\.#;/#;.*~")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/.git/")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "${CMAKE_CURRENT_BINARY_DIR}")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "Makefile")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "Makefile.*")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/autom4te.cache")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/aclocal.m4")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/autogen.sh")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/*.pc")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/config*")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/depcomp")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/install-sh")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/intltool*")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/libtool")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/ltmain.sh")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/missing")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/mkinstalldirs")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/stamp-h1")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/\\\\*.xml")
    list (APPEND CPACK_SOURCE_IGNORE_FILES ".intltool-merge-cache")
    list (APPEND CPACK_SOURCE_IGNORE_FILES ".deps")
    list (APPEND CPACK_SOURCE_IGNORE_FILES ".libs")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/\\\\*.lo")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/\\\\*.o")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/\\\\*_options.*")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/\\\\*.la")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "/\\\\*.schema")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "CMakeCache.txt")
    list (APPEND CPACK_SOURCE_IGNORE_FILES "CMakeFiles")
    include(CPack)

    file (REMOVE "${CMAKE_BINARY_DIR}/CPackConfig.cmake")
endmacro (cf_add_package_generation)