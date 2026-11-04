cmake_minimum_required(VERSION 3.22...3.31)

# Versions: 1.0, 1.1, 1.2, ... ; release tags are v<version>.
project(status-panel VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

configure_file(src/plugin-version.h.in ${CMAKE_CURRENT_BINARY_DIR}/plugin-version.h @ONLY)

add_library(${PROJECT_NAME} MODULE
  src/plugin-main.cpp
  src/status-panel.cpp
  src/status-panel.hpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W3 /utf-8 /Zc:__cplusplus)
endif()

# Per-plugin folder layout used by OBS 28+:
#   <prefix>/status-panel/bin/64bit/status-panel.dll
#   <prefix>/status-panel/data/locale/*.ini
if(WIN32)
  install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${PROJECT_NAME}/bin/64bit)
  install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION ${PROJECT_NAME}/bin/64bit OPTIONAL)
  install(DIRECTORY data/ DESTINATION ${PROJECT_NAME}/data)
else()
  install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION lib/obs-plugins)
  install(DIRECTORY data/ DESTINATION share/obs/obs-plugins/${PROJECT_NAME})
endif()
