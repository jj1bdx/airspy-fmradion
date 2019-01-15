#!/usr/bin/env python3
# Curve-fitting calculation script for sinc compensation

import math
from scipy import optimize

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

def sincfitting(maxfreq, staticgain, fitfactor):
    sqsum = 0
    difflimit = 10.0 ** (-0.05) # -0.1dB
    if staticgain - fitfactor < difflimit:
        return 99999999.0
    for freq in range(50,57001,50):
        theta = 2 * math.pi * freq / maxfreq;
        compensate = 1.0 / aperture(theta)
        # filter model: 
        # mov1: moving-average filter (stage number: 1)
        # mov1[n] = (input[n-1] + input[n]) / 2.0
        # final output: gain-controlled sum of the above two filters
        # output[n] = staticgain * input[n] - fitfactor * mov1[n]
        fitlevel = staticgain - (fitfactor * mov1(2 * theta))
        logratio = math.log10(compensate / fitlevel)
        sqsum += logratio * logratio
    return sqsum

def sincfitting_test(x, f):
    return sincfitting(f, x[0], x[1])

print(optimize.minimize(
        sincfitting_test, [1.5, 0.5], args=(120000.0,),
        method="Nelder-Mead"))
print(optimize.minimize(
        sincfitting_test, [1.1, 0.095], args=(480000.0,),
        method="Nelder-Mead"))
