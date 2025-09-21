cmake_minimum_required(VERSION 3.20)
project(baresip_node LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

# cmake-js injects these:
include_directories(${CMAKE_JS_INC})
add_definitions(-DNAPI_VERSION=8)

# Where your staged SDK lives (change if your path differs)
set(LOCAL_PREFIX "${CMAKE_SOURCE_DIR}/local" CACHE PATH "SDK root")

# Resolve node-addon-api include dir via Node
execute_process(
  COMMAND node -p "require('node-addon-api').include"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
if(NOT EXISTS "${NODE_ADDON_API_DIR}/napi.h")
  # fallback if execute_process didn’t return quotes/path for some reason
  set(NODE_ADDON_API_DIR "${CMAKE_SOURCE_DIR}/node_modules/node-addon-api")
endif()
message(STATUS "node-addon-api include: ${NODE_ADDON_API_DIR}")

# ---- Target (define FIRST) ----
add_library(baresip_node SHARED
  ${CMAKE_JS_SRC}
  src/addon.cc
)
set_target_properties(baresip_node PROPERTIES PREFIX "" SUFFIX ".node")

# Includes for this target
target_include_directories(baresip_node PRIVATE
  ${NODE_ADDON_API_DIR}
  ${LOCAL_PREFIX}/baresip/include
  ${LOCAL_PREFIX}/re/include
  ${CMAKE_SOURCE_DIR}/src
)

# Link static libs from /local
set(BARESIP_STATIC "${LOCAL_PREFIX}/baresip/lib/libbaresip.a")
set(RE_STATIC      "${LOCAL_PREFIX}/re/lib/libre.a")
if(NOT EXISTS "${BARESIP_STATIC}")
  message(FATAL_ERROR "Missing ${BARESIP_STATIC}")
endif()
if(NOT EXISTS "${RE_STATIC}")
  message(FATAL_ERROR "Missing ${RE_STATIC}")
endif()

target_link_libraries(baresip_node PRIVATE
  "${BARESIP_STATIC}"
  "${RE_STATIC}"
  ${CMAKE_JS_LIB}
)

# macOS frameworks (because you compiled coreaudio module into baresip)
if(APPLE)
  target_link_options(baresip_node PRIVATE
    "-Wl,-undefined,dynamic_lookup"
    "-Wl,-rpath,@loader_path"
    "-Wl,-rpath,@loader_path/vendor")
  target_link_libraries(baresip_node PRIVATE
    "-framework CoreAudio"
    "-framework AudioUnit"
    "-framework AudioToolbox"
    "-framework CoreServices")
endif()

find_library(AOM_DYLIB NAMES aom PATHS "/opt/homebrew/opt/aom/lib" NO_DEFAULT_PATH)
if(AOM_DYLIB)
  target_link_libraries(baresip_node PRIVATE ${AOM_DYLIB})
endif()

# Find video codec dylibs from Homebrew
find_library(AOM_DYLIB NAMES aom PATHS "/opt/homebrew/opt/aom/lib" NO_DEFAULT_PATH)
find_library(VPX_DYLIB NAMES vpx PATHS "/opt/homebrew/opt/libvpx/lib" NO_DEFAULT_PATH)

if(AOM_DYLIB)
  target_link_libraries(baresip_node PRIVATE ${AOM_DYLIB})
endif()
if(VPX_DYLIB)
  target_link_libraries(baresip_node PRIVATE ${VPX_DYLIB})
endif()
