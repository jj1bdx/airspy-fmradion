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

#include <climits>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <rtl-sdr.h>

#include "util.h"
#include "parsekv.h"
#include "BladeRFSource.h"

BladeRFSource *BladeRFSource::m_this = 0;
const std::vector<int> BladeRFSource::m_lnaGains({0, 3, 6});
const std::vector<int> BladeRFSource::m_vga1Gains({5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30});
const std::vector<int> BladeRFSource::m_vga2Gains({0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30});
const std::vector<int> BladeRFSource::m_halfbw({750000, 875000, 1250000, 1375000, 1500000, 1920000, 2500000, 2750000, 3000000, 3500000, 4375000, 5000000, 6000000, 7000000, 10000000, 14000000});

// Open BladeRF device.
BladeRFSource::BladeRFSource(const char *serial) :
    m_dev(0),
    m_sampleRate(1000000),
    m_actualSampleRate(1000000),
    m_frequency(300000000),
    m_minFrequency(300000000),
    m_bandwidth(1500000),
    m_actualBandwidth(1500000),
    m_lnaGain(3),
    m_vga1Gain(6),
    m_vga2Gain(5),
    m_thread(0)
{
    int status;
    struct bladerf_devinfo info;

    bladerf_init_devinfo(&info);

    if (serial != 0)
    {
        strncpy(info.serial, serial, BLADERF_SERIAL_LENGTH - 1);
        info.serial[BLADERF_SERIAL_LENGTH - 1] = '\0';
    }

    status = bladerf_open_with_devinfo(&m_dev, &info);

    if (status == BLADERF_ERR_NODEV)
    {
        std::ostringstream err_ostr;
        err_ostr << "No devices available with serial=" << serial;
        m_error = err_ostr.str();
        m_dev = 0;
    }
    else if (status != 0)
    {
        std::ostringstream err_ostr;
        err_ostr << "Failed to open device with serial=" << serial;
        m_error = err_ostr.str();
        m_dev = 0;
    }
    else
    {
        int fpga_loaded = bladerf_is_fpga_configured(m_dev);

        if (fpga_loaded < 0)
        {
            std::ostringstream err_ostr;
            err_ostr << "Failed to check FPGA state: " << bladerf_strerror(fpga_loaded);
            m_error = err_ostr.str();
            m_dev = 0;
        }
        else if (fpga_loaded == 0)
        {
            m_error = "The device's FPGA is not loaded.";
            m_dev = 0;
        }
        else
        {
            if ((status = bladerf_sync_config(m_dev, BLADERF_MODULE_RX, BLADERF_FORMAT_SC16_Q11, 64, 8192, 32, 10000)) < 0)
            {
                std::ostringstream err_ostr;
                err_ostr << "bladerf_sync_config failed with return code " << status;
                m_error = err_ostr.str();
                m_dev = 0;
            }
            else
            {
                if ((status = bladerf_enable_module(m_dev, BLADERF_MODULE_RX, true)) < 0)
                {
                    std::ostringstream err_ostr;
                    err_ostr << "bladerf_enable_module failed with return code " << status;
                    m_error = err_ostr.str();
                    m_dev = 0;
                }
                else
                {
                    if (bladerf_expansion_attach(m_dev, BLADERF_XB_200) == 0)
                    {
                        std::cerr << "BladeRFSource::BladeRFSource: Attached XB200 extension" << std::endl;

                        if ((status = bladerf_xb200_set_path(m_dev, BLADERF_MODULE_RX, BLADERF_XB200_MIX)) != 0)
                        {
                            std::cerr << "BladeRFSource::BladeRFSource: bladerf_xb200_set_path failed with return code " << status << std::endl;
                        }
                        else
                        {
                            if ((status = bladerf_xb200_set_filterbank(m_dev, BLADERF_MODULE_RX, BLADERF_XB200_AUTO_1DB)) != 0)
                            {
                                std::cerr << "BladeRFSource::BladeRFSource: bladerf_xb200_set_filterbank failed with return code " << status << std::endl;
                            }
                            else
                            {
                                std::cerr << "BladeRFSource::BladeRFSource: XB200 configured. Min freq set to 100kHz" << std::endl;
                                m_minFrequency = 100000;
                            }
                        }
                    }
                }
            }
        }
    }

    std::ostringstream lgains_ostr;

    for (int g: m_lnaGains) {
        lgains_ostr << g << " ";
    }

    m_lnaGainsStr = lgains_ostr.str();

    std::ostringstream v1gains_ostr;

    for (int g: m_vga1Gains) {
        v1gains_ostr << g << " ";
    }

    m_vga1GainsStr = v1gains_ostr.str();

    std::ostringstream v2gains_ostr;

    for (int g: m_vga2Gains) {
        v2gains_ostr << g << " ";
    }

    m_vga2GainsStr = v2gains_ostr.str();

    std::ostringstream bw_ostr;

    for (int b: m_halfbw) {
        bw_ostr << 2*b << " ";
    }

    m_bwfiltStr = bw_ostr.str();

    m_this = this;
}


