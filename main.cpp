///////////////////////////////////////////////////////////////////////////////////
// SoftFM - Software decoder for FM broadcast radio with stereo support          //
//                                                                               //
// Copyright (C) 2015 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
/////////////////////////////////////////////////////////////////////////////////// 

#include <cstdlib>
#include <cstdio>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>

#include "util.h"
#include "SoftFM.h"
#include "RtlSdrSource.h"
#include "FmDecode.h"
#include "AudioOutput.h"
#include "MovingAverage.h"



/** Flag is set on SIGINT / SIGTERM. */
static std::atomic_bool stop_flag(false);


/** Buffer to move sample data between threads. */
template <class Element>
class DataBuffer
{
public:
    /** Constructor. */
    DataBuffer()
        : m_qlen(0)
        , m_end_marked(false)
    { }

    /** Add samples to the queue. */
    void push(std::vector<Element>&& samples)
    {
        if (!samples.empty()) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_qlen += samples.size();
            m_queue.push(move(samples));
            lock.unlock();
            m_cond.notify_all();
        }
    }

    /** Mark the end of the data stream. */
    void push_end()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_end_marked = true;
        lock.unlock();
        m_cond.notify_all();
    }

    /** Return number of samples in queue. */
    std::size_t queued_samples()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_qlen;
    }

    /**
     * If the queue is non-empty, remove a block from the queue and
     * return the samples. If the end marker has been reached, return
     * an empty vector. If the queue is empty, wait until more data is pushed
     * or until the end marker is pushed.
     */
    std::vector<Element> pull()
    {
    	std::vector<Element> ret;
    	std::unique_lock<std::mutex> lock(m_mutex);
        while (m_queue.empty() && !m_end_marked)
            m_cond.wait(lock);
        if (!m_queue.empty()) {
            m_qlen -= m_queue.front().size();
            swap(ret, m_queue.front());
            m_queue.pop();
        }
        return ret;
    }

    /** Return true if the end has been reached at the Pull side. */
    bool pull_end_reached()
    {
    	std::unique_lock<std::mutex> lock(m_mutex);
        return m_qlen == 0 && m_end_marked;
    }

    /** Wait until the buffer contains minfill samples or an end marker. */
    void wait_buffer_fill(std::size_t minfill)
    {
    	std::unique_lock<std::mutex> lock(m_mutex);
        while (m_qlen < minfill && !m_end_marked)
            m_cond.wait(lock);
    }

private:
    std::size_t              m_qlen;
    bool                     m_end_marked;
    std::queue<std::vector<Element>> m_queue;
    std::mutex               m_mutex;
    std::condition_variable  m_cond;
};


/** Simple linear gain adjustment. */
void adjust_gain(SampleVector& samples, double gain)
{
    for (unsigned int i = 0, n = samples.size(); i < n; i++) {
        samples[i] *= gain;
    }
}


/**
 * Read data from source device and put it in a buffer.
 *
 * This code runs in a separate thread.
 * The RTL-SDR library is not capable of buffering large amounts of data.
 * Running this in a background thread ensures that the time between calls
 * to RtlSdrSource::get_samples() is very short.
 */
void read_source_data(RtlSdrSource *rtlsdr, DataBuffer<IQSample> *buf)
{
    IQSampleVector iqsamples;

    while (!stop_flag.load()) {

        if (!rtlsdr->get_samples(iqsamples)) {
            fprintf(stderr, "ERROR: RtlSdr: %s\n", rtlsdr->error().c_str());
            exit(1);
        }

        buf->push(move(iqsamples));
    }

    buf->push_end();
}


/**
 * Get data from output buffer and write to output stream.
 *
 * This code runs in a separate thread.
 */
