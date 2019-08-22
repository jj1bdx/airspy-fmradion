#!/usr/bin/env python3

import csv
import sys
import cmath
import math
import matplotlib.pyplot as plt
import matplotlib.mlab as mlab
import matplotlib.gridspec as gridspec

filename = sys.argv[1]
stages = 72
elements = (stages * 2) + 1 

fig = plt.figure(constrained_layout=True)

with open(filename, newline='') as f:
  reader = csv.reader(f)
  for row in reader:
    block = int(row[1])
    error = float(row[3])
    smallrow = row[5:]
    coeff = dict()

    for j in range(0, elements - 1):
      k = j * 3;
      pos = int(smallrow[k])
      real = float(smallrow[k + 1])
      imag = float(smallrow[k + 2])
      coeff[pos] = complex(real, imag)

    coeff_am = []
    coeff_ph = []
    for j in range(0, elements - 1):
      z = coeff[j]
      coeff_am.append(abs(z))
      coeff_ph.append(cmath.phase(z))

    figfilename = "./png/" + filename + ".block.polar." + str(block) + ".png"

    axis_x = range(-stages, stages)
    title_re = str("block " + str(block))

    fig.clear()
    gs = gridspec.GridSpec(2, 1, figure=fig)
    pi = math.pi

    ax1 = fig.add_subplot(gs[0, 0])
    plt.ylim(0, 1.2)
    plt.yscale('symlog', linthreshy = 0.001)
    ax1.bar(axis_x, coeff_am)
    ax1.set_xlabel('position')
    ax1.set_ylabel('amplitude')
    plt.title(title_re)
    plt.grid(True)

    ax2 = fig.add_subplot(gs[1, 0])
    plt.ylim(-pi, pi)
    plt.yscale('linear')
    ax2.bar(axis_x, coeff_ph)
    ax2.set_xlabel('position')
    ax2.set_ylabel('phase [rad]')
    ax2.set_yticks([-pi, -pi/2, 0, pi/2, pi])
    plt.grid(True)
        
    plt.savefig(figfilename)

    print("file " + figfilename + " created")
