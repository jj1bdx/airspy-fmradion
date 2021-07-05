#!/usr/bin/env python3
# Convert little-endian 32-bit float stereo raw file to
# RF64 (WAV capable to handle 64-bit length) format

import os
import sys

argvs = sys.argv
argc = len(argvs)

if argc == 3:
  rawname = argvs[1]
  wavname = argvs[2]
else:
  print('Usage: ', argvs[0], 'rawfile wavfile', file=sys.stderr)
  quit()

if os.path.exists(wavname):
  print('output file', wavname, 'exists, aborted', file=sys.stderr)
  quit()

# open the raw file
rawfile = open(rawname, 'rb')
rawfile.seek(0, os.SEEK_END)
rawfilesize = rawfile.tell()

if (rawfilesize % 8) != 0:
  print('input file', rawname, 'is not aligned to 8-byte boundary, aborted',
          file=sys.stderr)
  quit()

print("raw filesize =", rawfilesize)

# set raw file seek position to the top
rawfile.seek(0, os.SEEK_SET)

# open the wav file
wavfile = open(wavname, 'wb')

def wavfile_write_string(string):
  wavfile.write(bytes(string, 'ascii'))

def wavfile_write_uint2(uint2):
  wavfile.write(uint2.to_bytes(2, 'little'))

def wavfile_write_uint4(uint4):
  wavfile.write(uint4.to_bytes(4, 'little'))

def wavfile_write_uint8(uint8):
  wavfile.write(uint8.to_bytes(8, 'little'))

# Add RF64 header
wavfile_write_string('RF64')
wavfile_write_uint4(0xffffffff)
wavfile_write_string('WAVE')
# Add ds64 header
wavfile_write_string('ds64')
# size of ds64 header
wavfile_write_uint4(28)
# size of RF64 chunk
wavfile_write_uint8(rawfilesize + 96)
# size of data chunk
wavfile_write_uint8(rawfilesize)
# dummyLow, dummyHigh, tableLength
wavfile_write_uint4(0)
wavfile_write_uint4(0)
wavfile_write_uint4(0)
# Add fmt header
wavfile_write_string('fmt ')
# fmt header size
wavfile_write_uint4(40)
# WAVE_FORMAT_EXTENSIBLE
wavfile_write_uint2(0xfffe)
# channelCount
wavfile_write_uint2(2)
# Sampling rate
wavfile_write_uint4(48000)
# Bytes per second
wavfile_write_uint4(48000 * 8)
# block alignment
wavfile_write_uint2(8)
# bits per sample
wavfile_write_uint2(32)
# extra information length (for WAVE_FORMAT_EXTENSIBLE)
wavfile_write_uint2(22)
# wValidBitsPerSample; /* bits of precision */
wavfile_write_uint2(32)
# dwChannelMask: SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
wavfile_write_uint4(0x00000003)
# MSGUID_SUBTYPE_IEEE_FLOAT
wavfile_write_uint4(0x00000003)
wavfile_write_uint2(0x0000)
wavfile_write_uint2(0x0010)
wavfile_write_uint8(0x719b3800aa000080)
# Add data header
wavfile_write_string('data')
wavfile_write_uint4(0xffffffff)

# Copy binary data as is from rawfile to wavfile
while True:
  chunk = rawfile.read(1048576)
  if not chunk:
    # eof
    break
  wavfile.write(chunk)
  ratio = float(rawfile.tell()) / float(rawfilesize)
  print('Progress: {:.2%}\r'.format(ratio), end='')

print()
rawfile.close()
wavfile.close()
