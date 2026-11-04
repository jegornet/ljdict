cmake_minimum_required(VERSION 3.24)
project(LjDict VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "" FORCE)
  # Homebrew's libraries are built for the current system and say so at every link.
  add_link_options(-Wl,-w)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
find_package(Threads REQUIRED)

add_library(ljdict_core STATIC
  src/core/text_folding.cpp
  src/core/stardict_book.cpp
  src/core/dictionary_library.cpp
  src/core/article_renderer.cpp
  src/core/rich_text.cpp)
target_include_directories(ljdict_core PUBLIC src)
target_link_libraries(ljdict_core PUBLIC PkgConfig::GLIB Threads::Threads)

add_executable(LjDict MACOSX_BUNDLE
  src/app/main.cpp
  src/app/application.cpp
  src/app/app_model.cpp
  src/app/settings.cpp
  src/app/search_history.cpp
  src/app/main_window.cpp
  src/app/word_list.cpp
  src/app/article_view.cpp
  src/app/settings_window.cpp)
target_link_libraries(LjDict PRIVATE ljdict_core PkgConfig::GTKMM)
target_compile_definitions(LjDict PRIVATE LJDICT_VERSION="${PROJECT_VERSION}")
if(APPLE)
  target_sources(LjDict PRIVATE src/platform/platform_mac.mm)
  target_link_libraries(LjDict PRIVATE
    "-framework AppKit" "-framework Carbon" "-framework ServiceManagement")
  # Always a bundle: outside one, macOS shows the generic "exec" icon in the Dock.
  target_sources(LjDict PRIVATE resources/AppIcon.icns)
  set_source_files_properties(resources/AppIcon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(LjDict PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist.in")
  add_custom_command(TARGET LjDict POST_BUILD COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:LjDict>")
else()
  target_sources(LjDict PRIVATE src/platform/platform_stub.cpp)
  # The desktop file and icon are named by the application id, as Wayland matches windows by it.
  include(GNUInstallDirs)
  set_target_properties(LjDict PROPERTIES OUTPUT_NAME ljdict)
  install(TARGETS LjDict RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES resources/linux/net.jegor.ljdict.gtk.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES resources/linux/net.jegor.ljdict.gtk.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  foreach(size 16 32 64 128 256 512)
    install(FILES resources/linux/icons/${size}.png
      DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
      RENAME net.jegor.ljdict.gtk.png)
  endforeach()
endif()

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE ljdict_core)
add_test(NAME core_tests COMMAND core_tests)
