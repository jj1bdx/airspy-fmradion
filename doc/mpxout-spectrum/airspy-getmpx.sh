#!/bin/sh
# 88.1MHz (10Msps, Set 500kHz upper freq)
# CF32 IQ signal decimated to 768kHz first, 
# FM decoded to real signal, 
# then decimate by 4 to 192kHz CF32 output
airspy_rx -r/dev/fd/1 -p0 -f88.6 -a10000000 -t0 -g7 | \
    csdr shift_addition_cc 0.05 | \
    csdr fir_decimate_cc 13.028333333333 0.05 HAMMING | \
    csdr fmdemod_quadri_cf | csdr fractional_decimator_ff 4
