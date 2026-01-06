#!/usr/bin/env python3
import scipy.signal as signal
import numpy as np
import matplotlib.pyplot as plt

# pure integrator with fixed gain of 80dB (10000)
sosinteg = [10000.0, 0.0, 0.0, 1.0, -1.0, 0.0]
# original 2nd-order LPF
sosprelpf = [1.46974784e-06, 0, 0, 1, -1.99682419, 0.996825659]
# original 1st-order HPF (or inversed LPF)
sosfirsthpf = [0.000304341788, -0.000304324564, 0, 1, 0, 0]
# approximated 2nd-order LPF by Butterworth design
sosprelpf2 = signal.butter(2, 30, 'lowpass', False, 'sos', 384000)

# reference open-loop charasteristics
sosref = [sosprelpf, sosfirsthpf, sosinteg]

# HPF only
soshpfonly = [sosfirsthpf]

# LPF only
soslpfonly = [sosprelpf]

# original twofilt
sostwofilt = [sosprelpf, sosfirsthpf]
 
# new twofilt
sostwofilt = [sosprelpf2, sosfirsthpf]

w, h = signal.sosfreqz(sosref, 38400000, False, 384000)

plt.subplot(2, 1, 1)
db = 20*np.log10(np.maximum(np.abs(h), 1e-20))
plt.plot(np.log10(w), db)
plt.ylim(-80, +80)
plt.xlim(-2, 3)
plt.grid(True)
#plt.yticks([0, -20, -40, -60, -80, -100, -120, -140, -160, -180])
#plt.yticks([-120, -140, -160, -180, -200])
#plt.xticks([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
plt.ylabel('Gain [dB]')
plt.ylabel('Gain [dB]')
plt.title('Frequency Response')
plt.subplot(2, 1, 2)
plt.plot(np.log10(w), np.angle(h))
plt.grid(True)
plt.xlim(-2, 3)
plt.ylim(-2, 0)
#plt.yticks([-np.pi, -0.5*np.pi, 0],
#           [r'$-\pi$', r'$-\pi/2$', '0'])
#plt.xticks([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
plt.ylabel('Phase [rad]')
plt.xlabel('frequency [log10(Hz)]')
plt.show()
