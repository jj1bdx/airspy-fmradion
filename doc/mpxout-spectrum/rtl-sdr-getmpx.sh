#!/bin/sh
# 88.1MHz (1Msps, Set 200kHz upper freq)
# CF32 IQ signal decimated to 500kHz first, 
# FM decoded to real signal, 
# then decimate by 2.604166666 to 192kHz CF32 output
rtl_sdr -s 1000000 -f 88300000 -g 3.7 - |
    csdr convert_u8_f | csdr shift_addition_cc 0.2 | 
    csdr fir_decimate_cc 2 0.05 HAMMING | \
    csdr fmdemod_quadri_cf | csdr fractional_decimator_ff 2.6041666666666
