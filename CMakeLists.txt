cmake_minimum_required(VERSION 3.16)
project(ReduceWhitePoint VERSION 1.0 LANGUAGES CXX)
include(GNUInstallDirs)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(portable_default OFF)
if(WIN32 OR APPLE)
    set(portable_default ON)
endif()
option(REDUCE_WHITE_DEPLOY_RUNTIME "Install Qt runtime libraries and plugins for a portable distribution" ${portable_default})
if(REDUCE_WHITE_DEPLOY_RUNTIME AND UNIX AND NOT APPLE)
    find_program(REDUCE_WHITE_PATCHELF patchelf)
    if(NOT REDUCE_WHITE_PATCHELF)
        message(FATAL_ERROR "Portable Linux deployment requires patchelf to relocate Qt plugins. Install patchelf or use -DREDUCE_WHITE_DEPLOY_RUNTIME=OFF.")
    endif()
    set(QT_DEPLOY_USE_PATCHELF ON)
    set(QT_DEPLOY_PATCHELF_EXECUTABLE "${REDUCE_WHITE_PATCHELF}")
endif()

find_package(Qt6 6.2 COMPONENTS Core Gui Network REQUIRED)
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(qt_opengl_libraries WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(qt_opengl_libraries MATCHES "AGL")
        find_library(REDUCE_WHITE_AGL_FRAMEWORK AGL)
        if(NOT REDUCE_WHITE_AGL_FRAMEWORK)
            message(FATAL_ERROR "This Qt kit references AGL, which is absent from the selected macOS SDK. Use a current Qt kit (such as Qt 6.10+) or an older compatible Xcode SDK.")
        endif()
    endif()
endif()
include(CheckCXXSourceCompiles)
option(REDUCE_WHITE_FORCE_QT_NUMBER_PARSER "Use Qt's C locale instead of floating-point from_chars" OFF)
check_cxx_source_compiles("#include <charconv>
int main() { const char value[] = \"0.5\"; double number;
return std::from_chars(value, value + 3, number).ec == std::errc() ? 0 : 1; }"
    REDUCE_WHITE_HAS_FLOAT_FROM_CHARS)

# Enable Interprocedural Optimization (LTO) for smaller/faster binaries if supported
option(REDUCE_WHITE_ENABLE_IPO "Enable link-time optimization in release builds" ON)
if(REDUCE_WHITE_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT error LANGUAGES CXX)
    if(ipo_supported)
        message(STATUS "IPO / LTO enabled for release builds")
    endif()
endif()

# Optional LayerShellQt for Linux Wayland
if(UNIX AND NOT APPLE)
    find_package(LayerShellQt 6 QUIET)
    if(LayerShellQt_FOUND)
        message(STATUS "Found LayerShellQt. Wayland overlay support enabled.")
    else()
        message(STATUS "LayerShellQt NOT found. Falling back to standard Qt window flags.")
    endif()
endif()

set(SOURCES
    src/main.cpp
    src/OverlayWindow.cpp
    src/Daemon.cpp
)

# Keep CLI stdout/stderr available on Windows; detached daemons use QProcess.
qt_add_executable(reduce-white ${SOURCES})
set_target_properties(reduce-white PROPERTIES WIN32_EXECUTABLE FALSE)
if(REDUCE_WHITE_HAS_FLOAT_FROM_CHARS AND NOT REDUCE_WHITE_FORCE_QT_NUMBER_PARSER)
    target_compile_definitions(reduce-white PRIVATE REDUCE_WHITE_HAS_FLOAT_FROM_CHARS)
endif()
if(APPLE)
    target_sources(reduce-white PRIVATE src/MacPlatform.mm)
    find_library(APPKIT_FRAMEWORK AppKit)
    if(NOT APPKIT_FRAMEWORK)
        message(FATAL_ERROR "The macOS AppKit framework was not found; install the Xcode Command Line Tools.")
    endif()
    target_link_libraries(reduce-white PRIVATE ${APPKIT_FRAMEWORK})
    set_target_properties(reduce-white PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
endif()
if(REDUCE_WHITE_ENABLE_IPO AND ipo_supported)
    set_target_properties(reduce-white PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

target_link_libraries(reduce-white PRIVATE Qt6::Core Qt6::Gui Qt6::Network)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(reduce-white PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(reduce-white PRIVATE /W4)
endif()

if(LayerShellQt_FOUND)
    target_compile_definitions(reduce-white PRIVATE HAVE_LAYER_SHELL)
    target_link_libraries(reduce-white PRIVATE LayerShellQt::Interface)
endif()

if(UNIX AND NOT APPLE)
    file(RELATIVE_PATH REDUCE_WHITE_LIB_RELATIVE_DIR
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(reduce-white PROPERTIES
        INSTALL_RPATH "$ORIGIN/${REDUCE_WHITE_LIB_RELATIVE_DIR}")
endif()
install(TARGETS reduce-white
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .)
if(APPLE)
    file(RELATIVE_PATH REDUCE_WHITE_BUNDLE_RELATIVE_DIR
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}" "${CMAKE_INSTALL_PREFIX}")
    configure_file(cmake/reduce-white-macos.sh.in reduce-white-macos.sh @ONLY)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/reduce-white-macos.sh"
        DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME reduce-white)
endif()

if(REDUCE_WHITE_DEPLOY_RUNTIME)
    if(Qt6_VERSION VERSION_LESS 6.5 OR NOT COMMAND qt6_generate_deploy_script)
        message(FATAL_ERROR "Portable deployment requires Qt 6.5 or newer; use -DREDUCE_WHITE_DEPLOY_RUNTIME=OFF for a system-Qt installation.")
    endif()
    set(deploy_options NO_TRANSLATIONS)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10 AND NOT APPLE)
        # A black raster overlay does not need image codecs, icon engines, or desktop themes.
        list(APPEND deploy_options EXCLUDE_PLUGIN_TYPES
            imageformats iconengines platformthemes egldeviceintegrations tls networkinformation
            qmltooling scenegraph)
        set(deploy_plugins qoffscreen)
        if(UNIX AND NOT APPLE)
            list(APPEND deploy_plugins qwayland qwayland-generic qwayland-egl)
        endif()
        list(APPEND deploy_options INCLUDE_PLUGINS ${deploy_plugins})
    endif()
    if(UNIX AND NOT APPLE)
        list(APPEND deploy_options PRE_EXCLUDE_REGEXES
            "^libc[.]so.*" "^libm[.]so.*" "^libpthread[.]so.*" "^libdl[.]so.*"
            "^librt[.]so.*" "^libresolv[.]so.*" "^libnss_.*" "^ld-linux.*")
    endif()
    # Deployment tools normally select only the default desktop platform.
    # Explicitly stage the headless test plugin and available Wayland platforms,
    # then include their dependencies in the deployment scan on every Qt version.
    if(APPLE)
        set(deploy_executable "reduce-white.app")
        set(deploy_plugin_dir "reduce-white.app/Contents/PlugIns/platforms")
    else()
        set(deploy_executable "$<TARGET_FILE:reduce-white>")
        set(deploy_plugin_dir "plugins/platforms")
        list(APPEND deploy_options GENERATE_QT_CONF PLUGINS_DIR plugins)
    endif()
    set(deploy_modules "")
    foreach(plugin IN ITEMS QOffscreenIntegrationPlugin QWaylandIntegrationPlugin
            QWaylandEglPlatformIntegrationPlugin)
        if(TARGET Qt6::${plugin})
            install(FILES "$<TARGET_FILE:Qt6::${plugin}>" DESTINATION ${deploy_plugin_dir})
            string(APPEND deploy_modules " \"${deploy_plugin_dir}/$<TARGET_FILE_NAME:Qt6::${plugin}>\"")
        endif()
    endforeach()
    string(JOIN " " deploy_options_text ${deploy_options})
    set(deploy_extra "")
    if(UNIX AND NOT APPLE)
        # Older Qt tools don't select explicitly staged platform modules as
        # default plugins, so ensure those copies also have relative RPATHs.
        set(deploy_extra "
foreach(plugin IN ITEMS ${deploy_modules})
    execute_process(COMMAND \"${REDUCE_WHITE_PATCHELF}\" --set-rpath
        \"$ORIGIN/../../${CMAKE_INSTALL_LIBDIR}\" \"\${QT_DEPLOY_PREFIX}/\${plugin}\"
        RESULT_VARIABLE patch_result)
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR \"Cannot relocate platform plugin: \${plugin}\")
    endif()
endforeach()")
    endif()
    # Qt's generated support script sets its own policy version. Set the default
    # before it is included so recent CMake versions do not emit path warnings.
    install(CODE [[
        set(CMAKE_POLICY_DEFAULT_CMP0207 NEW)
        # Qt requires an absolute deployment root even when --prefix is relative.
        get_filename_component(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)
    ]])
    qt6_generate_deploy_script(TARGET reduce-white OUTPUT_SCRIPT deploy_script CONTENT
        "qt_deploy_runtime_dependencies(
            EXECUTABLE \"${deploy_executable}\"
            ADDITIONAL_MODULES ${deploy_modules}
            ${deploy_options_text})
        ${deploy_extra}")
    install(SCRIPT ${deploy_script})
endif()
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(ipc-smoke tests/ipc_smoke.cpp)
    set_target_properties(ipc-smoke PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
    target_include_directories(ipc-smoke PRIVATE src)
    target_link_libraries(ipc-smoke PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME native-ipc COMMAND ipc-smoke $<TARGET_FILE:reduce-white>)
    set_tests_properties(native-ipc PROPERTIES TIMEOUT 30)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME regression COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_regression.py $<TARGET_FILE:reduce-white>)
        set_tests_properties(regression PROPERTIES TIMEOUT 60)
    endif()
endif()

set(CPACK_PACKAGE_NAME "reduce-white")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Click-through screen dimming overlay")
set(CPACK_PACKAGE_FILE_NAME "reduce-white-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32 OR APPLE)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
