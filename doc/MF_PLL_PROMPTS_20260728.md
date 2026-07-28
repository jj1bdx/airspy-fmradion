By decoding the test IQ file at `test-files/AirSpy_20260727_125800Z_89700kHz_IQ.wav`, I've found there was notable difference between the output of the current dev build and an old build at tag 20260716-0. I suspect the behavioral difference of the multipath filter is the reason.

Your mission: measure the difference between the audio output of the two builds, and investigate the possible reason. Use coefficient monitor debug option for a detailed analysis. Use c++-expert, python-expert and numpy-expert for the analysis.

After the analysis, write down the report to `doc/MF_DIFFERENCE_20260727.md`. Refer to the other reports such as `MF_ENERGY_20260726.md` for the format. 
Use the following example for the audio decoding of the IQ file:

```
airspy-fmradion -m fm -t filesource -E 36 -c freq=89700000,srate=384000,filename=AirSpy_20260727_125800Z_89700kHz_IQ.wavc,wav,format=FLOAT -G interfm-20260727125800utc-new.wav
```

Analyze the detailed difference of FM Pilot PLL behavior by decoding the test IQ file at `test-files/AirSpy_20260727_125800Z_89700kHz_IQ.wav`, between the output of the current dev build and an old build at tag 20260716-0. To confirm the difference, measure the difference between the audio output of the two builds, and investigate the possible reason. Use DEBUG_PLL_FILTER option for a detailed analysis. Use c++-expert, python-expert and numpy-expert for the analysis.

After the analysis, write down the report to `doc/MF_PLL_DIFFERENCE_20260727.md`. Refer to the other reports such as `MF_DIFFERENCE_20260727.md` for the format. 
Use the following example for the audio decoding of the IQ file:

```
airspy-fmradion -m fm -t filesource -E 36 -c freq=89700000,srate=384000,filename=AirSpy_20260727_125800Z_89700kHz_IQ.wavc,wav,format=FLOAT -G interfm-20260727125800utc-new.wav
```

Analyze the detailed difference of FM Pilot PLL behavior changing the multipath filter stages by decoding the test IQ file at `test-files/AirSpy_20260727_125800Z_89700kHz_IQ.wav`, between the output of the current dev build and an old build at tag 20260716-0, with the different multipath filter parameter of -E, of the values 18, 36, 50, 70, 100. To confirm the difference, measure the difference between the audio output of the two builds, and investigate the possible reason. Use DEBUG_PLL_FILTER option for a detailed analysis. Use c++-expert, python-expert and numpy-expert for the analysis.

After the analysis, write down the report to `doc/MF_PLL_DIFFERENCE_20260728.md`. Refer to the other reports such as `MF_PLL_DIFFERENCE_20260727.md` for the format. 
Use the following example for the audio decoding of the IQ file:

```
airspy-fmradion -m fm -t filesource -E 36 -c freq=89700000,srate=384000,filename=AirSpy_20260727_125800Z_89700kHz_IQ.wavc,wav,format=FLOAT -G interfm-20260727125800utc-new.wav
```
