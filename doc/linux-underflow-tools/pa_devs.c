/* List PortAudio output devices: index, host API, name, latencies. */
#include <portaudio.h>
#include <stdio.h>

int main(void) {
  PaError err = Pa_Initialize();
  if (err != paNoError) {
    fprintf(stderr, "Pa_Initialize: %s\n", Pa_GetErrorText(err));
    return 1;
  }
  printf("%s\n", Pa_GetVersionText());
  printf("default output device: %d\n", Pa_GetDefaultOutputDevice());
  const int n = Pa_GetDeviceCount();
  for (int i = 0; i < n; i++) {
    const PaDeviceInfo *d = Pa_GetDeviceInfo(i);
    if (d == NULL || d->maxOutputChannels <= 0) {
      continue;
    }
    const PaHostApiInfo *h = Pa_GetHostApiInfo(d->hostApi);
    printf("%3d\t%s\tout=%d\tlow=%.1fms\thigh=%.1fms\trate=%.0f\t%s\n", i,
           h ? h->name : "?", d->maxOutputChannels,
           d->defaultLowOutputLatency * 1000.0,
           d->defaultHighOutputLatency * 1000.0, d->defaultSampleRate,
           d->name);
  }
  Pa_Terminate();
  return 0;
}
