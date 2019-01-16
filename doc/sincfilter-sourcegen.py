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
    absmax = 0
    difflimit = 10.0 ** (-0.05) # -0.1dB
    if staticgain - fitfactor < difflimit:
        return (99999999.0, 99999999.0)
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
            absmax = logratio
        sqsum += logratio * logratio
    return (sqsum, absmax)

def sincfitting_test(x, f):
    return sincfitting(f, x[0], x[1])[0]

def minimizer(freq):
    return optimize.minimize(sincfitting_test,
            [1.334, 0.334], args=(freq,), method="Nelder-Mead")

freq_initial = 100000.0
freq_step = 10000.0
freq_maximum = 5000000.0

freq = freq_initial
vector_staticgain = []
vector_fitlevel = []
while freq <= freq_maximum:
    result = minimizer(freq)
    staticgain = result.x[0]
    fitlevel = result.x[1]
    absmax = sincfitting(freq, staticgain, fitlevel)[1]
    # print(freq, staticgain, fitlevel, absmax, sep = ",")
    vector_staticgain.append(staticgain)
    vector_fitlevel.append(fitlevel)
    freq += freq_step

print("double freq_initial = ", freq_initial, ";", sep="")
print("double freq_step = ", freq_step, ";", sep="")

freq = freq_initial
print("std::vector<double> vector_staticgain {")
for x in vector_staticgain:
    print("    ", x , ", // ", freq, " Hz", sep="")
    freq += freq_step
print("    };")

freq = freq_initial
print("std::vector<double> vector_fitlevel {")
for x in vector_fitlevel:
    print("    ", x , ", // ", freq, " Hz", sep="")
    freq += freq_step
print("    };")

