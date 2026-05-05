# Device connectivity tests

* The purpose is to ensure that all supporter SDR devices work OK
* Use local FM radio stations (Tokyo stations are in this example)
* Apply the best RF gain values without distortion

## Commands

```shell
# Airspy HF+ Discovery
airspy-fmradion -m fm -E100 -t airspyhf -c freq=82500000 -P -
airspy-fmradion -m fm -E100 -t airspyhf -c freq=89700000 -P -
# Airspy R2
airspy-fmradion -m fm -E100 -t airspy -c freq=82500000,lgain=5,mgain=0,vgain=10 -P -
airspy-fmradion -m fm -E100 -t airspy -c freq=89700000,lgain=8,mgain=0,vgain=10 -P -
# RTL-SDR
airspy-fmradion -m fm -E100 -t rtlsdr -c srate=1536k,gain=3.7,freq=82500000 -P -
airspy-fmradion -m fm -E100 -t rtlsdr -c srate=1536k,gain=12.5,freq=89700000 -P -
