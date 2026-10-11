cmake_minimum_required(VERSION 3.16)
project(buster_cmake_abi_discovery C)
find_library(DISCOVERY_Z_LIB NAMES z)
find_library(DISCOVERY_X11_LIB NAMES X11)
file(WRITE "${CMAKE_BINARY_DIR}/discovery.txt"
"cmake_version=${CMAKE_VERSION}
compiler_id=${CMAKE_C_COMPILER_ID}
sizeof_data_ptr=${CMAKE_C_SIZEOF_DATA_PTR}
library_architecture=${CMAKE_C_LIBRARY_ARCHITECTURE}
implicit_link_directories=${CMAKE_C_IMPLICIT_LINK_DIRECTORIES}
implicit_link_libraries=${CMAKE_C_IMPLICIT_LINK_LIBRARIES}
compiler_linker=${CMAKE_C_COMPILER_LINKER}
compiler_linker_id=${CMAKE_C_COMPILER_LINKER_ID}
z=${DISCOVERY_Z_LIB}
x11=${DISCOVERY_X11_LIB}
")
