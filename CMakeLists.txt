# CMake definitions for airspy-fmradion

cmake_minimum_required(VERSION 3.18)
project(airspy-fmradion)

# Workaround for the compilation warning
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

# Enable compile_commands.json as default
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use cmake-git-version-tracking
include(FetchContent)
FetchContent_Declare(
  cmake_git_version_tracking
  GIT_REPOSITORY
    https://github.com/jj1bdx/cmake-git-version-tracking.git
  GIT_TAG 6c0cb87edd029ddfb403a8e24577c144a03605a6)
FetchContent_MakeAvailable(cmake_git_version_tracking)

# cmake-format: off
# Use local {fmt} 12.1.0
# By default, fmt builds the static library only
# cmake-format: on
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG 407c905e45ad75fc29bf0f9bb7c5c2fd3475976f)
FetchContent_MakeAvailable(fmt)

# EXPORT_COMPILE_COMMANDS is supported for CMake version 3.20 or later
# only
if(CMAKE_VERSION VERSION_LESS 3.20.0)
  message(STATUS "No EXPORT_COMPILE_COMMANDS available")
  message(STATUS "Use compdb for proper compile_commands.json handling")
else()
  set_target_properties(cmake_git_version_tracking
                        PROPERTIES EXPORT_COMPILE_COMMANDS OFF)
endif()

# Use pkg-config
include(FindPkgConfig)

# Find Threads package
find_package(Threads REQUIRED)

# Find Volk library.
pkg_check_modules(PKG_VOLK volk REQUIRED)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  find_library(
    VOLK_LIBRARY libvolk.dylib HINTS /usr/local/lib /opt/homebrew/lib
                                     ${PKG_VOLK_LIBRARY_DIRS})
else()
  find_library(VOLK_LIBRARY libvolk.so HINTS ${PKG_VOLK_LIBRARY_DIRS})
endif()
message(STATUS "volk ${PKG_VOLK_VERSION}: ${VOLK_LIBRARY}")

# Find Airspy library.
pkg_check_modules(PKG_AIRSPY libairspy REQUIRED)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  find_path(
    AIRSPY_INCLUDE_DIR airspy.h
    HINTS /usr/local/include/libairspy /opt/homebrew/include/libairspy
          ${PKG_AIRSPY_INCLUDE_DIRS})
  find_library(
    AIRSPY_LIBRARY libairspy.dylib
    HINTS /usr/local/lib /opt/homebrew/lib ${PKG_AIRSPY_LIBRARY_DIRS})
  set(AIRSPY_INCLUDE_OPTION
      "-I/usr/local/include -I/opt/homebrew/include")
else()
  find_path(AIRSPY_INCLUDE_DIR airspy.h
            HINTS ${PKG_AIRSPY_INCLUDE_DIRS})
  find_library(AIRSPY_LIBRARY libairspy.so
               HINTS ${PKG_AIRSPY_LIBRARY_DIRS})
  set(AIRSPY_INCLUDE_OPTION "")
endif()
message(
  STATUS
    "libairspy ${PKG_AIRSPY_VERSION}: ${AIRSPY_INCLUDE_DIR}, ${AIRSPY_LIBRARY}"
)

# Find Airspy HF library.
pkg_check_modules(PKG_AIRSPYHF libairspyhf REQUIRED)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  find_path(
    AIRSPYHF_INCLUDE_DIR airspyhf.h
    HINTS /usr/local/include/libairspyhf
          /opt/homebrew/include/libairspyhf
          ${PKG_AIRSPYHF_INCLUDE_DIRS})
  find_library(
    AIRSPYHF_LIBRARY libairspyhf.dylib
    HINTS /usr/local/lib /opt/homebrew/lib ${PKG_AIRSPYHF_LIBRARY_DIRS})
  set(AIRSPYHF_INCLUDE_OPTION
      "-I/usr/local/include -I/opt/homebrew/include")
else()
  find_path(AIRSPYHF_INCLUDE_DIR airspyhf.h
            HINTS ${PKG_AIRSPYHF_INCLUDE_DIRS})
  find_library(AIRSPYHF_LIBRARY libairspyhf.so
               HINTS ${PKG_AIRSPYHF_LIBRARY_DIRS})
  set(AIRSPYHF_INCLUDE_OPTION "")
endif()
message(
  STATUS
    "libairspyhf ${PKG_AIRSPYHF_VERSION}: ${AIRSPYHF_INCLUDE_DIR}, ${AIRSPYHF_LIBRARY}"
)

