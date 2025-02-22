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
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT ARGS_INSTALL_NAMESPACE)
        message(FATAL_ERROR "Must specify INSTALL_NAMESPACE for ${plugin}")
    endif()
    if (ARGS_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "kcoreaddons_add_plugin method call recieved unexpected arguments: ${ARGS_UNPARSED_ARGUMENTS}")
    endif()
    if (NOT BUILD_SHARED_LIBS)
        set(ARGS_STATIC ON)
    endif()

    if (ARGS_STATIC)
        add_library(${plugin} STATIC ${ARGS_SOURCES})
        target_compile_definitions(${plugin} PRIVATE QT_STATICPLUGIN)
        string(REPLACE "/" "_" SANITIZED_PLUGIN_NAMESPACE ${ARGS_INSTALL_NAMESPACE})

        if (NOT ${plugin} IN_LIST KCOREADDONS_STATIC_PLUGINS${SANITIZED_PLUGIN_NAMESPACE})
            set(KCOREADDONS_STATIC_PLUGINS${SANITIZED_PLUGIN_NAMESPACE} "${KCOREADDONS_STATIC_PLUGINS${SANITIZED_PLUGIN_NAMESPACE}};${plugin}" CACHE INTERNAL "list of known static plugins for ${ARGS_INSTALL_NAMESPACE} namespace")
        endif()
        set_target_properties(${plugin} PROPERTIES PLUGIN_INSTALL_NAMESPACE "${ARGS_INSTALL_NAMESPACE}" PLUGIN_NAME "${plugin}")
        set_property(TARGET ${plugin} APPEND PROPERTY EXPORT_PROPERTIES "PLUGIN_INSTALL_NAMESPACE;PLUGIN_NAME")
    else()
        add_library(${plugin} MODULE ${ARGS_SOURCES})
    endif()

    string(REPLACE "." "_" PLUGIN_FACTORY_NAME ${plugin}_factory)
    string(REPLACE "-" "_" PLUGIN_FACTORY_NAME ${PLUGIN_FACTORY_NAME})
    target_compile_definitions(${plugin} PRIVATE KPLUGINFACTORY_PLUGIN_CLASS_INTERNAL_NAME=${PLUGIN_FACTORY_NAME})

    # If we have static plugins there are no plugins to install
    if (ARGS_STATIC)
        return()
    endif()

    set_target_properties(${plugin} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${ARGS_INSTALL_NAMESPACE}")

    if (NOT KCOREADDONS_INTERNAL_SKIP_PLUGIN_INSTALLATION)
        if(NOT ANDROID)
            install(TARGETS ${plugin} DESTINATION ${KDE_INSTALL_PLUGINDIR}/${ARGS_INSTALL_NAMESPACE})
        else()
            string(REPLACE "/" "_" pluginprefix "${ARGS_INSTALL_NAMESPACE}")
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
# This includes the initailization statements and linking the plugins to the given target.
#
# In case the plugins are used in both the executable and multiple autotests it it recommended to
# bundle the static plugins in a shared lib for the autotests. In case of shared libs the plugin registrations
# will take effect when the test link against it.
#
# Since 5.89
function(kcoreaddons_target_static_plugins app_target)
    cmake_parse_arguments(ARGS "" "LINK_OPTION;NAMESPACE;TARGETS" "" ${ARGN})
    if ((ARGS_LINK_OPTION AND ARGS_NAMESPACE) OR (NOT ARGS_NAMESPACE AND NOT ARGS_TARGETS))
        message(FATAL_ERROR "Either NAMESPACE or TARGETS must be set")
    endif()

    # We access this as extern, because then we don't even have to install the header file for it
    set(PLUGIN_REGISTRATION_AUTOGEN "#include <QPluginLoader>\nextern void kRegisterStaticPluginFunction(const QString &, const QString &, QStaticPlugin)\;\n\n")

    macro(generate_plugin_registration PLUGIN_NAME PLUGIN_NAMESPACE)
        string(REPLACE "." "_" PLUGIN_FACTORY_NAME ${PLUGIN_NAME}_factory)
        string(REPLACE "-" "_" PLUGIN_FACTORY_NAME ${PLUGIN_FACTORY_NAME})
        set(REGISTER_PLUGIN_CALL "
extern const QT_PREPEND_NAMESPACE(QStaticPlugin) qt_static_plugin_${PLUGIN_FACTORY_NAME}()\;
class Static${PLUGIN_FACTORY_NAME}PluginInstance
{
public:
Static${PLUGIN_FACTORY_NAME}PluginInstance()
{
kRegisterStaticPluginFunction(
    QStringLiteral(\"${PLUGIN_NAME}\"),
    QStringLiteral(\"${PLUGIN_NAMESPACE}\"),
    qt_static_plugin_${PLUGIN_FACTORY_NAME}()
)\;
}
}\;
static Static${PLUGIN_FACTORY_NAME}PluginInstance static${PLUGIN_FACTORY_NAME}Instance\;")
    set(PLUGIN_REGISTRATION_AUTOGEN "${PLUGIN_REGISTRATION_AUTOGEN}${REGISTER_PLUGIN_CALL}\n")
endmacro()

    if(ARGS_NAMESPACE)
        string(REPLACE "/" "_" SANITIZED_PLUGIN_NAMESPACE ${ARGS_NAMESPACE})
        set(TMP_PLUGIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/kcoreaddons_static_${SANITIZED_PLUGIN_NAMESPACE}_plugins_tmp.cpp")
        set(PLUGIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/kcoreaddons_static_${SANITIZED_PLUGIN_NAMESPACE}_plugins.cpp")

        foreach(PLUGIN_NAME IN LISTS KCOREADDONS_STATIC_PLUGINS${SANITIZED_PLUGIN_NAMESPACE})
            if (PLUGIN_NAME)
                generate_plugin_registration(${PLUGIN_NAME} ${ARGS_NAMESPACE})
                target_link_libraries(${app_target} ${ARGS_LINK_OPTION} ${PLUGIN_NAME})
            endif()
        endforeach()
    elseif(ARGS_TARGETS)
        set(TMP_PLUGIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/kcoreaddons_static_${app_target}_plugins_tmp.cpp")
        set(PLUGIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/kcoreaddons_static_${app_target}_plugins.cpp")
        foreach(ARG_TARGET IN LISTS ARGS_TARGETS)
            get_target_property(PLUGIN_NAME ${ARG_TARGET} PLUGIN_NAME)
            get_target_property(PLUGIN_INSTALL_NAMESPACE ${ARG_TARGET} PLUGIN_INSTALL_NAMESPACE)
            generate_plugin_registration(${PLUGIN_NAME} ${PLUGIN_INSTALL_NAMESPACE})
            target_link_libraries(${app_target} ${ARGS_LINK_OPTION} ${ARG_TARGET})
        endforeach()
    endif()

    file(WRITE ${TMP_PLUGIN_FILE} ${PLUGIN_REGISTRATION_AUTOGEN})
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TMP_PLUGIN_FILE} ${PLUGIN_FILE})
    file(REMOVE ${TMP_PLUGIN_FILE})
    target_sources(${app_target} PRIVATE ${PLUGIN_FILE})
endfunction()

# Clear previously set plugins, otherwise the linker will fail due to the symbols of a removed pugin being missing
get_directory_property(_cache_vars CACHE_VARIABLES)
foreach(CACHED_VAR IN LISTS _cache_vars)
    if (CACHED_VAR MATCHES "^KCOREADDONS_STATIC_PLUGINS")
        set(${CACHED_VAR} "" CACHE INTERNAL "")
    endif()
endforeach()
