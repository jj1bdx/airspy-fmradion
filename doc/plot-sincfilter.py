#!/usr/bin/env python3
import matplotlib.pyplot as plt
import csv

freq = []
compensate = []
fitlevel = []
logratiodb = []

with open('sincfilter.csv','r') as csvfile:
    plots = csv.reader(csvfile, delimiter=',')
    next(plots, None)
    for row in plots:
        freq.append(float(row[0]))
        compensate.append(float(row[1]))
        fitlevel.append(float(row[2]))
        logratiodb.append(float(row[3]) * 20.0)

fig1 = plt.figure(1)
fig1.subplots_adjust(left=0.2)
plt.plot(freq, compensate, label='compensate')
plt.plot(freq, fitlevel, label='fitlevel')
plt.xlabel('frequency [Hz]')
plt.ylabel('gain')
plt.title('compensation graph')
plt.legend()
plt.grid()
plt.savefig("fig-compensation.png")
plt.show()

fig2 = plt.figure(2)
fig2.subplots_adjust(left=0.2)
plt.plot(freq, logratiodb, label='logratio')
plt.xlabel('frequency [Hz]')
plt.ylabel('compensate - fitlevel [dB]')
plt.title('logratio')
plt.legend()
plt.grid()
plt.savefig("fig-logratio.png")
plt.show()
