# SPDX-FileCopyrightText: 2023 Alexander Lohnau <alexander.lohnau@gmx.de>
# SPDX-License-Identifier: BSD-2-Clause
#
# kcoreaddons_add_plugin(plugin_name
#     [SOURCES <src> [<src> [...]]] # optional since 5.83, required before
#     [STATIC]
#     [INSTALL_NAMESPACE "servicename"]
# )
#
# This macro helps simplifying the creation of plugins for KPluginFactory
# based systems.
# It will create a plugin given the SOURCES list and the INSTALL_NAMESPACE so that
# the plugin is installed with the rest of the plugins from the same sub-system,
# within ${KDE_INSTALL_PLUGINDIR}.
# Since 5.89 the macro supports static plugins by passing in the STATIC option.
#
# Example:
#   kcoreaddons_add_plugin(kdeconnect_share SOURCES ${kdeconnect_share_SRCS} INSTALL_NAMESPACE "kdeconnect")
#
# Since 5.10.0

function(kcoreaddons_add_plugin plugin)
    set(options STATIC)
    set(oneValueArgs INSTALL_NAMESPACE)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(KCA_ADD_PLUGIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT KCA_ADD_PLUGIN_INSTALL_NAMESPACE)
        message(FATAL_ERROR "Must specify INSTALL_NAMESPACE for ${plugin}")
    endif()
    if (KCA_ADD_PLUGIN_UNPARSED_ARGUMENTS)
            message(FATAL_ERROR "kcoreaddons_add_plugin method call recieved unexpected arguments: ${KCA_ADD_PLUGIN_UNPARSED_ARGUMENTS}")
    endif()

    string(REPLACE "-" "_" SANITIZED_PLUGIN_NAME ${plugin})
    string(REPLACE "." "_" SANITIZED_PLUGIN_NAME ${SANITIZED_PLUGIN_NAME})
    if (KCA_ADD_PLUGIN_STATIC)
        add_library(${plugin} STATIC ${KCA_ADD_PLUGIN_SOURCES})
        target_compile_definitions(${plugin} PRIVATE QT_STATICPLUGIN)
        set_property(TARGET ${plugin} PROPERTY AUTOMOC_MOC_OPTIONS -MX-KDE-FileName=${KCA_ADD_PLUGIN_INSTALL_NAMESPACE}/${plugin})
        string(REPLACE "/" "_" SANITIZED_PLUGIN_NAMESPACE ${KCA_ADD_PLUGIN_INSTALL_NAMESPACE})

        if (NOT ${SANITIZED_PLUGIN_NAME} IN_LIST KCOREADDONS_STATIC_PLUGINS${SANITIZED_PLUGIN_NAMESPACE})
            set(KCOREADDONS_STATIC_PLUGINS${SANITIZED_PLUGIN_NAMESPACE} "${KCOREADDONS_STATIC_PLUGINS${SANITIZED_PLUGIN_NAMESPACE}};${SANITIZED_PLUGIN_NAME}" CACHE INTERNAL "list of known static plugins for ${KCA_ADD_PLUGIN_INSTALL_NAMESPACE} namespace, used to generate Q_IMPORT_PLUGIN macros")
        endif()
    else()
        add_library(${plugin} MODULE ${KCA_ADD_PLUGIN_SOURCES})
    endif()

    target_compile_definitions(${plugin} PRIVATE KPLUGINFACTORY_PLUGIN_CLASS_INTERNAL_NAME=${SANITIZED_PLUGIN_NAME}_factory)

    # If we have static plugins there are no plugins to install
    if (KCA_ADD_PLUGIN_STATIC)
        return()
    endif()

    set_target_properties(${plugin} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${KCA_ADD_PLUGIN_INSTALL_NAMESPACE}")

    if (NOT KCOREADDONS_INTERNAL_SKIP_PLUGIN_INSTALLATION)
        if(NOT ANDROID)
            install(TARGETS ${plugin} DESTINATION ${KDE_INSTALL_PLUGINDIR}/${KCA_ADD_PLUGIN_INSTALL_NAMESPACE})
        else()
            string(REPLACE "/" "_" pluginprefix "${KCA_ADD_PLUGIN_INSTALL_NAMESPACE}")
            set_property(TARGET ${plugin} PROPERTY PREFIX "libplugins_")
            if(NOT pluginprefix STREQUAL "")
                set_property(TARGET ${plugin} APPEND_STRING PROPERTY PREFIX "${pluginprefix}_")
            endif()
            install(TARGETS ${plugin} DESTINATION ${KDE_INSTALL_PLUGINDIR})
        endif()
    endif()
endfunction()

# This macro imports the plugins for the given namespace that were
# registered using the kcoreaddons_add_plugin function.
# This includes the K_IMPORT_PLUGIN statements and linking the plugins to the given target.
#
# In case the plugins are used in both the executable and multiple autotests it it recommended to
# bundle the static plugins in a shared lib for the autotests. In case of shared libs the plugin registrations
# will take effect when the test link against it.
#
# Since 5.89
function(kcoreaddons_target_static_plugins app_target plugin_namespace)
    cmake_parse_arguments(ARGS "" "LINK_OPTION" "" ${ARGN})
    set(IMPORT_PLUGIN_STATEMENTS "#include <kstaticpluginhelpers.h>\n\n")
    string(REPLACE "/" "_" SANITIZED_PLUGIN_NAMESPACE ${plugin_namespace})
    set(TMP_PLUGIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/kcoreaddons_static_${SANITIZED_PLUGIN_NAMESPACE}_plugins_tmp.cpp")
    set(PLUGIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/kcoreaddons_static_${SANITIZED_PLUGIN_NAMESPACE}_plugins.cpp")

    foreach(PLUGIN_TARGET_NAME IN LISTS KCOREADDONS_STATIC_PLUGINS${SANITIZED_PLUGIN_NAMESPACE})
        if (PLUGIN_TARGET_NAME)
            set(IMPORT_PLUGIN_STATEMENTS "${IMPORT_PLUGIN_STATEMENTS}K_IMPORT_PLUGIN(QStringLiteral(\"${plugin_namespace}\"), ${PLUGIN_TARGET_NAME}_factory)\;\n")
            target_link_libraries(${app_target} ${ARGS_LINK_OPTION} ${PLUGIN_TARGET_NAME})
        endif()
    endforeach()
    file(WRITE ${TMP_PLUGIN_FILE} ${IMPORT_PLUGIN_STATEMENTS})
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TMP_PLUGIN_FILE} ${PLUGIN_FILE})
    file(REMOVE ${TMP_PLUGIN_FILE})
    target_sources(${app_target} PRIVATE ${PLUGIN_FILE})
endfunction()

# Clear previously set plugins, otherwise Q_IMPORT_PLUGIN statements
# will fail to compile if plugins got removed from the build
get_directory_property(_cache_vars CACHE_VARIABLES)
foreach(CACHED_VAR IN LISTS _cache_vars)
    if (CACHED_VAR MATCHES "^KCOREADDONS_STATIC_PLUGINS")
        set(${CACHED_VAR} "" CACHE INTERNAL "")
    endif()
endforeach()
