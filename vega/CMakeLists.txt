cmake_minimum_required(VERSION 3.19)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(EmberCore
        VERSION 1.0
        LANGUAGES CXX C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_link_options("LINKER:--no-undefined" "LINKER:-z,defs")
endif()

# The platform-neutral C core shared with the Android build.
set(EMBER_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

set(HEADERS
  kepler/turbo-modules/generated/EmberCoreSpec.h
  kepler/turbo-modules/EmberCore.h
  ${EMBER_CORE}/ember.h
  ${EMBER_CORE}/model.h
)
set(SOURCES
  kepler/turbo-modules/generated/EmberCoreSpec.cpp
  kepler/turbo-modules/EmberCore.cpp
  kepler/AutoLinkInit.cpp
  ${EMBER_CORE}/ember.c
)

find_package(turbomoduleAPI CONFIG REQUIRED)

kepler_add_turbo_module_library(EmberCore SHARED ${HEADERS} ${SOURCES})
target_include_directories(EmberCore PRIVATE ${EMBER_CORE})

install(TARGETS EmberCore)
