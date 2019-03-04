#!/usr/bin/env python3
from scipy import signal
import matplotlib.pyplot as plt
import numpy as np

# calculated by Twitter lambdaprog 
# as a 7-stage IQ converter filter for Airspy R2

b = [-0.031534955091398462, 0.0, 0.281533904166905770, 0.5,
      0.281533904166905770,  0.0, -0.031534955091398462]

w, h = signal.freqz(b)
fig, ax1 = plt.subplots()
ax1.set_title('Digital filter frequency response')
ax1.plot(w, 20 * np.log10(abs(h)), 'b')
ax1.set_ylabel('Amplitude [dB]', color='b')
ax1.set_xlabel('Frequency [rad/sample]')
plt.show()
