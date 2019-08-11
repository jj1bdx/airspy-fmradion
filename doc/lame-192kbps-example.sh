airspy-fmradion -t airspyhf -q -c freq=76500000,srate=384000 -R - | lame -r -s 48 --signed --bitwidth 16 --little-endian -m j -b 192 -q 0 --cbr --silent - test3.mp3
