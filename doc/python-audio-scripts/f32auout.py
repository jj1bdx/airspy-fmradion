#!/usr/bin/env python3
# Pyaudio output device for 32-bit floating audio input from stdin
import pyaudio
import signal
import sys
import time

argvs = sys.argv
argc = len(argvs)

if argc == 2:
  channels = int(argvs[1])
  devidx = None
elif argc == 3:
  channels = int(argvs[1])
  devidx = int(argvs[2])
else:
  print('Usage: ', argvs[0], 'channels [device-index]\n')
  quit()
channels = int(argvs[1])
sample_rate = 48000
sample_width = 4 # 32bit float

def terminate():
    stream.stop_stream()
    stream.close()
    p.terminate()

def signal_handler(signal, frame):
    print('Terminated by CTRL/C')
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)

framesize = 1024 # 10msec
readsize = framesize * sample_width * channels

p = pyaudio.PyAudio()

stream = p.open(format = pyaudio.paFloat32,
                channels = channels,
                rate = sample_rate,
                output = True,
                output_device_index = devidx,
                frames_per_buffer = framesize
                )

data = sys.stdin.buffer.read(readsize)
while data != '':
    stream.write(data)
    data = sys.stdin.buffer.read(readsize)

terminate()
