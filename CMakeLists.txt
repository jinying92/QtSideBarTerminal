cmake_minimum_required(VERSION 3.16)

# Remove when sharing with others.
list(APPEND CMAKE_PREFIX_PATH "H:/Qt/Qt6/Tools/QtCreator")

project(QtSideBarTerminal)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(QtCreator REQUIRED COMPONENTS Core Terminal)
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Add a CMake option that enables building your plugin with tests.
# You don't want your released plugin binaries to contain tests,
# so make that default to 'NO'.
# Enable tests by passing -DWITH_TESTS=ON to CMake.
option(WITH_TESTS "Builds with tests" NO)

if(WITH_TESTS)
  # Look for QtTest
  find_package(Qt6 REQUIRED COMPONENTS Test)

  # Tell CMake functions like add_qtc_plugin about the QtTest component.
  set(IMPLICIT_DEPENDS Qt::Test)

  # Enable ctest for auto tests.
  enable_testing()
endif()

add_qtc_plugin(QtSideBarTerminal
  PLUGIN_DEPENDS
    QtCreator::Core
    QtCreator::Terminal
  DEPENDS
    Qt::Widgets
    QtCreator::ExtensionSystem
    QtCreator::Utils
  SOURCES
    qtsidebarterminal.cpp
    sidebarterminal.h sidebarterminal.cpp
    qtsidebarterminalconstants.h
    qtsidebarterminaltr.h
)

# Terminal 是插件（非库），无法通过 DEPENDS 直接链接。
# Windows MSVC 链接时需要 Terminal 的导入库 (.lib) 来解析符号。
# 在 Linux/macOS 上符号由动态加载器自动解析，无需额外处理。
if(WIN32)
    get_target_property(_core_loc QtCreator::Core LOCATION)
    get_filename_component(_core_dir ${_core_loc} DIRECTORY)
    get_filename_component(_core_dir ${_core_dir} DIRECTORY)
    get_filename_component(_plugin_base ${_core_dir} DIRECTORY)

    find_library(TERMINAL_IMPORT_LIB Terminal
        PATHS "${_plugin_base}"
        PATH_SUFFIXES lib/qtcreator/plugins
        NO_DEFAULT_PATH
    )
    if(TERMINAL_IMPORT_LIB)
        target_link_libraries(QtSideBarTerminal PRIVATE ${TERMINAL_IMPORT_LIB})
        message(STATUS "Found Terminal import library: ${TERMINAL_IMPORT_LIB}")
    else()
        message(WARNING "Terminal import library not found. Linking without Terminal.")
    endif()
endif()

# 将 QtSideBarTerminal.json 拷贝到插件输出目录（与 .dll/.so 同级）
# Qt Creator 加载插件时需要 JSON 元数据文件与二进制文件在同一目录
add_custom_command(TARGET QtSideBarTerminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/QtSideBarTerminal.json"
        "$<TARGET_FILE_DIR:QtSideBarTerminal>"
    COMMENT "Copy QtSideBarTerminal.json to plugin directory"
)

# Enable the Run button in Qt Creator
get_target_property(QtCreatorCorePath QtCreator::Core LOCATION)
find_program(QtCreatorExecutable
  NAMES
    qtcreator "Qt Creator"
  PATHS
    "${QtCreatorCorePath}/../../../bin"
    "${QtCreatorCorePath}/../../../MacOS"
  NO_DEFAULT_PATH
)
if (QtCreatorExecutable)
  add_custom_target(RunQtCreator
    COMMAND ${QtCreatorExecutable} -pluginpath $<TARGET_FILE_DIR:QtSideBarTerminal>
    DEPENDS QtSideBarTerminal
  )
  set_target_properties(RunQtCreator PROPERTIES FOLDER "qtc_runnable")
endif()
