# CMake definitions for SoftFM

cmake_minimum_required(VERSION 3.0.2)
project(NGSoftFM)

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

# Find RTL-SDR library.
pkg_check_modules(PKG_RTLSDR librtlsdr)
find_path(RTLSDR_INCLUDE_DIR rtl-sdr.h
          HINT ${PKG_RTLSDR_INCLUDE_DIRS})
find_library(RTLSDR_LIBRARY librtlsdr.a
             HINT ${PKG_RTLSDR_LIBRARY_DIRS})

# Find HackRF library.
pkg_check_modules(PKG_HACKRF libhackrf)
find_path(HACKRF_INCLUDE_DIR hackrf.h
          HINT ${PKG_HACKRF_INCLUDE_DIRS})
find_library(HACKRF_LIBRARY libhackrf.a
             HINT ${PKG_HACKRF_LIBRARY_DIRS})

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

if(RTLSDR_INCLUDE_DIR AND RTLSDR_LIBRARY)
    message(STATUS "Found librtlsdr: ${RTLSDR_INCLUDE_DIR}, ${RTLSDR_LIBRARY}")
else()
    message(WARNING "Can not find Osmocom RTL-SDR library")
    message("Try again with environment variable PKG_CONFIG_PATH")
    message("or with -DRTLSDR_INCLUDE_DIR=/path/rtlsdr/include")
    message("        -DRTLSDR_LIBRARY=/path/rtlsdr/lib/librtlsdr.a")
endif()

set(RTLSDR_INCLUDE_DIRS ${RTLSDR_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(RTLSDR_LIBRARIES    ${RTLSDR_LIBRARY} ${LIBUSB_LIBRARY})

set(HACKRF_INCLUDE_DIRS ${HACKRF_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(HACKRF_LIBRARIES    ${HACKRF_LIBRARY} ${LIBUSB_LIBRARY})

set(AIRSPY_INCLUDE_DIRS ${AIRSPY_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(AIRSPY_LIBRARIES    ${AIRSPY_LIBRARY} ${LIBUSB_LIBRARY})

# Compiler flags.
# Enable speed-based optimization
set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -O3 -ffast-math -ftree-vectorize -march=native ${ALSA_OPTION} ${AIRSPY_INCLUDE_OPTION} ${EXTRA_FLAGS}")
# For vectorization analisys (in Clang only)
#set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -O3 -ffast-math -ftree-vectorize -march=native -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize ${ALSA_OPTION} ${AIRSPY_INCLUDE_OPTION} ${EXTRA_FLAGS}")
# Use conservative options when failed to run
#set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -O2 ${ALSA_OPTION} ${AIRSPY_INCLUDE_OPTION} ${EXTRA_FLAGS}")
# For clang profiling
# set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -g -fprofile-instr-generate -fcoverage-mapping ${ALSA_OPTION} ${AIRSPY_INCLUDE_OPTION} ${EXTRA_FLAGS}")
# SET(CMAKE_EXE_LINKER_FLAGS "-fprofile-instr-generate")

set(sfmbase_SOURCES
    sfmbase/Filter.cpp
    sfmbase/FmDecode.cpp
    sfmbase/AudioOutput.cpp 
    sfmbase/EqParameters.cpp
)

set(sfmbase_HEADERS
    include/AudioOutput.h
    include/Filter.h
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

# RTL-SDR sources

set(sfmrtlsdr_SOURCES
    sfmbase/RtlSdrSource.cpp
)

set(sfmrtlsdr_HEADERS
    include/RtlSdrSource.h
)

set(sfmrtlsdr_SOURCES
    ${sfmrtlsdr_SOURCES}
    ${sfmrtlsdr_HEADERS}
)

# HackRF sources

set(sfmhackrf_SOURCES
    sfmbase/HackRFSource.cpp
)

set(sfmhackrf_HEADERS
    include/HackRFSource.h
)

set(sfmhackrf_SOURCES
    ${sfmhackrf_SOURCES}
    ${sfmhackrf_HEADERS}
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

add_library(sfmrtlsdr STATIC
    ${sfmrtlsdr_SOURCES}
)

add_library(sfmhackrf STATIC
    ${sfmhackrf_SOURCES}
)

add_library(sfmairspy STATIC
    ${sfmairspy_SOURCES}
)

add_executable(ngsoftfm
	main.cpp
)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${ALSA_INCLUDE_DIRS}
    ${EXTRA_INCLUDES} 
)

target_link_libraries(ngsoftfm
    sfmbase
    sfmrtlsdr
    sfmhackrf
    sfmairspy
    ${CMAKE_THREAD_LIBS_INIT}
    ${ALSA_LIBRARIES}
    ${EXTRA_LIBS} 
)

target_include_directories(sfmrtlsdr PUBLIC
    ${RTLSDR_INCLUDE_DIRS}
)

target_link_libraries(sfmrtlsdr
    ${RTLSDR_LIBRARIES}
)

target_link_libraries(sfmhackrf
    ${HACKRF_LIBRARIES}
)

target_link_libraries(sfmairspy
    ${AIRSPY_LIBRARIES}
)

install(TARGETS ngsoftfm DESTINATION bin)
install(TARGETS sfmbase sfmrtlsdr sfmhackrf sfmairspy DESTINATION lib)
