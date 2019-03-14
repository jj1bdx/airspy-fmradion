#!/bin/sh
# 88.1MHz (768ksps, Set 192kHz lower freq)
# CF32 IQ signal decimated to 192kHz and
# FM decoded to real signal
airspyhf_rx -z -d -r stdout -f87.908 | \
    csdr shift_addition_cc -0.25 | \
    csdr fir_decimate_cc 4 0.05 HAMMING | \
    csdr fmdemod_quadri_cf
