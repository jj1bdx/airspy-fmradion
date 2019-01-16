#!/usr/bin/env python3
import numpy as np
import matplotlib.pyplot as plt
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
    absmax = 0
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
        if (absmax < math.fabs(logratio)):
            absmax = math.fabs(logratio)
        sqsum += logratio * logratio
    return (sqsum, absmax)

def sincfitting_test(x, f):
    return sincfitting(f, x[0], x[1])[0]

data = np.genfromtxt('curve1.csv', delimiter=',', skip_header=1, dtype='float')
freq = data[:,0]
staticgain = data[:,1]
fitlevel = data[:,2]
    
s_a1, s_a2, s_b = np.polyfit(freq, staticgain, 2)
print("staticgain =", s_a1, "* f * f +", s_a2, "* f +", s_b)
f_a1, f_a2, f_b = np.polyfit(freq, fitlevel, 2)
print("fitlevel =", f_a1, "* f * f +", f_a2, "* f +", f_b)

print("#maxfreq, staticgain, fitlevel, absmax")
for freq in range(450000, 850001, 10000):
    staticgain = s_a1 * freq * freq + s_a2 * freq + s_b
    fitlevel = f_a1 * freq * freq + f_a2 * freq + f_b
    absmax = sincfitting(freq, staticgain, fitlevel)[1]
    print(freq, staticgain, fitlevel, absmax, sep = ",")
