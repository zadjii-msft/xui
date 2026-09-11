cmake_minimum_required(VERSION 3.24)
project(xui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(xui_core src/core.cpp src/views.cpp src/controls.cpp)
target_include_directories(xui_core PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(xui_core PUBLIC Threads::Threads)

include(CTest)
if(BUILD_TESTING)
    add_executable(xui_core_tests tests/core_tests.cpp)
    target_link_libraries(xui_core_tests PRIVATE xui_core)
    add_test(NAME xui_core_tests COMMAND xui_core_tests)
    add_executable(xui_control_tests tests/control_tests.cpp)
    target_link_libraries(xui_control_tests PRIVATE xui_core)
    add_test(NAME xui_control_tests COMMAND xui_control_tests)
    add_executable(xui_performance_tests tests/performance_tests.cpp)
    target_link_libraries(xui_performance_tests PRIVATE xui_core)
    if(WIN32)
        target_link_libraries(xui_performance_tests PRIVATE psapi)
    endif()
    add_test(NAME xui_performance_tests COMMAND xui_performance_tests)
    set_tests_properties(xui_performance_tests PROPERTIES TIMEOUT 60)
endif()

if(WIN32)
    enable_language(RC)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The initial Windows backend requires a 64-bit build.")
    endif()
    add_library(xui_windows
        src/accessibility.cpp
        src/drawing.cpp
        src/native_edit.cpp
        src/window_host.cpp
        src/application.cpp
        src/list_peer.cpp
        src/async.cpp
        src/control_accessibility.cpp
        )
    target_link_libraries(xui_windows PUBLIC xui_core
        d2d1 dwrite dwmapi uiautomationcore ole32 oleaut32 comctl32 shell32 user32 gdi32)
    target_compile_definitions(xui_windows PUBLIC
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_include_directories(xui_windows PUBLIC include)
    add_executable(xui_demo WIN32 demo/main.cpp demo/browser.cpp demo/directory.cpp demo/xui.rc)
    target_include_directories(xui_demo PRIVATE demo)
    target_link_libraries(xui_demo PRIVATE xui_windows)
    add_executable(xui_gallery WIN32 demo/gallery.cpp demo/xui.rc)
    target_include_directories(xui_gallery PRIVATE demo)
    target_link_libraries(xui_gallery PRIVATE xui_windows)
    if(MSVC)
        target_link_options(xui_demo PRIVATE /MANIFEST:NO)
        target_link_options(xui_gallery PRIVATE /MANIFEST:NO)
    endif()
    if(BUILD_TESTING)
        add_executable(xui_windows_smoke tests/windows_smoke.cpp)
        target_link_libraries(xui_windows_smoke PRIVATE ole32 oleaut32 uiautomationcore user32 psapi)
        target_compile_definitions(xui_windows_smoke PRIVATE
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
        add_executable(xui_gallery_smoke tests/gallery_smoke.cpp)
        target_link_libraries(xui_gallery_smoke PRIVATE ole32 oleaut32 uiautomationcore user32 gdi32)
        target_compile_definitions(xui_gallery_smoke PRIVATE
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
        add_executable(xui_window_tests tests/window_tests.cpp demo/xui.rc)
        target_include_directories(xui_window_tests PRIVATE demo)
        target_link_libraries(xui_window_tests PRIVATE xui_windows)
        if(MSVC)
            target_link_options(xui_window_tests PRIVATE /MANIFEST:NO)
        endif()
        option(XUI_DESKTOP_TESTS "Register tests that open desktop windows" OFF)
        if(XUI_DESKTOP_TESTS)
            add_test(NAME xui_windows_smoke COMMAND xui_windows_smoke $<TARGET_FILE:xui_demo>)
            set_tests_properties(xui_windows_smoke PROPERTIES TIMEOUT 60 LABELS desktop)
            add_test(NAME xui_gallery_smoke COMMAND xui_gallery_smoke $<TARGET_FILE:xui_gallery>)
            set_tests_properties(xui_gallery_smoke PROPERTIES TIMEOUT 60 LABELS desktop)
            add_test(NAME xui_window_tests COMMAND xui_window_tests)
            set_tests_properties(xui_window_tests PROPERTIES TIMEOUT 30 LABELS desktop)
        endif()
    endif()
endif()

if(MSVC)
    foreach(target xui_core xui_core_tests xui_control_tests xui_performance_tests xui_windows xui_demo xui_gallery xui_windows_smoke xui_gallery_smoke xui_window_tests)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
        endif()
    endforeach()
endif()