# Find RTL-SDR library.
pkg_check_modules(PKG_RTLSDR librtlsdr REQUIRED)
find_path(RTLSDR_INCLUDE_DIR rtl-sdr.h HINTS ${PKG_RTLSDR_INCLUDE_DIRS})
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  find_library(RTLSDR_LIBRARY librtlsdr.dylib
               HINTS ${PKG_RTLSDR_LIBRARY_DIRS})
else()
  find_library(RTLSDR_LIBRARY librtlsdr.so
               HINTS ${PKG_RTLSDR_LIBRARY_DIRS})
endif()
message(
  STATUS
    "librtlsdr ${PKG_RTLSDR_VERSION}: ${RTLSDR_INCLUDE_DIR}, ${RTLSDR_LIBRARY}"
)

# Find libusb See
# https://github.com/texane/stlink/blob/master/cmake/modules/FindLibUSB.cmake
pkg_check_modules(PKG_LIBUSB libusb-1.0 REQUIRED)
find_path(
  LIBUSB_INCLUDE_DIR libusb.h
  HINTS /usr /usr/local /opt /opt/homebrew ${PKG_LIBUSB_INCLUDE_DIRS}
  PATH_SUFFIXES libusb-1.0)
set(LIBUSB_NAME usb-1.0)
find_library(
  LIBUSB_LIBRARY ${LIBUSB_NAME}
  HINTS /usr /usr/local /opt /opt/homebrew ${PKG_LIBUSB_LIBRARY_DIRS})
message(
  STATUS
    "libusb ${PKG_LIBUSB_VERSION}: ${LIBUSB_INCLUDE_DIR}, ${LIBUSB_LIBRARY}"
)

# Find sndfile library
pkg_check_modules(PKG_SNDFILE sndfile REQUIRED)
find_path(SNDFILE_INCLUDE_DIR sndfile.h
          HINTS ${PKG_SNDFILE_INCLUDE_DIRS})
find_library(SNDFILE_LIBRARY sndfile)
message(
  STATUS
    "sndfile ${PKG_SNDFILE_VERSION}: ${SNDFILE_LIBRARY}, ${SNDFILE_INCLUDE_DIR}"
)
# Determine MP3 support on sndfile
if(PKG_SNDFILE_VERSION VERSION_GREATER_EQUAL "1.1")
  set(SNDFILE_INCLUDE_OPTION "-DLIBSNDFILE_MP3_ENABLED")
  message(STATUS "sndfile MP3 support enabled")
endif()

