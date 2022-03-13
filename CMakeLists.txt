# CMake definitions for airspy-fmradion

cmake_minimum_required(VERSION 3.0.2)
project(airspy-fmradion)

# Workaround for the compilation warning
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

# Use pkg-config
include(FindPkgConfig)

find_package(Threads)

# Find Volk library.
pkg_check_modules(PKG_VOLK volk)
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_library(VOLK_LIBRARY libvolk.dylib
	HINTS /usr/local/lib /opt/homebrew/lib
	      ${PKG_VOLK_LIBRARY_DIRS})
else()
    find_library(VOLK_LIBRARY libvolk.so
	HINTS ${PKG_VOLK_LIBRARY_DIRS})
endif()
message(STATUS "volk library: ${VOLK_LIBRARY}")

# Find Airspy library.
pkg_check_modules(PKG_AIRSPY libairspy)
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_path(AIRSPY_INCLUDE_DIR airspy.h
	    HINTS /usr/local/include/libairspy
	          /opt/homebrew/include/libairspy
	          ${PKG_AIRSPY_INCLUDE_DIRS})
    find_library(AIRSPY_LIBRARY libairspy.a
	    HINTS /usr/local/lib
	          /opt/homebrew/lib
	          ${PKG_AIRSPY_LIBRARY_DIRS})
    set(AIRSPY_INCLUDE_OPTION "-I/usr/local/include -I/opt/homebrew/include")
else()
    find_path(AIRSPY_INCLUDE_DIR airspy.h
	    HINTS ${PKG_AIRSPY_INCLUDE_DIRS})
    find_library(AIRSPY_LIBRARY libairspy.a
	    HINTS ${PKG_AIRSPY_LIBRARY_DIRS})
    set(AIRSPY_INCLUDE_OPTION "")
endif()
message(STATUS "libairspy: ${AIRSPY_INCLUDE_DIR}, ${AIRSPY_LIBRARY}")

# Find Airspy HF library.
pkg_check_modules(PKG_AIRSPYHF libairspyhf)
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_path(AIRSPYHF_INCLUDE_DIR airspyhf.h
	HINTS /usr/local/include/libairspyhf
	      /opt/homebrew/include/libairspyhf
	      ${PKG_AIRSPYHF_INCLUDE_DIRS})
    find_library(AIRSPYHF_LIBRARY libairspyhf.a
	HINTS /usr/local/lib
	      /opt/homebrew/lib
	      ${PKG_AIRSPYHF_LIBRARY_DIRS})
    set(AIRSPYHF_INCLUDE_OPTION "-I/usr/local/include -I/opt/homebrew/include")
else()
    find_path(AIRSPYHF_INCLUDE_DIR airspyhf.h
             HINT ${PKG_AIRSPYHF_INCLUDE_DIRS})
    find_library(AIRSPYHF_LIBRARY libairspyhf.a
             HINT ${PKG_AIRSPYHF_LIBRARY_DIRS})
    set(AIRSPYHF_INCLUDE_OPTION "")
endif()
message(STATUS "libairspyhf: ${AIRSPYHF_INCLUDE_DIR}, ${AIRSPYHF_LIBRARY}")

# Find RTL-SDR library.
pkg_check_modules(PKG_RTLSDR librtlsdr)
find_path(RTLSDR_INCLUDE_DIR rtl-sdr.h
	HINTS ${PKG_RTLSDR_INCLUDE_DIRS})
find_library(RTLSDR_LIBRARY librtlsdr.a
	HINTS ${PKG_RTLSDR_LIBRARY_DIRS})
message(STATUS "librtlsdr: ${RTLSDR_INCLUDE_DIR}, ${RTLSDR_LIBRARY}")

# Find libusb
# See https://github.com/texane/stlink/blob/master/cmake/modules/FindLibUSB.cmake
pkg_check_modules(PKG_LIBUSB libusb-1.0)
find_path(LIBUSB_INCLUDE_DIR libusb.h
    HINTS /usr /usr/local /opt /opt/homebrew ${PKG_LIBUSB_INCLUDE_DIRS}
    PATH_SUFFIXES libusb-1.0)
