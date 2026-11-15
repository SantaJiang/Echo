cmake_minimum_required(VERSION 3.24)
project(Echo VERSION 1.0.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Sql Concurrent Network)
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Test ShaderTools)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(EBUR128 REQUIRED IMPORTED_TARGET libebur128)

if(UNIX AND NOT APPLE)
  find_package(Qt6 REQUIRED COMPONENTS DBus)
  pkg_check_modules(PIPEWIRE libpipewire-0.3)
  pkg_check_modules(ALSA alsa)
endif()

set(ECHO_CORE_SOURCES
  src/vendor/fooyin/src/ffmpegpcm.cpp
  src/audio/decoder.cpp
  src/audio/cue.cpp
  src/audio/convert.cpp
  src/audio/loudness.cpp
  src/audio/engine.cpp
  src/audio/output.cpp
  src/library/types.cpp
  src/library/database.cpp
  src/library/schema.cpp
  src/library/searchindex.cpp
  src/library/trackrepository.cpp
  src/library/playlistrepository.cpp
  src/library/metadata.cpp
  src/library/artwork.cpp
  src/library/scanner.cpp
  src/library/library.cpp
  src/library/pagequery.cpp
  src/playback/tracksource.h
  src/playback/player.cpp
  src/playback/loudnessservice.cpp
)

if(APPLE)
  list(APPEND ECHO_CORE_SOURCES src/audio/output_coreaudio.mm src/audio/output_spatial_mac.mm)
elseif(WIN32)
  list(APPEND ECHO_CORE_SOURCES src/audio/output_wasapi.cpp)
else()
  if(PIPEWIRE_FOUND)
    list(APPEND ECHO_CORE_SOURCES src/audio/output_pipewire.cpp)
  endif()
  if(ALSA_FOUND)
    list(APPEND ECHO_CORE_SOURCES src/audio/output_alsa.cpp)
  endif()
  list(APPEND ECHO_CORE_SOURCES src/audio/output_null.cpp)
endif()

