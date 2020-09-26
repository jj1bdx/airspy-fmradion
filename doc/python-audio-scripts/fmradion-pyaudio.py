#!/usr/bin/env python3
# Simple caller for softfm
import pyaudio
import signal
import subprocess
import sys
import time

argvs = sys.argv
argc = len(argvs)

if argc != 2:
    print('Usage: ', argvs[0], '<frequency in MHz>\n')
    quit()
freq = int(float(argvs[1]) * 1000000)

chunk = 128 
channels = 2
sample_rate = 48000
sample_width = 4 # 32bit float

def terminate():
    stream.stop_stream()
    stream.close()
    fmradion.stdout.close()
    p.terminate()

def signal_handler(signal, frame):
    print('Terminated by CTRL/C')
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)

def callback(in_data, frame_count, time_info, status):
    data = fmradion.stdout.read(frame_count * sample_width * channels)
    return (data, pyaudio.paContinue)

fmradion = subprocess.Popen(["airspy-fmradion", "-E100", "-t", "airspyhf", "-b0.001", "-q", "-c", "freq=" + str(freq), "-F", "-"], stdin = None, stdout = subprocess.PIPE)

p = pyaudio.PyAudio()

stream = p.open(format = pyaudio.paFloat32,
                channels = channels,
                rate = sample_rate,
                output = True,
                stream_callback = callback)

stream.start_stream()

while stream.is_active():
    time.sleep(0.1)

terminate()