set(LIBUSB_NAME usb-1.0)
find_library(LIBUSB_LIBRARY ${LIBUSB_NAME}
    HINTS /usr /usr/local /opt /opt/homebrew ${PKG_LIBUSB_LIBRARY_DIRS})
message(STATUS "libusb: ${LIBUSB_INCLUDE_DIR}, ${LIBUSB_LIBRARY}")

# Find libsoxr
pkg_check_modules(PKG_LIBSOXR libsoxr)
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_path(LIBSOXR_INCLUDE_DIR soxr.h
        HINTS /usr/local/include /opt/homebrew/include
	    ${PKG_LIBSOXR_INCLUDE_DIRS})
    find_library(LIBSOXR_LIBRARY libsoxr.dylib
        HINTS /usr/local/lib /opt/homebrew/lib
	    ${PKG_LIBSOXR_LIBRARY_DIRS})
else()
    find_path(LIBSOXR_INCLUDE_DIR soxr.h
        HINTS ${PKG_LIBSOXR_INCLUDE_DIRS})
    find_library(LIBSOXR_LIBRARY libsoxr.so
        HINTS ${PKG_LIBSOXR_LIBRARY_DIRS})
endif()
message(STATUS "libsoxr: ${LIBSOXR_INCLUDE_DIR}, ${LIBSOXR_LIBRARY}")

# Find sndfile library.
FIND_LIBRARY(SNDFILE_LIBRARY sndfile)

FIND_PATH(SNDFILE_INCLUDE_DIR sndfile.h
	HINTS ${PKG_SNDFILE_INCLUDE_DIRS})
MESSAGE(STATUS "Sndfile: ${SNDFILE_LIBRARY}, ${SNDFILE_INCLUDE_DIR}")

