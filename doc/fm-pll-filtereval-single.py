#!/usr/bin/env python3
import scipy.signal as signal
import numpy as np
import matplotlib.pyplot as plt

# simulated integrator
sosinteg = [1e-6, 0, 0, 1, -0.999999, 0]
# 2nd-order LPF
sosprelpf = [1.46974784e-06, 0, 0, 1, -1.99682419, 0.996825659]
# 1st-order HPF (or inversed LPF)
sosfirsthpf = [0.000304341788, -0.000304324564, 0, 1, 0, 0]

sostest = [sosprelpf, sosprelpf, sosfirsthpf, sosinteg, sosinteg]

sosref = [sosprelpf, sosfirsthpf, sosinteg, sosinteg]

sostwofilt = [sosprelpf, sosfirsthpf]

w, h = signal.sosfreqz(sosref, 38400000, False, 384000)

plt.subplot(2, 1, 1)
db = 20*np.log10(np.maximum(np.abs(h), 1e-20))
plt.plot(np.log10(w), db)
#plt.ylim(-300, -0)
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
plt.yticks([-np.pi, -0.5*np.pi, 0, 0.5*np.pi, np.pi],
           [r'$-\pi$', r'$-\pi/2$', '0', r'$\pi/2$', r'$\pi$'])
#plt.xticks([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
plt.ylabel('Phase [rad]')
plt.xlabel('frequency [log10(Hz)]')
plt.show()