add_library(echo_core STATIC ${ECHO_CORE_SOURCES})
target_include_directories(echo_core PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/vendor/fooyin/include)
target_link_libraries(echo_core PUBLIC Qt6::Core Qt6::Gui Qt6::Sql PkgConfig::FFMPEG PkgConfig::TAGLIB PkgConfig::EBUR128 Qt6::Concurrent)
get_filename_component(ECHO_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
target_compile_definitions(echo_core PUBLIC "ECHO_QT_PLUGINS=\"${ECHO_QT_PREFIX}/share/qt/plugins\"")
if(PIPEWIRE_FOUND)
  target_compile_definitions(echo_core PUBLIC ECHO_HAS_PIPEWIRE=1)
  target_include_directories(echo_core PUBLIC ${PIPEWIRE_INCLUDE_DIRS})
  target_link_libraries(echo_core PUBLIC ${PIPEWIRE_LINK_LIBRARIES})
endif()
if(ALSA_FOUND)
  target_compile_definitions(echo_core PUBLIC ECHO_HAS_ALSA=1)
  target_include_directories(echo_core PUBLIC ${ALSA_INCLUDE_DIRS})
  target_link_libraries(echo_core PUBLIC ${ALSA_LINK_LIBRARIES})
endif()
if(APPLE)
  target_link_libraries(echo_core PUBLIC "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation" "-framework AVFoundation" "-framework CoreMedia")
  set_source_files_properties(src/audio/output_spatial_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_source_files_properties(src/audio/output_coreaudio.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
if(WIN32)
  target_link_libraries(echo_core PUBLIC ole32 uuid avrt ksguid ksuser)
endif()
if(MSVC)
  target_compile_options(echo_core PRIVATE /W4 /EHsc)
else()
  target_compile_options(echo_core PRIVATE -Wall -Wextra)
endif()

set(ECHO_SUPPORT_SOURCES src/presentation/theme.cpp src/presentation/icons.cpp src/platform/platform.cpp resources/echo.qrc)
if(APPLE)
  list(APPEND ECHO_SUPPORT_SOURCES src/platform/platform_mac.mm)
elseif(WIN32)
  list(APPEND ECHO_SUPPORT_SOURCES src/platform/platform_win.cpp)
else()
  list(APPEND ECHO_SUPPORT_SOURCES src/platform/platform_linux.cpp)
endif()
add_library(echo_ui_support STATIC ${ECHO_SUPPORT_SOURCES})
target_link_libraries(echo_ui_support PUBLIC echo_core Qt6::Network)
add_library(echo_quick STATIC
  src/quick/playbackview.cpp src/quick/quickview.cpp src/quick/client.cpp
  src/quick/dialogservice.cpp src/quick/librarypresenter.cpp src/quick/menuservice.cpp
  src/quick/selectionmodel.cpp src/quick/systemintegration.cpp
  src/quick/rowmodel.h src/quick/dialogservice.h src/quick/librarypresenter.h src/quick/systemintegration.h
  src/quick/quick.qrc)
qt_add_shaders(echo_quick playback_shaders
  PREFIX "/echo/quick/shaders"
  BASE "src/quick/shaders"
  FILES src/quick/shaders/background.frag src/quick/shaders/backgroundblur.frag)
target_include_directories(echo_quick PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(echo_quick PUBLIC echo_ui_support Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
add_executable(echo src/main.cpp)
target_compile_definitions(echo PRIVATE "ECHO_VERSION=\"${PROJECT_VERSION}\"")
target_link_libraries(echo PRIVATE echo_quick)
if(APPLE)
  target_link_libraries(echo_ui_support PUBLIC "-framework AppKit" "-framework MediaPlayer")
  target_sources(echo PRIVATE data/echo.icns)
  set_source_files_properties(data/echo.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-version
    OUTPUT_VARIABLE macos_sdk_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(macos_sdk_version VERSION_GREATER_EQUAL "26.0")
    set(app_icon_source "${CMAKE_CURRENT_SOURCE_DIR}/data/AppIcon.icon")
    set(app_icon_output "${CMAKE_CURRENT_BINARY_DIR}/macos-icon")
    set(app_icon_catalog "${app_icon_output}/Assets.car")
    add_custom_command(
      OUTPUT "${app_icon_catalog}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${app_icon_output}"
      COMMAND xcrun actool --compile "${app_icon_output}" --platform macosx
              --minimum-deployment-target 12.0 --app-icon AppIcon
              --output-partial-info-plist "${app_icon_output}/Info.plist"
              "${app_icon_source}"
      DEPENDS "${app_icon_source}/icon.json" "${app_icon_source}/Assets/Mark.png"
      VERBATIM
    )
    set_source_files_properties("${app_icon_catalog}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(echo PRIVATE "${app_icon_catalog}")
  endif()
  set_source_files_properties(src/platform/platform_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_target_properties(echo PROPERTIES OUTPUT_NAME Echo MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER org.echo.player MACOSX_BUNDLE_BUNDLE_NAME Echo
    MACOSX_BUNDLE_ICON_FILE echo.icns MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/data/MacOSXBundleInfo.plist.in)
elseif(WIN32)
  enable_language(RC)
  target_sources(echo PRIVATE data/echo.rc)
  target_include_directories(echo PRIVATE ${CMAKE_SOURCE_DIR}/data)
  target_link_libraries(echo_ui_support PUBLIC dwmapi shell32)
  set_target_properties(echo PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME Echo)
else()
  set_target_properties(echo PROPERTIES OUTPUT_NAME Echo)
  if(Qt6DBus_FOUND)
    target_compile_definitions(echo_ui_support PRIVATE ECHO_HAS_MPRIS=1)
    target_link_libraries(echo_ui_support PRIVATE Qt6::DBus)
  endif()
endif()
if(NOT MSVC)
  target_compile_options(echo PRIVATE -Wall -Wextra)
  target_compile_options(echo_quick PRIVATE -Wall -Wextra)
  target_compile_options(echo_ui_support PRIVATE -Wall -Wextra)
endif()
add_library(echo_test_support STATIC tools/test_support/support.cpp)
target_include_directories(echo_test_support PUBLIC ${CMAKE_SOURCE_DIR}/tools ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(echo_test_support PUBLIC echo_core)
if(NOT MSVC)
  target_compile_options(echo_test_support PRIVATE -Wall -Wextra)
endif()

add_executable(echo-quicktest tools/quicktest.cpp tools/quicktest_harness.cpp tools/quicktest_layout.cpp tools/quicktest_background.cpp tools/quicktest_scenes.cpp tools/quicktest_suite.cpp)
target_link_libraries(echo-quicktest PRIVATE echo_quick echo_test_support Qt6::Test)
if(APPLE)
  target_sources(echo-quicktest PRIVATE tools/quicktest_mac.mm)
  set_source_files_properties(tools/quicktest_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
add_executable(echo-foldertest tools/foldertest.cpp)
target_link_libraries(echo-foldertest PRIVATE echo_ui_support echo_test_support)
add_executable(echo-probe tools/probe.cpp)
target_link_libraries(echo-probe PRIVATE echo_core)
add_executable(echo-selftest tools/selftest.cpp)
target_link_libraries(echo-selftest PRIVATE echo_core)
add_executable(echo-playbacktest tools/playbacktest.cpp)
target_link_libraries(echo-playbacktest PRIVATE echo_core echo_test_support)
if(APPLE)
  add_executable(echo-spatialtest tools/spatialtest.cpp)
  target_link_libraries(echo-spatialtest PRIVATE echo_core echo_test_support)
endif()

enable_testing()
add_test(NAME selftest COMMAND echo-selftest)
add_test(NAME playbacktest COMMAND echo-playbacktest)
add_test(NAME foldertest COMMAND echo-foldertest)
add_test(NAME quick-smoke COMMAND echo-quicktest --qml-smoke)
set_tests_properties(quick-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(APPLE)
  get_filename_component(ECHO_QT_BIN "${Qt6_DIR}/../../../bin" ABSOLUTE)
  find_program(ECHO_MACDEPLOYQT macdeployqt HINTS "${ECHO_QT_BIN}")
endif()
if(WIN32)
  get_filename_component(ECHO_QT_BIN "${Qt6_DIR}/../../../bin" ABSOLUTE)
  find_program(ECHO_WINDEPLOYQT windeployqt HINTS "${ECHO_QT_BIN}")
endif()

include(GNUInstallDirs)
install(TARGETS echo BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(ECHO_MACDEPLOYQT)
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
  install(CODE "execute_process(COMMAND \"${Python3_EXECUTABLE}\" \"${CMAKE_SOURCE_DIR}/tools/deploy_macos.py\" --bundle \"\${CMAKE_INSTALL_PREFIX}/Echo.app\" --qt \"${ECHO_QT_PREFIX}\" --qml \"${CMAKE_SOURCE_DIR}/src/quick/qml\" --macdeployqt \"${ECHO_MACDEPLOYQT}\" COMMAND_ERROR_IS_FATAL ANY)")
endif()
if(ECHO_WINDEPLOYQT)
  install(CODE "execute_process(COMMAND \"${ECHO_WINDEPLOYQT}\" \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/Echo.exe\" --qmldir \"${CMAKE_SOURCE_DIR}/src/quick/qml\" COMMAND_ERROR_IS_FATAL ANY)")
endif()
option(ECHO_INSTALL_PROBE "Install the echo-probe diagnostic tool" OFF)
if(ECHO_INSTALL_PROBE)
  install(TARGETS echo-probe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(UNIX AND NOT APPLE)
  install(FILES data/echo.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES data/echo.svg DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
endif()

set(CPACK_PACKAGE_NAME "Echo")
set(CPACK_PACKAGE_VENDOR "Echo")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Local high-quality music player")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/NOTICE")
if(APPLE)
  set(CPACK_GENERATOR DragNDrop)
elseif(WIN32)
  set(CPACK_GENERATOR NSIS)
  set(CPACK_NSIS_DISPLAY_NAME "Echo")
  set(CPACK_NSIS_PACKAGE_NAME "Echo")
else()
  set(CPACK_GENERATOR DEB)
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Echo")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()
include(CPack)
