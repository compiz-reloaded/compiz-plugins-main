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

	if (CF_${_PLUGIN}_BUILD)
	    _color_message ("  ${_plugin_name}: ${_escape}[1;32mYes${_escape}[0m")
	else (CF_${_PLUGIN}_BUILD)
	    _color_message ("  ${_plugin_name}: ${_escape}[1;31mNo${_escape}[0m")
	endif (CF_${_PLUGIN}_BUILD)
    endforeach (_plugin ${_plugins_in})
endmacro (_get_plugin_stats)

macro (cf_print_configure_results folder)
    _format_string ("${PROJECT_NAME}" 40 _project)
    _format_string ("${VERSION}" 40 _version)
    _color_message ("\n${_escape}[40;37m************************************************************${_escape}[0m")
    _color_message ("${_escape}[40;37m* ${_escape}[1;31mCompiz ${_escape}[1;37mFusion ${_escape}[0;40;34mBuildsystem${_escape}[0m${_escape}[40;37m                                *${_escape}[0m")
    _color_message ("${_escape}[40;37m*                                                          *${_escape}[0m")
    _color_message ("${_escape}[40;37m* Package : ${_escape}[32m${_project} ${_escape}[37m      *${_escape}[0m")
    _color_message ("${_escape}[40;37m* Version : ${_escape}[32m${_version} ${_escape}[37m      *${_escape}[0m")
    _color_message ("${_escape}[40;37m************************************************************${_escape}[0m")
    _color_message ("\n${_escape}[4mPlugin configure check results:${_escape}[0m\n")
    _get_plugin_stats (${folder})
    message ("")
    _color_message ("${_escape}[40;37m************************************************************${_escape}[0m\n")
endmacro (cf_print_configure_results)

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

macro (cf_install_data_files folder)

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

endmacro (cf_install_data_files)

macro (get_version)
    file (READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _file)
    string (
	REGEX REPLACE
	"^.*VERSION=([^\n]*).*$" "\\1"
	_version ${_file}
    )
    set (VERSION ${_version})
endmacro (get_version)