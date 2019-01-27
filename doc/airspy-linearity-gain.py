#!/usr/bin/env python3

airspy_linearity_vga_gains = [ 13, 12, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 8, 7, 6, 5, 4 ]
airspy_linearity_mixer_gains = [ 12, 12, 11, 9, 8, 7, 6, 6, 5, 0, 0, 1, 0, 0, 2, 2, 1, 1, 1, 1, 0, 0 ]
airspy_linearity_lna_gains = [ 14, 14, 14, 13, 12, 10, 9, 9, 8, 9, 8, 6, 5, 3, 1, 0, 0, 0, 0, 0, 0, 0 ]
airspy_linearity_vga_gains.reverse()
airspy_linearity_mixer_gains.reverse()
airspy_linearity_lna_gains.reverse()

print('Airspy Linearity gain conversion table for ngsoftfm:')
for i in range(len(airspy_linearity_lna_gains)):
    print('linearity gain i = %d: lgain=%d,mgain=%d,vgain=%d' %
            (i, airspy_linearity_lna_gains[i],
                airspy_linearity_mixer_gains[i],
                airspy_linearity_vga_gains[i]))