// Close BladeRF device.
BladeRFSource::~BladeRFSource()
{
    if (m_dev) {
        bladerf_close(m_dev);
    }
        
    m_this = 0;
}

bool BladeRFSource::configure(std::string configurationStr)
{
    namespace qi = boost::spirit::qi;
    std::string::iterator begin = configurationStr.begin();
    std::string::iterator end = configurationStr.end();

    uint32_t sample_rate = 1000000;
    uint32_t frequency = 300000000;
    uint32_t bandwidth = 1500000;
    int lnaGainIndex = 2; // 3 dB
    int vga1Gain = 20;
    int vga2Gain = 9;

    parsekv::key_value_sequence<std::string::iterator> p;
    parsekv::pairs_type m;

    if (!qi::parse(begin, end, p, m))
    {
        m_error = "Configuration parsing failed\n";
        return false;
    }
    else
    {
        if (m.find("srate") != m.end())
        {
            std::cerr << "BladeRFSource::configure: srate: " << m["srate"] << std::endl;
            sample_rate = atoi(m["srate"].c_str());

            if ((sample_rate < 48000) || (sample_rate > 40000000))
            {
                m_error = "Invalid sample rate";
                return false;
            }
        }

        if (m.find("freq") != m.end())
        {
            std::cerr << "BladeRFSource::configure: freq: " << m["freq"] << std::endl;
            frequency = atoi(m["freq"].c_str());

            if ((frequency < m_minFrequency) || (frequency > 3800000000))
            {
                m_error = "Invalid frequency";
                return false;
            }
        }

        if (m.find("bw") != m.end())
        {
            std::cerr << "BladeRFSource::configure: bw: " << m["bw"] << std::endl;

            if (strcasecmp(m["bw"].c_str(), "list") == 0)
            {
                m_error = "Available bandwidths (Hz): " + m_bwfiltStr;
                return false;
            }

            bandwidth = atoi(m["bw"].c_str());

            if (bandwidth < 0)
            {
                m_error = "Invalid bandwidth";
                return false;
            }
        }

        if (m.find("v1gain") != m.end())
        {
            std::cerr << "BladeRFSource::configure: v1gain: " << m["v1gain"] << std::endl;

            if (strcasecmp(m["v1gain"].c_str(), "list") == 0)
            {
                m_error = "Available VGA1 gains (dB): " + m_vga1GainsStr;
                return false;
            }

            vga1Gain = atoi(m["v1gain"].c_str());

            if (find(m_vga1Gains.begin(), m_vga1Gains.end(), vga1Gain) == m_vga1Gains.end())
            {
                m_error = "VGA1 gain not supported. Available gains (dB): " + m_vga1GainsStr;
                return false;
            }
        }

        if (m.find("v2gain") != m.end())
        {
            std::cerr << "BladeRFSource::configure: v2gain: " << m["v2gain"] << std::endl;

            if (strcasecmp(m["v2gain"].c_str(), "list") == 0)
            {
                m_error = "Available VGA2 gains (dB): " + m_vga2GainsStr;
                return false;
            }

            vga1Gain = atoi(m["v2gain"].c_str());

            if (find(m_vga2Gains.begin(), m_vga2Gains.end(), vga2Gain) == m_vga2Gains.end())
            {
                m_error = "VGA2 gain not supported. Available gains (dB): " + m_vga2GainsStr;
                return false;
            }
        }

        if (m.find("lgain") != m.end())
        {
            std::cerr << "BladeRFSource::configure: lgain: " << m["lgain"] << std::endl;

            if (strcasecmp(m["lgain"].c_str(), "list") == 0)
            {
                m_error = "Available LNA gains (dB): " + m_lnaGainsStr;
                return false;
            }

            int lnaGain = atoi(m["lgain"].c_str());
            uint32_t i;

            for (i = 0; i < m_lnaGains.size(); i++)
            {
                if (m_lnaGains[i] == lnaGain)
                {
                    lnaGainIndex = i+1;
                    break;
                }
            }

            if (i == m_lnaGains.size())
            {
                m_error = "Invalid LNA gain";
                return false;
            }
        }

        // Intentionally tune at a higher frequency to avoid DC offset.
        m_confFreq = frequency;
        double tuner_freq = frequency + 0.25 * sample_rate;

        return configure(sample_rate, tuner_freq, bandwidth, lnaGainIndex, vga1Gain, vga2Gain);
    }
}

