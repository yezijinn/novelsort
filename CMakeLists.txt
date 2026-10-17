cmake_minimum_required(VERSION 3.20)

project(novel_lines_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W4)
endif()

add_library(novel_core STATIC
  src/core/TextFile.cpp
  src/core/ChapterParser.cpp
  src/core/ChapterOrder.cpp
  src/core/DirectoryEdit.cpp
)
target_include_directories(novel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(novellines-cli
  src/cli/main.cpp
)
target_link_libraries(novellines-cli PRIVATE novel_core)

# ===== 桌面 GUI（纯 C++ / Qt Widgets）=====
# Qt 只做窗口与绘制，业务全部在 novel_core（C++20，不依赖 Qt）。
# 找不到 Qt5 时仍可构建 core / CLI / 测试。
find_package(Qt5 COMPONENTS Widgets QUIET)
if(Qt5_FOUND)
  set(CMAKE_AUTOMOC ON)

  add_executable(novellines-gui WIN32
    src/ui/main.cpp
    src/ui/MainWindow.cpp
    src/ui/ChapterModel.cpp
    src/ui/ChapterTableView.cpp
    src/ui/PreviewEdit.cpp
    src/ui/Theme.cpp
    src/ui/CheckBoxStyle.cpp
  )
  target_link_libraries(novellines-gui PRIVATE novel_core Qt5::Widgets)
  target_include_directories(novellines-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  if(MSVC)
    target_compile_options(novellines-gui PRIVATE /utf-8 /W4)
  endif()
  if(WIN32 AND MSVC AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/novelsort.ico")
    configure_file(resources/novelsort.ico
                   "${CMAKE_CURRENT_BINARY_DIR}/novelsort.ico" COPYONLY)
    target_sources(novellines-gui PRIVATE resources/version.rc)
    target_include_directories(novellines-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  endif()

  # 纯 CMake 部署目标：cmake --build build --config Release --target deploy-gui
  get_target_property(_qt_qmake Qt5::qmake IMPORTED_LOCATION)
  if(_qt_qmake)
    get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin}")
    if(WINDEPLOYQT_EXECUTABLE)
      set(_qt_extra_dlls "")
      foreach(_dll libpng16.dll zlib.dll)
        if(EXISTS "${_qt_bin}/${_dll}")
          list(APPEND _qt_extra_dlls "${_qt_bin}/${_dll}")
        endif()
      endforeach()
      add_custom_target(deploy-gui
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --no-translations
                "$<TARGET_FILE:novellines-gui>"
        COMMENT "部署 Qt 运行库到 novellines-gui 输出目录"
        DEPENDS novellines-gui
        VERBATIM)
      if(_qt_extra_dlls)
        add_custom_command(TARGET deploy-gui POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_qt_extra_dlls}
                  "$<TARGET_FILE_DIR:novellines-gui>"
          VERBATIM)
      endif()
    endif()
  endif()
else()
  message(STATUS "Qt5 Widgets 未找到：跳过 novellines-gui")
endif()

enable_testing()

add_executable(novel_tests tests/test_main.cpp)
target_link_libraries(novel_tests PRIVATE novel_core)
target_compile_definitions(novel_tests PRIVATE
  NOVEL_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
)

add_test(NAME novel_core_tests COMMAND novel_tests)
