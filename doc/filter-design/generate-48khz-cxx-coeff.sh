#!/bin/sh
./generate-cxx-coeff-list.py jj1bdx_ssb_48khz_12to15khz 48kHz-am-12kHz-15kHz-511taps-coeff.txt
./generate-cxx-coeff-list.py jj1bdx_am_48khz_narrow 48kHz-am-3kHz-255taps-coeff.txt
./generate-cxx-coeff-list.py jj1bdx_am_48khz_medium 48kHz-am-4.5kHz-255taps-coeff.txt
./generate-cxx-coeff-list.py jj1bdx_am_48khz_default 48kHz-am-6kHz-255taps-coeff.txt
./generate-cxx-coeff-list.py jj1bdx_cw_48khz_800hz 48kHz-cw-800Hz-511taps-coeff.txt
./generate-cxx-coeff-list.py jj1bdx_nbfm_48khz_default 48kHz-nbfm-10kHz-127taps-coeff.txt
./generate-cxx-coeff-list.py jj1bdx_nbfm_48khz_narrow 48kHz-nbfm-6.25kHz-127taps-coeff.txt
./generate-cxx-coeff-list.py jj1bdx_nbfm_48khz_medium 48kHz-nbfm-8kHz-127taps-coeff.txt