// Configure RTL-SDR tuner and prepare for streaming.
bool BladeRFSource::configure(uint32_t sample_rate,
        uint32_t frequency,
        uint32_t bandwidth,
        int lna_gainIndex,
        int vga1_gain,
        int vga2_gain)
{
    m_frequency = frequency;
    m_vga1Gain = vga1_gain;
    m_vga2Gain = vga2_gain;
    m_lnaGain = m_lnaGains[lna_gainIndex-1];

    if (bladerf_set_sample_rate(m_dev, BLADERF_MODULE_RX, sample_rate, &m_actualSampleRate) < 0)
    {
        m_error = "Cannot set sample rate";
        return false;
    }

    if (bladerf_set_frequency( m_dev, BLADERF_MODULE_RX, frequency ) != 0)
    {
        m_error = "Cannot set Rx frequency";
        return false;
    }

    if (bladerf_set_bandwidth(m_dev, BLADERF_MODULE_RX, bandwidth, &m_actualBandwidth) < 0)
    {
        m_error = "Cannot set Rx bandwidth";
        return false;
    }

    if (bladerf_set_lna_gain(m_dev, static_cast<bladerf_lna_gain>(lna_gainIndex)) != 0)
    {
        m_error = "Cannot set LNA gain";
        return false;
    }

    if (bladerf_set_rxvga1(m_dev, vga1_gain) != 0)
    {
        m_error = "Cannot set VGA1 gain";
        return false;
    }

    if (bladerf_set_rxvga2(m_dev, vga2_gain) != 0)
    {
        m_error = "Cannot set VGA2 gain";
        return false;
    }

    return true;
}


// Return current sample frequency in Hz.
uint32_t BladeRFSource::get_sample_rate()
{
    return m_actualSampleRate;
}

// Return device current center frequency in Hz.
uint32_t BladeRFSource::get_frequency()
{
    return static_cast<uint32_t>(m_frequency);
}

void BladeRFSource::print_specific_parms()
{
    fprintf(stderr, "Bandwidth:         %d\n", m_actualBandwidth);
    fprintf(stderr, "LNA gain:          %d\n", m_lnaGain);
    fprintf(stderr, "VGA1 gain:         %d\n", m_vga1Gain);
    fprintf(stderr, "VGA2 gain:         %d\n", m_vga2Gain);
}

bool BladeRFSource::start(DataBuffer<IQSample>* buf, std::atomic_bool *stop_flag)
{
    m_buf = buf;
    m_stop_flag = stop_flag;
    
    if (m_thread == 0)
    {
        m_thread = new std::thread(run);
        return true;
    }
    else
    {
        m_error = "Source thread already started";
        return false;
    }
}

bool BladeRFSource::stop()
{
    if (m_thread) 
    {
        m_thread->join();
        delete m_thread;
        m_thread = 0;
    }
    
    return true;
}

void BladeRFSource::run()
{
    IQSampleVector iqsamples;

    while (!m_this->m_stop_flag->load() && get_samples(&iqsamples))
    {
        m_this->m_buf->push(move(iqsamples));
    }
}

// Fetch a bunch of samples from the device.
bool BladeRFSource::get_samples(IQSampleVector *samples)
{
    int res;
    std::vector<int16_t> buf(2*m_blockSize);

    if ((res = bladerf_sync_rx(m_this->m_dev, buf.data(), m_blockSize, 0, 10000)) < 0)
    {
        m_this->m_error = "bladerf_sync_rx failed";
        return false;
    }

    samples->resize(m_blockSize);

    for (int i = 0; i < m_blockSize; i++)
    {
        int32_t re = buf[2*i];
        int32_t im = buf[2*i+1];
        (*samples)[i] = IQSample( re / IQSample::value_type(1<<11),
                                  im / IQSample::value_type(1<<11) );
    }

    return true;
}


// Return a list of supported devices.
void BladeRFSource::get_device_names(std::vector<std::string>& devices)
{
    struct bladerf_devinfo *devinfo = 0;

    int count = bladerf_get_device_list(&devinfo);

    for (int i = 0; i < count; i++)
    {
        devices.push_back(std::string(devinfo[i].serial));
    }

    if (devinfo)
    {
        bladerf_free_device_list(devinfo);
    }
}

/* end */
