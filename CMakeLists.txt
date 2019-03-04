# CMake definitions for SoftFM

cmake_minimum_required(VERSION 3.0.2)
project(airspy-fmradion)

# Workaround for the compilation warning
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

find_package(Threads)
find_package(PkgConfig)
find_package(Boost 1.47)

# Require ALSA only for Linux
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ALSA REQUIRED)
endif()

# Find Airspy library.
pkg_check_modules(PKG_AIRSPY libairspy)
if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    find_path(AIRSPY_INCLUDE_DIR airspy.h
             HINT /usr/local/include/libairspy ${PKG_AIRSPY_INCLUDE_DIRS})
    find_library(AIRSPY_LIBRARY libairspy.so
             HINT /usr/local/lib ${PKG_AIRSPY_LIBRARY_DIRS})
    set(AIRSPY_INCLUDE_OPTION "-I/usr/local/include")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_path(AIRSPY_INCLUDE_DIR airspy.h
             HINT /usr/local/include/libairspy ${PKG_AIRSPY_INCLUDE_DIRS})
    find_library(AIRSPY_LIBRARY libairspy.a
             HINT /usr/local/lib ${PKG_AIRSPY_LIBRARY_DIRS})
    set(AIRSPY_INCLUDE_OPTION "-I/usr/local/include")
else()
    find_path(AIRSPY_INCLUDE_DIR airspy.h
             HINT ${PKG_AIRSPY_INCLUDE_DIRS})
    find_library(AIRSPY_LIBRARY libairspy.a
             HINT ${PKG_AIRSPY_LIBRARY_DIRS})
    set(AIRSPY_INCLUDE_OPTION "")
endif()

# Find libusb
# See https://github.com/texane/stlink/blob/master/cmake/modules/FindLibUSB.cmake
pkg_check_modules(PKG_LIBUSB libusb-1.0)
if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    find_path(LIBUSB_INCLUDE_DIR libusb.h
        HINTS /usr/include ${PKG_LIBUSB_INCLUDE_DIRS}
    )
else ()
    find_path(LIBUSB_INCLUDE_DIR libusb.h
        HINTS /usr /usr/local /opt ${PKG_LIBUSB_INCLUDE_DIRS}
        PATH_SUFFIXES libusb-1.0
    )
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(LIBUSB_NAME usb)
else ()
    set(LIBUSB_NAME usb-1.0)
endif()
find_library(LIBUSB_LIBRARY ${LIBUSB_NAME}
             HINTS /usr /usr/local /opt ${PKG_LIBUSB_LIBRARY_DIRS})

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ALSA_OPTION "-DUSE_ALSA")
else ()
    set(ALSA_OPTION "")
endif()

set(AIRSPY_INCLUDE_DIRS ${AIRSPY_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(AIRSPY_LIBRARIES    ${AIRSPY_LIBRARY} ${LIBUSB_LIBRARY})

# Compiler flags and options.
##
# Enable speed-based optimization
set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -O3 -ffast-math -ftree-vectorize -march=native ${ALSA_OPTION} ${AIRSPY_INCLUDE_OPTION} ${EXTRA_FLAGS}")
##
# Use conservative options when failed to run
#set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -O2 ${ALSA_OPTION} ${AIRSPY_INCLUDE_OPTION} ${EXTRA_FLAGS}")
##
# For vectorization analysis (in Clang only)
#set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -O3 -ffast-math -ftree-vectorize -march=native -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize ${ALSA_OPTION} ${AIRSPY_INCLUDE_OPTION} ${EXTRA_FLAGS}")
##
# For clang profiling
# set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -g -fprofile-instr-generate -fcoverage-mapping ${ALSA_OPTION} ${AIRSPY_INCLUDE_OPTION} ${EXTRA_FLAGS}")
# SET(CMAKE_EXE_LINKER_FLAGS "-fprofile-instr-generate")
##
# For clang profiling with optimization
#set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -O3 -ffast-math -ftree-vectorize -march=native -g -fprofile-instr-generate -fcoverage-mapping ${ALSA_OPTION} ${AIRSPY_INCLUDE_OPTION} ${EXTRA_FLAGS}")
#SET(CMAKE_EXE_LINKER_FLAGS "-fprofile-instr-generate")

set(sfmbase_SOURCES
    sfmbase/Filter.cpp
    sfmbase/FmDecode.cpp
    sfmbase/AudioOutput.cpp 
    sfmbase/EqParameters.cpp
)

set(sfmbase_HEADERS
    include/AudioOutput.h
    include/Filter.h
    include/FilterParameters.h
    include/FmDecode.h
    include/MovingAverage.h
    include/Source.h
    include/SoftFM.h
    include/DataBuffer.h
    include/parsekv.h
    include/util.h
    include/EqParameters.h
)

# Base sources

set(sfmbase_SOURCES
    ${sfmbase_SOURCES}
    ${sfmbase_HEADERS}
)

# Airspy sources

set(sfmairspy_SOURCES
    sfmbase/AirspySource.cpp
)

set(sfmairspy_HEADERS
    include/AirspySource.h
)

set(sfmairspy_SOURCES
    ${sfmairspy_SOURCES}
    ${sfmairspy_HEADERS}
)

# Libraries

add_library(sfmbase STATIC
    ${sfmbase_SOURCES}
)

add_library(sfmairspy STATIC
    ${sfmairspy_SOURCES}
)

add_executable(airspy-fmradion
	main.cpp
)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${ALSA_INCLUDE_DIRS}
    ${EXTRA_INCLUDES} 
)

target_link_libraries(airspy-fmradion
    sfmbase
    sfmairspy
    ${CMAKE_THREAD_LIBS_INIT}
    ${ALSA_LIBRARIES}
    ${EXTRA_LIBS} 
)

target_link_libraries(sfmairspy
    ${AIRSPY_LIBRARIES}
)

install(TARGETS airspy-fmradion DESTINATION bin)
install(TARGETS sfmbase sfmairspy DESTINATION lib)