void write_output_data(AudioOutput *output, DataBuffer<Sample> *buf,
                       unsigned int buf_minfill)
{
    while (!stop_flag.load()) {

        if (buf->queued_samples() == 0) {
            // The buffer is empty. Perhaps the output stream is consuming
            // samples faster than we can produce them. Wait until the buffer
            // is back at its nominal level to make sure this does not happen
            // too often.
            buf->wait_buffer_fill(buf_minfill);
        }

        if (buf->pull_end_reached()) {
            // Reached end of stream.
            break;
        }

        // Get samples from buffer and write to output.
        SampleVector samples = buf->pull();
        output->write(samples);
        if (!(*output)) {
            fprintf(stderr, "ERROR: AudioOutput: %s\n", output->error().c_str());
        }
    }
}


/** Handle Ctrl-C and SIGTERM. */
static void handle_sigterm(int sig)
{
    stop_flag.store(true);

    std::string msg = "\nGot signal ";
    msg += strsignal(sig);
    msg += ", stopping ...\n";

    const char *s = msg.c_str();
    write(STDERR_FILENO, s, strlen(s));
}


void usage()
{
    fprintf(stderr,
    "Usage: softfm [options]\n"
    		"  -c config     Comma separated key=value configuration pairs or just key for switches\n"
    		"                See below for valid values per device type\n"
            "  -d devidx     RTL-SDR device index, 'list' to show device list (default 0)\n"
            "  -r pcmrate    Audio sample rate in Hz (default 48000 Hz)\n"
            "  -M            Disable stereo decoding\n"
            "  -R filename   Write audio data as raw S16_LE samples\n"
            "                use filename '-' to write to stdout\n"
            "  -W filename   Write audio data to .WAV file\n"
            "  -P [device]   Play audio via ALSA device (default 'default')\n"
            "  -T filename   Write pulse-per-second timestamps\n"
            "                use filename '-' to write to stdout\n"
            "  -b seconds    Set audio buffer size in seconds\n"
    		"\n"
    		"Configuration options for RTL-SDR devices\n"
    		"  freq=<int>    Frequency of radio station in Hz (default 100000000)\n"
    		"  srate=<int>   IF sample rate in Hz (default 1000000)\n"
			"                (valid ranges: [225001, 300000], [900001, 3200000]))\n"
    		"  gain=<float>  Set LNA gain in dB, or 'auto',\n"
    		"                or 'list' to just get a list of valid values (default auto)\n"
    		"  blklen=<int>  Set audio buffer size in seconds (default RTL-SDR default)\n"
    		"  agc           Enable RTL AGC mode (default disabled)\n"
            "\n");
}


void badarg(const char *label)
{
    usage();
    fprintf(stderr, "ERROR: Invalid argument for %s\n", label);
    exit(1);
}


bool parse_int(const char *s, int& v, bool allow_unit=false)
{
    char *endp;
    long t = strtol(s, &endp, 10);
    if (endp == s)
        return false;
    if ( allow_unit && *endp == 'k' &&
         t > INT_MIN / 1000 && t < INT_MAX / 1000 ) {
        t *= 1000;
        endp++;
    }
    if (*endp != '\0' || t < INT_MIN || t > INT_MAX)
        return false;
    v = t;
    return true;
}


/** Return Unix time stamp in seconds. */
double get_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + 1.0e-6 * tv.tv_usec;
}


