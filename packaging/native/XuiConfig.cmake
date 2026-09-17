if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "XUI packages require 64-bit Windows.")
endif()
if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$" OR MSVC_CXX_ARCHITECTURE_ID STREQUAL "ARM64")
    set(_xui_arch ARM64)
    set(_xui_rid win-arm64)
else()
    set(_xui_arch x64)
    set(_xui_rid win-x64)
endif()
if(NOT TARGET Xui::Core)
    add_library(Xui::Core STATIC IMPORTED)
    set_target_properties(Xui::Core PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib/${_xui_arch}/xui_core.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/include"
        INTERFACE_COMPILE_FEATURES cxx_std_20)
    add_library(Xui::Windows STATIC IMPORTED)
    set_target_properties(Xui::Windows PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib/${_xui_arch}/xui_windows.lib"
        INTERFACE_LINK_LIBRARIES "Xui::Core;d2d1;dwrite;dwmapi;uiautomationcore;ole32;oleaut32;comctl32;shell32;user32;gdi32;windowscodecs;uxtheme;mfuuid"
        INTERFACE_COMPILE_DEFINITIONS "UNICODE;_UNICODE;NOMINMAX;WIN32_LEAN_AND_MEAN;_WIN32_WINNT=0x0A00")
    add_library(Xui::CAbi SHARED IMPORTED)
    set_target_properties(Xui::CAbi PROPERTIES
        IMPORTED_IMPLIB "${CMAKE_CURRENT_LIST_DIR}/lib/${_xui_arch}/xui.lib"
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../runtimes/${_xui_rid}/native/xui.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/include")
endif()
set(Xui_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/xui.manifest")
set(Xui_PREVIEW_HOST "${CMAKE_CURRENT_LIST_DIR}/../../runtimes/${_xui_rid}/native/xui_preview_host.exe")
function(xui_deploy_preview_host target)
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${Xui_PREVIEW_HOST}" "$<TARGET_FILE_DIR:${target}>/xui_preview_host.exe")
endfunction()
