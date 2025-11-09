# airspy-fmradion FIR filter data

* Filter characteristics are described in Scipy code
* How to show the display characteristics:

```shell
./display-freq-khz.py [sampling frequency in kHz] [coefficient file]
# example
./display-freq-khz.py 48 48kHz-fmaudio-64taps-coeff.txt
```

## Design tools used

* For .ih_fir files: Iowa Hills Software FIR filter design program
* For .json files: [PyFDA](https://github.com/chipmuenk/pyFDA/)


