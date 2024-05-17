# GPSDO clock limitation for AirSpy R2

17-MAY-2024

Giving 10MHz reference clock to AirSpy R2 from Leo Bodnar GPSDO
did *not* give less noise on output; it gave spurious output
~10kHz of -75dB, during no-sound signal of NHK-FM Tokyo 82.5MHz.
