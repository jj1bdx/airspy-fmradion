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

#ifndef SOFTFM_RTLSDRSOURCE_H
#define SOFTFM_RTLSDRSOURCE_H

#include <cstdint>
#include <string>
#include <vector>
#include <thread>

#include "Source.h"

class RtlSdrSource : public Source
{
public:

    static const int default_block_length = 65536;

    /** Open RTL-SDR device. */
    RtlSdrSource(int dev_index);

    /** Close RTL-SDR device. */
    virtual ~RtlSdrSource();

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
     * tuner_gain   :: desired tuner gain in 0.1 dB, or INT_MIN for auto-gain.
     * block_length :: preferred number of samples per block.
     *
     * Return true for success, false if an error occurred.
     */
    bool configure(std::uint32_t sample_rate,
                   std::uint32_t frequency,
                   int tuner_gain,
                   int block_length=default_block_length,
                   bool agcmode=false);

    /** Return a list of supported tuner gain settings in units of 0.1 dB. */
    std::vector<int> get_tuner_gains();

    /** Return current tuner gain in units of 0.1 dB. */
    int get_tuner_gain();

    /**
     * Fetch a bunch of samples from the device.
     *
     * This function must be called regularly to maintain streaming.
     * Return true for success, false if an error occurred.
     */
    static bool get_samples(IQSampleVector *samples);

    static void run();

    struct rtlsdr_dev * m_dev;
    int                 m_block_length;
    std::vector<int>    m_gains;
    std::string         m_gainsStr;
    bool                m_confAgc;
    std::thread         *m_thread;
    static RtlSdrSource *m_this;
};

#endif
