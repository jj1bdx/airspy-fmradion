#!/usr/bin/env python3
# Simple caller for softfm
import sys
import subprocess
import miniaudio

argvs = sys.argv
argc = len(argvs)

if argc != 2:
    print('Usage: ', argvs[0], '<frequency in kHz>\n')
    quit()
freq = int(float(argvs[1]) * 1000)

channels = 1
sample_rate = 48000
sample_width = 4 # 32bit float

def stream_pcm(source):
    required_frames = yield b""  # generator initialization
    while True:
        required_bytes = required_frames * channels * sample_width
        sample_data = source.read(required_bytes)
        if not sample_data:
            break
        # print(".", end="", flush=True)
        required_frames = yield sample_data

with miniaudio.PlaybackDevice(
        output_format = miniaudio.SampleFormat.FLOAT32,
        nchannels=channels,
        sample_rate=sample_rate) as device:
    fmradion = subprocess.Popen(["airspy-fmradion", "-m", "am", "-b0.001", "-t", "airspyhf", "-c", "freq=" + str(freq) +",srate=192000", "-F", "-"], stdin = None, stdout = subprocess.PIPE)
    stream = stream_pcm(fmradion.stdout)
    next(stream)
    device.start(stream)
    input("Enter to stop playback\n")
    fmradion.terminate()