# for PortAudio
pkg_check_modules(PORTAUDIO2 portaudio-2.0 REQUIRED)
if(PORTAUDIO2_FOUND)
  set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO2_INCLUDE_DIRS})
  set(PORTAUDIO_LIBRARY_DIRS ${PORTAUDIO2_LIBRARY_DIRS})
  set(PORTAUDIO_LINK_LIBRARIES ${PORTAUDIO2_LINK_LIBRARIES})
  set(PORTAUDIO_VERSION 19)
  set(PORTAUDIO_FOUND TRUE)
  find_path(PORTAUDIO_INCLUDE_DIR portaudio.h
            HINTS /usr/local/include /opt/homebrew/include
                  ${PORTAUDIO_INCLUDE_DIRS})
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_library(
      PORTAUDIO_LIBRARY libportaudio.dylib
      HINTS /usr/local/lib /opt/homebrew/lib ${PORTAUDIO_LIBRARY_DIRS})
  else(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_library(
      PORTAUDIO_LIBRARY libportaudio.so
      HINTS /usr/local/lib /opt/homebrew/lib ${PORTAUDIO_LIBRARY_DIRS})
  endif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
else(PORTAUDIO2_FOUND)
  message(FATAL_ERROR "Could not find Portaudio")
endif(PORTAUDIO2_FOUND)
message(
  STATUS
    "libportaudio ${PORTAUDIO2_VERSION}: ${PORTAUDIO_INCLUDE_DIR}, ${PORTAUDIO_LIBRARY}"
)
message(STATUS "PortAudio libs: ${PORTAUDIO_LINK_LIBRARIES}")

set(RTLSDR_INCLUDE_DIRS ${RTLSDR_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(RTLSDR_LIBRARIES ${RTLSDR_LIBRARY} ${LIBUSB_LIBRARY})
set(AIRSPY_INCLUDE_DIRS ${AIRSPY_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(AIRSPY_LIBRARIES ${AIRSPY_LIBRARY} ${LIBUSB_LIBRARY})
set(AIRSPYHF_INCLUDE_DIRS ${AIRSPYHF_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(AIRSPYHF_LIBRARIES ${AIRSPYHF_LIBRARY} ${LIBUSB_LIBRARY})

# cmake-format: off
##
# Optimization flags and options.
#
# Enable speed-based optimization
# Do not apply -ffast-math; it enables -menable-no-nans
# which cancels detection functions of multipath filter abnormality!
#
set(OPTIMIZATION_FLAGS "-O3 -ftree-vectorize ")
##
# Use conservative options when failed to run
#
#set(OPTIMIZATION_FLAGS "-O2")
##
# For vectorization analysis (in Clang only)
#
#set(OPTIMIZATION_FLAGS "-O3 -ftree-vectorize -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize")
##
# For clang profiling with optimization
#
#set(OPTIMIZATION_FLAGS "-O3 -ftree-vectorize -g -fprofile-instr-generate -fcoverage-mapping")
#SET(CMAKE_EXE_LINKER_FLAGS "-fprofile-instr-generate")
##
# For Sanitizers
# with Thread Sanitizer
#
#set(OPTIMIZATION_FLAGS "-fsanitize=thread -O3 -ftree-vectorize ")
# with Address Sanitizer
#
#set(OPTIMIZATION_FLAGS "-fsanitize=address -O3 -ftree-vectorize ")
# For valgrind check
#
#set(OPTIMIZATION_FLAGS "-g")
#
##
# cmake-format: on

# Common compiler flags and options.
#
set(CMAKE_CXX_FLAGS
    "-Wall -std=c++20 ${OPTIMIZATION_FLAGS} ${AIRSPY_INCLUDE_OPTION} ${AIRSPYHF_INCLUDE_OPTION} ${SNDFILE_INCLUDE_OPTION} ${EXTRA_FLAGS}"
)

# For building airspy-fmradion sources

set(sfmbase_SOURCES
    sfmbase/AfSimpleAgc.cpp
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
    sfmbase/IfResampler.cpp
    sfmbase/IfSimpleAgc.cpp
    sfmbase/MultipathFilter.cpp
    sfmbase/NbfmDecode.cpp
    sfmbase/PhaseDiscriminator.cpp
    sfmbase/PilotPhaseLock.cpp
    sfmbase/RtlSdrSource.cpp)

set(sfmbase_HEADERS
    include/AfSimpleAgc.h
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
    include/git.h
    include/IfResampler.h
    include/IfSimpleAgc.h
    include/MovingAverage.h
    include/MultipathFilter.h
    include/NbfmDecode.h
    include/PhaseDiscriminator.h
    include/PilotPhaseLock.h
    include/RtlSdrSource.h
    include/Source.h
    include/SoftFM.h
    include/Utility.h)

# cmake-format: off
# For building r8brain-free-src
# See https://github.com/baconpaul/SampleRateComparison for the details
# (Licensed GPLv3)
# Use R8B_PFFFT_DOUBLE for double-precision conversion
# cmake-format: on

add_library(r8b r8brain-free-src/r8bbase.cpp
                r8brain-free-src/pffft_double/pffft_double.c)
target_include_directories(r8b PUBLIC r8brain-free-src)
target_compile_definitions(r8b PUBLIC R8B_EXTFFT=1 R8B_FASTTIMING=1
                                      R8B_PFFFT_DOUBLE=1)

# Base sources

set(sfmbase_SOURCES ${sfmbase_SOURCES} ${sfmbase_HEADERS})

# Libraries

add_library(sfmbase STATIC ${sfmbase_SOURCES})

# Executable

add_executable(airspy-fmradion main.cpp)

include_directories(
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/r8brain-free-src
  ${fmt_SOURCE_DIR}/include
  ${AIRSPYHF_INCLUDE_DIRS}
  ${AIRSPY_INCLUDE_DIRS}
  ${RTLSDR_INCLUDE_DIRS}
  ${PORTAUDIO_INCLUDE_DIR}
  ${EXTRA_INCLUDES})

target_link_libraries(
  airspy-fmradion
  fmt::fmt
  sfmbase
  r8b
  Threads::Threads
  ${PORTAUDIO_LINK_LIBRARIES}
  ${VOLK_LIBRARY}
  ${EXTRA_LIBS}
  cmake_git_version_tracking)

target_link_libraries(
  sfmbase ${SNDFILE_LIBRARY} ${AIRSPY_LIBRARY} ${AIRSPYHF_LIBRARY}
  ${RTLSDR_LIBRARY} ${LIBUSB_LIBRARY})

# Installation

install(TARGETS airspy-fmradion DESTINATION bin)
install(TARGETS sfmbase DESTINATION lib)

# End of CMakeLists.txt
