# CMake definitions for SoftFM

cmake_minimum_required(VERSION 3.0.2)
project(SoftFM)

find_package(Threads)
find_package(PkgConfig)
find_package(ALSA REQUIRED)
find_package(Boost 1.47)

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
find_path(AIRSPY_INCLUDE_DIR airspy.h
          HINT ${PKG_AIRSPY_INCLUDE_DIRS})
find_library(AIRSPY_LIBRARY libairspy.a
             HINT ${PKG_AIRSPY_LIBRARY_DIRS})

# Find BladeRF library.
pkg_check_modules(PKG_BLADERF libbladerf)
find_path(BLADERF_INCLUDE_DIR libbladeRF.h
          HINT ${PKG_BLADERF_INCLUDE_DIRS})
find_library(BLADERF_LIBRARY libbladeRF.so
             HINT ${PKG_BLADERF_LIBRARY_DIRS})

# Find libusb
pkg_check_modules(PKG_LIBUSB libusb-1.0)
find_path(LIBUSB_INCLUDE_DIR libusb.h
          HINT ${PKG_LIBUSB_INCLUDE_DIRS}
          PATH_SUFFIXES libusb-1.0)
find_library(LIBUSB_LIBRARY usb-1.0
             HINT ${PKG_LIBUSB_LIBRARY_DIRS})

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

set(BLADERF_INCLUDE_DIRS ${BLADERF_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(BLADERF_LIBRARIES    ${BLADERF_LIBRARY} ${LIBUSB_LIBRARY})

# Compiler flags.
set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -O2 -ffast-math -ftree-vectorize ${EXTRA_FLAGS}")

set(sfmbase_SOURCES
    sfmbase/Filter.cpp
    sfmbase/FmDecode.cpp
    sfmbase/AudioOutput.cpp 
)

set(sfmbase_HEADERS
    include/AudioOutput.h
    include/Filter.h
    include/FmDecode.h
    include/MovingAverage.h
    include/Source.h
    include/SoftFM.h
    include/DataBuffer.h
    include/fastatan2.h
    include/parsekv.h
    include/util.h
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

# BLadeRF sources

set(sfmbladerf_SOURCES
    sfmbase/BladeRFSource.cpp
)

set(sfmbladerf_HEADERS
    include/BladeRFSource.h
)

set(sfmbladerf_SOURCES
    ${sfmbladerf_SOURCES}
    ${sfmbladerf_HEADERS}
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

add_library(sfmbladerf STATIC
    ${sfmbladerf_SOURCES}
)

add_executable(softfm
	main.cpp
)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${ALSA_INCLUDE_DIRS}
    ${EXTRA_INCLUDES} 
)

target_link_libraries(softfm
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

target_link_libraries(sfmbladerf
    ${BLADERF_LIBRARIES}
)

install(TARGETS softfm DESTINATION bin)
install(TARGETS sfmbase sfmrtlsdr sfmhackrf sfmairspy sfmbladerf DESTINATION lib)
