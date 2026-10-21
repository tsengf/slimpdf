cmake_minimum_required(VERSION 3.19)
project(slimpdf VERSION 1.0.0 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Pdf Svg)
qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/main_window.h
    src/main_window.cpp
)
if(APPLE)
    list(APPEND SOURCES src/macos_dock.h src/macos_dock.mm)
    set_source_files_properties(src/macos_dock.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
endif()

qt_add_executable(slimpdf WIN32 MACOSX_BUNDLE ${SOURCES})

target_include_directories(slimpdf PRIVATE src)

qt_add_resources(slimpdf "icons"
    PREFIX "/"
    FILES
        icons/zoom-in.svg
        icons/zoom-out.svg
        icons/zoom-reset.svg
        icons/fit-width.svg
        icons/fit-height.svg
        icons/fit-page.svg
)

target_link_libraries(slimpdf PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Pdf
    Qt6::Svg
)

set_target_properties(slimpdf PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER  com.slimpdf.app
    MACOSX_BUNDLE_BUNDLE_NAME     "SlimPDF"
    MACOSX_BUNDLE_BUNDLE_VERSION  ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING
        ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

install(TARGETS slimpdf
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
