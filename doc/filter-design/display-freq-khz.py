#!/usr/bin/env python3
from scipy import signal
import matplotlib.pyplot as plt
import numpy as np
import sys

argv = sys.argv
argc = len(argv)
if (argc != 3):
  print("Usage: %s maxfreq-in-kHz filename" % argv[0])
  quit()

maxfreq = float(argv[1])
coeff_list = []
file = open(argv[2], "r")
for line in file:
  s = line.strip(' \r\n')
  if (s != ""):
    coeff_list.append(float(s))

print(coeff_list)

w, h = signal.freqz(coeff_list)
fig, ax1 = plt.subplots()
fs = maxfreq / (np.pi * 2.0)
ax1.set_title('Digital filter frequency response')
ax1.grid(True)
ax1.plot(w * fs, 20 * np.log10(abs(h)), 'b')
ax1.set_ylabel('Amplitude [dB]', color='b')
ax1.set_xlabel('Frequency [Hz]')
plt.show()
