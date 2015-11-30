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

#ifndef SOFTFM_BLADERFSOURCE_H
#define SOFTFM_BLADERFSOURCE_H

#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include "libbladeRF.h"

#include "Source.h"

class BladeRFSource : public Source
{
public:

    /** Open BladeRF device. */
    BladeRFSource(const char *serial);

    /** Close BladeRF device. */
    virtual ~BladeRFSource();

    virtual bool configure(std::string configuration);

    /** Return current sample frequency in Hz. */
    virtual std::uint32_t get_sample_rate();

    /** Return device current center frequency in Hz. */
    virtual std::uint32_t get_frequency();

    /** Print current parameters specific to device type */
    virtual void print_specific_parms();

    virtual bool start(DataBuffer<IQSample>* samples, std::atomic_bool *stop_flag);
    virtual bool stop();

    /** Return true if the device is OK, return false if there is an error. */
    virtual operator bool() const
    {
        return m_dev && m_error.empty();
    }

    /** Return a list of supported devices. */
    static void get_device_names(std::vector<std::string>& devices);

private:
    /**
     * Configure RTL-SDR tuner and prepare for streaming.
     *
     * sample_rate  :: desired sample rate in Hz.
     * frequency    :: desired center frequency in Hz.
     * bandwidth    :: desired filter bandwidth in Hz.
     * lna_gain     :: desired LNA gain index (1: 0dB, 2: 3dB, 3: 6dB).
     * vga1_gain    :: desired VGA1 gain.
     * vga2_gain    :: desired VGA2 gain.
     *
     * Return true for success, false if an error occurred.
     */
    bool configure(uint32_t sample_rate,
                   uint32_t frequency,
                   uint32_t bandwidth,
                   int lna_gainIndex,
                   int vga1_gain,
                   int vga2_gain);

    /**
     * Fetch a bunch of samples from the device.
     *
     * This function must be called regularly to maintain streaming.
     * Return true for success, false if an error occurred.
     */
    static bool get_samples(IQSampleVector *samples);

    static void run();

    struct bladerf *m_dev;
    uint32_t m_sampleRate;
    uint32_t m_actualSampleRate;
    uint32_t m_frequency;
    uint32_t m_minFrequency;
    uint32_t m_bandwidth;
    uint32_t m_actualBandwidth;
    int m_lnaGain;
    int m_vga1Gain;
    int m_vga2Gain;
    std::thread *m_thread;
    static const int m_blockSize = 1<<14;
    static BladeRFSource *m_this;
    static const std::vector<int> m_lnaGains;
    static const std::vector<int> m_vga1Gains;
    static const std::vector<int> m_vga2Gains;
    static const std::vector<int> m_halfbw;
    std::string m_lnaGainsStr;
    std::string m_vga1GainsStr;
    std::string m_vga2GainsStr;
    std::string m_bwfiltStr;
};

#endif // SOFTFM_BLADERFSOURCE_H
