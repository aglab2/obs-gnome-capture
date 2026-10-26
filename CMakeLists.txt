cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

find_package(Gio 2.76 REQUIRED)
find_package(GStreamer REQUIRED COMPONENTS video)
find_package(JsonGlib REQUIRED)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs gio::gio ${GSTREAMER_LIBRARIES} ${GSTREAMER_BASE_LIBRARIES} ${GSTREAMER_VIDEO_LIBRARIES} ${GSTREAMER_APP_LIBRARIES} ${JSON_GLIB_LIBRARIES})

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_qt(COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/gnome-capture.cpp)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${JSON_GLIB_INCLUDE_DIRS})

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

add_executable(mutter_inject src/mutter_inject.cpp)

add_library(mutter_list_windows SHARED src/mutter_list_windows.c)
target_link_libraries(mutter_list_windows PRIVATE gio::gio)
set_target_properties(mutter_list_windows PROPERTIES
    OUTPUT_NAME mutter_list_windows
    POSITION_INDEPENDENT_CODE ON
)

install(
  TARGETS mutter_inject
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  TARGETS mutter_list_windows
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
