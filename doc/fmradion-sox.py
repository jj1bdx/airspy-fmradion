#!/usr/bin/env python3
# Simple caller for softfm
import sys
import subprocess
import signal

argvs = sys.argv
argc = len(argvs)

def signal_handler(signal, frame):
    print('Terminated by CTRL/C')
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)

if argc != 2:
    print('Usage: ', argvs[0], '<frequency in MHz>\n')
    quit()
freq = int(float(argvs[1]) * 1000000)
command = "airspy-fmradion -q -c freq=" + str(freq) + \
           ",srate=10000000,lgain=3,mgain=0,vgain=10 -r 48000 -b 1.0 -R - | " + \
          "play -t raw -esigned-integer -b16 -r 48000 -c 2 -q -"
print("command =", command)
subprocess.run(command, shell=True, check=True)