# for PortAudio
pkg_check_modules(PORTAUDIO2 portaudio-2.0)
if (PORTAUDIO2_FOUND)
    set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO2_INCLUDE_DIRS})
    set(PORTAUDIO_LIBRARY_DIRS ${PORTAUDIO2_LIBRARY_DIRS})
    set(PORTAUDIO_LINK_LIBRARIES ${PORTAUDIO2_LINK_LIBRARIES})
    set(PORTAUDIO_VERSION 19)
    set(PORTAUDIO_FOUND TRUE)
    find_path(PORTAUDIO_INCLUDE_DIR portaudio.h
        HINTS /usr/local/include /opt/homebrew/include
	    ${PORTAUDIO_INCLUDE_DIRS})
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        find_library(PORTAUDIO_LIBRARY libportaudio.dylib
               HINTS /usr/local/lib /opt/homebrew/lib
	           ${PORTAUDIO_LIBRARY_DIRS})
    else (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        find_library(PORTAUDIO_LIBRARY libportaudio.so
               HINTS /usr/local/lib /opt/homebrew/lib
	           ${PORTAUDIO_LIBRARY_DIRS})
    endif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
else (PORTAUDIO2_FOUND)
    message(FATAL_ERROR "Could not find Portaudio")
endif (PORTAUDIO2_FOUND)
message(STATUS "libportaudio: ${PORTAUDIO_INCLUDE_DIR}, ${PORTAUDIO_LIBRARY}")
message(STATUS "PortAudio libs: ${PORTAUDIO_LINK_LIBRARIES}")

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

set(AIRSPY_INCLUDE_DIRS ${AIRSPY_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(AIRSPY_LIBRARIES    ${AIRSPY_LIBRARY} ${LIBUSB_LIBRARY})
set(AIRSPYHF_INCLUDE_DIRS ${AIRSPYHF_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(AIRSPYHF_LIBRARIES    ${AIRSPYHF_LIBRARY} ${LIBUSB_LIBRARY})

# Optimization flags and options.
##
# Enable speed-based optimization
# Do not apply -ffast-math; it enables -menable-no-nans
# which cancels detection functions of multipath filter abnormality!
set(OPTIMIZATION_FLAGS "-O3 -ftree-vectorize ")
##
# Use conservative options when failed to run
#set(OPTIMIZATION_FLAGS "-O2")
##
# For vectorization analysis (in Clang only)
#set(OPTIMIZATION_FLAGS "-O3 -ftree-vectorize -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize")
##
# For clang profiling with optimization
#set(OPTIMIZATION_FLAGS "-O3 -ftree-vectorize -g -fprofile-instr-generate -fcoverage-mapping")
#SET(CMAKE_EXE_LINKER_FLAGS "-fprofile-instr-generate")
##
# For Sanitizers
# with Thread Sanitizer
#set(OPTIMIZATION_FLAGS "-fsanitize=thread -O3 -ftree-vectorize ")
##

# Common compiler flags and options.
##
set(CMAKE_CXX_FLAGS "-Wall -std=c++17 ${OPTIMIZATION_FLAGS} ${AIRSPY_INCLUDE_OPTION} ${AIRSPYHF_INCLUDE_OPTION} ${EXTRA_FLAGS}")

set(sfmbase_SOURCES
    sfmbase/AfAgc.cpp
    sfmbase/AirspyHFSource.cpp
    sfmbase/AirspySource.cpp
    sfmbase/AmDecode.cpp
    sfmbase/AudioResampler.cpp
    sfmbase/AudioOutput.cpp
    sfmbase/ConfigParser.cpp
    sfmbase/FileSource.cpp
    sfmbase/Filter.cpp
    sfmbase/FilterParameters.cpp
    sfmbase/FineTuner.cpp
    sfmbase/FmDecode.cpp
    sfmbase/IfAgc.cpp
    sfmbase/IfResampler.cpp
    sfmbase/MultipathFilter.cpp
    sfmbase/NbfmDecode.cpp
    sfmbase/PhaseDiscriminator.cpp
    sfmbase/RtlSdrSource.cpp
)

set(sfmbase_HEADERS
    include/AfAgc.h
    include/AirspyHFSource.h
    include/AirspySource.h
    include/AmDecode.h
    include/AudioResampler.h
    include/AudioOutput.h
    include/ConfigParser.h
    include/DataBuffer.h
    include/FileSource.h
    include/Filter.h
    include/FilterParameters.h
    include/FineTuner.h
    include/FmDecode.h
    include/FourthConverterIQ.h
    include/IfAgc.h
    include/IfResampler.h
    include/MovingAverage.h
    include/MultipathFilter.h
    include/NbfmDecode.h
    include/PhaseDiscriminator.h
    include/RtlSdrSource.h
    include/Source.h
    include/SoftFM.h
    include/Utility.h
)

# Base sources

set(sfmbase_SOURCES
    ${sfmbase_SOURCES}
    ${sfmbase_HEADERS}
)

# Libraries

add_library(sfmbase STATIC
    ${sfmbase_SOURCES}
)

# Executable

add_executable(airspy-fmradion
	main.cpp
)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${AIRSPYHF_INCLUDE_DIRS}
    ${AIRSPY_INCLUDE_DIRS}
    ${RTLSDR_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIR}
    ${EXTRA_INCLUDES}
)

target_link_libraries(airspy-fmradion
    sfmbase
    ${CMAKE_THREAD_LIBS_INIT}
    ${PORTAUDIO_LINK_LIBRARIES}
    ${VOLK_LIBRARY}
    ${EXTRA_LIBS}
)

target_link_libraries(sfmbase
    ${LIBSOXR_LIBRARY}
    ${SNDFILE_LIBRARY}
    ${AIRSPY_LIBRARIES}
    ${AIRSPYHF_LIBRARIES}
    ${RTLSDR_LIBRARIES}
)

# Installation

install(TARGETS airspy-fmradion DESTINATION bin)
install(TARGETS sfmbase DESTINATION lib)

# End of CMakeLists.txt
