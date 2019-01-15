#!/usr/bin/env python3
# Curve-fitting calculation script for sinc compensation

import math
import csv

def aperture(x):
    if x == 0.0:
        return 1.0
    else:
        return 2.0 / x * math.sin(x/2.0)

def mov1(x):
    y = math.sin(x/4.0);
    if y == 0.0:
        return 0.0
    else:
        return 0.5 * math.sin(x/2.0) / math.sin (x/4.0)

# filter model:
# mov1: moving-average filter (stage number: 1)
# mov1[n] = (input[n-1] + input[n]) / 2.0
# final output: gain-controlled sum of the above two filters
# output[n] = staticgain * input[n] - fitfactor * mov1[n]

maxfreq = 480000
staticgain = 1.3412962
fitfactor = 0.34135089

with open('sincfilter.csv', mode='w') as output_file:
    fieldnames = ['freq', 'compensate', 'fitlevel', 'logratio']
    output_writer = csv.DictWriter(output_file, fieldnames=fieldnames)

    output_writer.writeheader()

    for freq in range(50,53100,1000):
        theta = 2 * math.pi * freq / maxfreq;
        compensate = 1.0 / aperture(theta)
        fitlevel = staticgain - (fitfactor * mov1(2 * theta))
        logratio = math.log10(compensate / fitlevel)
        output_writer.writerow({
              'freq': freq,
              'compensate': compensate,
              'fitlevel': fitlevel,
              'logratio': logratio
              })
