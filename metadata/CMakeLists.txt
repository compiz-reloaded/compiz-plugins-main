file (GLOB metadata_in "${CMAKE_CURRENT_SOURCE_DIR}/*.xml.in")

foreach (metadata_in_file ${metadata_in})
    get_filename_component (metadata_name ${metadata_in_file} NAME_WE)
    set (metadata_file ${CMAKE_CURRENT_BINARY_DIR}/${metadata_name}.xml)
    gen_xml_metadata (${metadata_name})
    add_custom_target (${metadata_name}_xml ALL DEPENDS ${metadata_file})
    install (
        FILES ${metadata_file}
        DESTINATION share/compiz
    )
endforeach (metadata_in_file ${metadata_in})