int main(int argc, char **argv)
{
    int     devidx  = 0;
    int     pcmrate = 48000;
    bool    stereo  = true;
    enum OutputMode { MODE_RAW, MODE_WAV, MODE_ALSA };
    OutputMode outmode = MODE_ALSA;
    std::string  filename;
    std::string  alsadev("default");
    std::string  ppsfilename;
    FILE *  ppsfile = NULL;
    double  bufsecs = -1;
    std::string config_str;

    fprintf(stderr,
            "SoftFM - Software decoder for FM broadcast radio with RTL-SDR\n");

    const struct option longopts[] = {
        { "config",     2, NULL, 'c' },
        { "dev",        1, NULL, 'd' },
        { "pcmrate",    1, NULL, 'r' },
        { "mono",       0, NULL, 'M' },
        { "raw",        1, NULL, 'R' },
        { "wav",        1, NULL, 'W' },
        { "play",       2, NULL, 'P' },
        { "pps",        1, NULL, 'T' },
        { "buffer",     1, NULL, 'b' },
        { NULL,         0, NULL, 0 } };

    int c, longindex;
    while ((c = getopt_long(argc, argv,
                            "c:d:r:MR:W:P::T:b:",
                            longopts, &longindex)) >= 0) {
        switch (c) {
            case 'c':
            	config_str.assign(optarg);
                break;
            case 'd':
                if (!parse_int(optarg, devidx))
                    devidx = -1;
                break;
            case 'r':
                if (!parse_int(optarg, pcmrate, true) || pcmrate < 1) {
                    badarg("-r");
                }
                break;
            case 'M':
                stereo = false;
                break;
            case 'R':
                outmode = MODE_RAW;
                filename = optarg;
                break;
            case 'W':
                outmode = MODE_WAV;
                filename = optarg;
                break;
            case 'P':
                outmode = MODE_ALSA;
                if (optarg != NULL)
                    alsadev = optarg;
                break;
            case 'T':
                ppsfilename = optarg;
                break;
            case 'b':
                if (!parse_dbl(optarg, bufsecs) || bufsecs < 0) {
                    badarg("-b");
                }
                break;
            default:
                usage();
                fprintf(stderr, "ERROR: Invalid command line options\n");
                exit(1);
        }
    }

    if (optind < argc)
    {
        usage();
        fprintf(stderr, "ERROR: Unexpected command line options\n");
        exit(1);
    }

    // Catch Ctrl-C and SIGTERM
    struct sigaction sigact;
    sigact.sa_handler = handle_sigterm;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_RESETHAND;

    if (sigaction(SIGINT, &sigact, NULL) < 0)
    {
        fprintf(stderr, "WARNING: can not install SIGINT handler (%s)\n", strerror(errno));
    }

    if (sigaction(SIGTERM, &sigact, NULL) < 0)
    {
        fprintf(stderr, "WARNING: can not install SIGTERM handler (%s)\n", strerror(errno));
    }

    std::vector<std::string> devnames = RtlSdrSource::get_device_names();

    if (devidx < 0 || (unsigned int)devidx >= devnames.size())
    {
        if (devidx != -1)
        {
            fprintf(stderr, "ERROR: invalid device index %d\n", devidx);
        }

        fprintf(stderr, "Found %u devices:\n", (unsigned int)devnames.size());

        for (unsigned int i = 0; i < devnames.size(); i++)
        {
            fprintf(stderr, "%2u: %s\n", i, devnames[i].c_str());
        }

        exit(1);
    }

    fprintf(stderr, "using device %d: %s\n", devidx, devnames[devidx].c_str());

    // Open RTL-SDR device.
    RtlSdrSource rtlsdr(devidx);
    if (!rtlsdr)
    {
        fprintf(stderr, "ERROR: RtlSdr: %s\n", rtlsdr.error().c_str());
        exit(1);
    }

    // Configure RTL-SDR device and start streaming.
    if (!rtlsdr.configure(config_str))
    {
        fprintf(stderr, "ERROR: RtlSdr configuration: %s\n", rtlsdr.error().c_str());
        exit(1);
    }

    double freq = rtlsdr.get_configured_frequency();
    fprintf(stderr, "tuned for:         %.6f MHz\n", freq * 1.0e-6);

    double tuner_freq = rtlsdr.get_frequency();
    fprintf(stderr, "device tuned for:  %.6f MHz\n", tuner_freq * 1.0e-6);

    double ifrate = rtlsdr.get_sample_rate();
    fprintf(stderr, "IF sample rate:    %.0f Hz\n", ifrate);

    double delta_if = tuner_freq - freq;
    MovingAverage<float> ppm_average(40, 0.0f);

    rtlsdr.print_specific_parms();

    // Create source data queue.
    DataBuffer<IQSample> source_buffer;

    // Start reading from device in separate thread.
    std::thread source_thread(read_source_data, &rtlsdr, &source_buffer);

    // The baseband signal is empty above 100 kHz, so we can
    // downsample to ~ 200 kS/s without loss of information.
    // This will speed up later processing stages.
    unsigned int downsample = std::max(1, int(ifrate / 215.0e3));
    fprintf(stderr, "baseband downsampling factor %u\n", downsample);

    // Prevent aliasing at very low output sample rates.
    double bandwidth_pcm = std::min(FmDecoder::default_bandwidth_pcm, 0.45 * pcmrate);
    fprintf(stderr, "audio sample rate: %u Hz\n", pcmrate);
    fprintf(stderr, "audio bandwidth:   %.3f kHz\n", bandwidth_pcm * 1.0e-3);

    // Prepare decoder.
    FmDecoder fm(ifrate,                            // sample_rate_if
                 freq - tuner_freq,                 // tuning_offset
                 pcmrate,                           // sample_rate_pcm
                 stereo,                            // stereo
                 FmDecoder::default_deemphasis,     // deemphasis,
                 FmDecoder::default_bandwidth_if,   // bandwidth_if
                 FmDecoder::default_freq_dev,       // freq_dev
                 bandwidth_pcm,                     // bandwidth_pcm
                 downsample);                       // downsample

    // Calculate number of samples in audio buffer.
    unsigned int outputbuf_samples = 0;

    if (bufsecs < 0 && (outmode == MODE_ALSA || (outmode == MODE_RAW && filename == "-")))
    {
        // Set default buffer to 1 second for interactive output streams.
        outputbuf_samples = pcmrate;
    }
    else if (bufsecs > 0)
    {
        // Calculate nr of samples for configured buffer length.
        outputbuf_samples = (unsigned int)(bufsecs * pcmrate);
    }

    if (outputbuf_samples > 0)
    {
       fprintf(stderr, "output buffer:     %.1f seconds\n", outputbuf_samples / double(pcmrate));
    }

    // Open PPS file.
    if (!ppsfilename.empty())
    {
        if (ppsfilename == "-")
        {
            fprintf(stderr, "writing pulse-per-second markers to stdout\n");
            ppsfile = stdout;
        }
        else
        {
            fprintf(stderr, "writing pulse-per-second markers to '%s'\n", ppsfilename.c_str());
            ppsfile = fopen(ppsfilename.c_str(), "w");

            if (ppsfile == NULL)
            {
                fprintf(stderr, "ERROR: can not open '%s' (%s)\n", ppsfilename.c_str(), strerror(errno));
                exit(1);
            }
        }

        fprintf(ppsfile, "#pps_index sample_index   unix_time\n");
        fflush(ppsfile);
    }

    // Prepare output writer.
    std::unique_ptr<AudioOutput> audio_output;

    switch (outmode)
    {
        case MODE_RAW:
            fprintf(stderr, "writing raw 16-bit audio samples to '%s'\n", filename.c_str());
            audio_output.reset(new RawAudioOutput(filename));
            break;
        case MODE_WAV:
            fprintf(stderr, "writing audio samples to '%s'\n", filename.c_str());
            audio_output.reset(new WavAudioOutput(filename, pcmrate, stereo));
            break;
        case MODE_ALSA:
            fprintf(stderr, "playing audio to ALSA device '%s'\n", alsadev.c_str());
            audio_output.reset(new AlsaAudioOutput(alsadev, pcmrate, stereo));
            break;
    }

    if (!(*audio_output))
    {
        fprintf(stderr, "ERROR: AudioOutput: %s\n", audio_output->error().c_str());
        exit(1);
    }

    // If buffering enabled, start background output thread.
    DataBuffer<Sample> output_buffer;
    std::thread output_thread;

    if (outputbuf_samples > 0)
    {
        unsigned int nchannel = stereo ? 2 : 1;
        output_thread = std::thread(write_output_data,
                               audio_output.get(),
                               &output_buffer,
                               outputbuf_samples * nchannel);
    }

    SampleVector audiosamples;
    bool inbuf_length_warning = false;
    double audio_level = 0;
    bool got_stereo = false;

    double block_time = get_time();

    // Main loop.
    for (unsigned int block = 0; !stop_flag.load(); block++)
    {

        // Check for overflow of source buffer.
        if (!inbuf_length_warning && source_buffer.queued_samples() > 10 * ifrate)
        {
            fprintf(stderr, "\nWARNING: Input buffer is growing (system too slow)\n");
            inbuf_length_warning = true;
        }

        // Pull next block from source buffer.
        IQSampleVector iqsamples = source_buffer.pull();

        if (iqsamples.empty())
        {
            break;
        }

        double prev_block_time = block_time;
        block_time = get_time();

        // Decode FM signal.
        fm.process(iqsamples, audiosamples);

        // Measure audio level.
        double audio_mean, audio_rms;
        samples_mean_rms(audiosamples, audio_mean, audio_rms);
        audio_level = 0.95 * audio_level + 0.05 * audio_rms;

        // Set nominal audio volume.
        adjust_gain(audiosamples, 0.5);

		ppm_average.feed(((fm.get_tuning_offset() + delta_if) / tuner_freq) * -1.0e6); // the minus factor is to show the ppm correction to make and not the one made

        // Show statistics.
        fprintf(stderr,
                "\rblk=%6d  freq=%10.6fMHz  ppm=%+6.2f  IF=%+5.1fdB  BB=%+5.1fdB  audio=%+5.1fdB ",
                block,
                (tuner_freq + fm.get_tuning_offset()) * 1.0e-6,
                ppm_average.average(),
                //((fm.get_tuning_offset() + delta_if) / tuner_freq) * 1.0e6,
                20*log10(fm.get_if_level()),
                20*log10(fm.get_baseband_level()) + 3.01,
                20*log10(audio_level) + 3.01);

        if (outputbuf_samples > 0)
        {
            unsigned int nchannel = stereo ? 2 : 1;
            std::size_t buflen = output_buffer.queued_samples();
            fprintf(stderr, " buf=%.1fs ", buflen / nchannel / double(pcmrate));
        }

        fflush(stderr);

        // Show stereo status.
        if (fm.stereo_detected() != got_stereo)
        {
            got_stereo = fm.stereo_detected();

            if (got_stereo)
            {
                fprintf(stderr, "\ngot stereo signal (pilot level = %f)\n", fm.get_pilot_level());
            }
            else
            {
                fprintf(stderr, "\nlost stereo signal\n");
            }
        }

        // Write PPS markers.
        if (ppsfile != NULL)
        {
            for (const PilotPhaseLock::PpsEvent& ev : fm.get_pps_events())
            {
                double ts = prev_block_time;
                ts += ev.block_position * (block_time - prev_block_time);
                fprintf(ppsfile, "%8s %14s %18.6f\n",
                		std::to_string(ev.pps_index).c_str(),
						std::to_string(ev.sample_index).c_str(),
                        ts);
                fflush(ppsfile);
            }
        }

        // Throw away first block. It is noisy because IF filters
        // are still starting up.
        if (block > 0)
        {
            // Write samples to output.
            if (outputbuf_samples > 0)
            {
                // Buffered write.
                output_buffer.push(move(audiosamples));
            }
            else
            {
                // Direct write.
                audio_output->write(audiosamples);
            }
        }
    }

    fprintf(stderr, "\n");

    // Join background threads.
    source_thread.join();
    if (outputbuf_samples > 0)
    {
        output_buffer.push_end();
        output_thread.join();
    }

    // No cleanup needed; everything handled by destructors.

    return 0;
}

/* end */
