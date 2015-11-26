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

#include <cstring>
#include <sstream>
#include <iostream>
#include <iomanip>

#include "HackRFSource.h"

// Open HackRF device.
HackRFSource::HackRFSource(int dev_index) :
    m_dev(0),
	m_sampleRate(5000000),
	m_frequency(100000000),
	m_lnaGain(16),
	m_vgaGain(22),
	m_bandwidth(2500000),
	m_extAmp(false),
	m_biasAnt(false)
{
	hackrf_device_list_t *hackrf_devices = hackrf_device_list();
	hackrf_error rc;

	rc = (hackrf_error) hackrf_device_list_open(hackrf_devices, dev_index, &m_dev);

	if (rc != HACKRF_SUCCESS)
	{
		const char *errmsg = hackrf_error_name(rc);
		m_error = "Failed to open HackRF device (";
		m_error += std::string(errmsg);
		m_error += ")";
		m_dev = 0;
	}
}

HackRFSource::~HackRFSource()
{}

void HackRFSource::get_device_names(std::vector<std::string>& devices)
{
	hackrf_device_list_t *hackrf_devices = hackrf_device_list();
	hackrf_device *hackrf_ptr;
	read_partid_serialno_t read_partid_serialno;
	hackrf_error rc;
	int i;

	devices.clear();
	rc = (hackrf_error) hackrf_init();

	if (rc != HACKRF_SUCCESS)
	{
		return;
	}

	for (i=0; i < hackrf_devices->devicecount; i++)
	{
		rc = (hackrf_error) hackrf_device_list_open(hackrf_devices, i, &hackrf_ptr);

		if (rc == HACKRF_SUCCESS)
		{
			rc = (hackrf_error) hackrf_board_partid_serialno_read(hackrf_ptr, &read_partid_serialno);

			if (rc != HACKRF_SUCCESS)
			{
				hackrf_close(hackrf_ptr);
				continue;
			}

			uint32_t serial_msb = read_partid_serialno.serial_no[2];
			uint32_t serial_lsb = read_partid_serialno.serial_no[3];
			std::ostringstream devname_ostr;

			devname_ostr << "Serial " << serial_msb << ":" << serial_lsb;
			devices.push_back(devname_ostr.str());
		}
	}
}

std::uint32_t HackRFSource::get_sample_rate()
{
	return m_sampleRate;
}

std::uint32_t HackRFSource::get_frequency()
{
	return m_frequency;
}

void HackRFSource::print_specific_parms()
{
    fprintf(stderr, "LNA gain:          %d\n", m_lnaGain);
    fprintf(stderr, "VGA gain:          %d\n", m_vgaGain);
    fprintf(stderr, "Bandwidth          %d\n", m_bandwidth);
    fprintf(stderr, "External Amp       %s\n", m_extAmp ? "enabled" : "disabled");
    fprintf(stderr, "Bias ant           %s\n", m_biasAnt ? "enabled" : "disabled");
}

bool HackRFSource::configure(uint32_t sample_rate,
        uint32_t frequency,
        bool ext_amp,
		bool bias_ant,
        int lna_gain,
        int vga_gain,
		uint32_t bandwidth
)
{
	m_sampleRate = sample_rate;
	m_frequency = frequency;
	m_extAmp = ext_amp;
	m_biasAnt = bias_ant;
	m_lnaGain = lna_gain;
	m_vgaGain = vga_gain;
	m_bandwidth = bandwidth;
	hackrf_error rc;

    if (!m_dev) {
        return false;
    }

	rc = (hackrf_error) hackrf_set_freq(m_dev, static_cast<uint64_t>(m_frequency));

	if (rc != HACKRF_SUCCESS)
	{
		std::ostringstream err_ostr;
		err_ostr << "Could not set center frequency to " << m_frequency << " Hz";
		m_error = err_ostr.str();
		return false;
	}

	rc = (hackrf_error) hackrf_set_sample_rate_manual(m_dev, m_sampleRate, 1);

	if (rc != HACKRF_SUCCESS)
	{
		std::ostringstream err_ostr;
		err_ostr << "Could not set center sample rate to " << m_sampleRate << " Hz";
		m_error = err_ostr.str();
		return false;
	}

	rc = (hackrf_error) hackrf_set_lna_gain(m_dev, m_lnaGain);

	if (rc != HACKRF_SUCCESS)
	{
		std::ostringstream err_ostr;
		err_ostr << "Could not set LNA gain to " << m_lnaGain << " dB";
		m_error = err_ostr.str();
		return false;
	}

	rc = (hackrf_error) hackrf_set_vga_gain(m_dev, m_vgaGain);

	if (rc != HACKRF_SUCCESS)
	{
		std::ostringstream err_ostr;
		err_ostr << "Could not set VGA gain to " << m_vgaGain << " dB";
		m_error = err_ostr.str();
		return false;
	}

	rc = (hackrf_error) hackrf_set_antenna_enable(m_dev, (m_biasAnt ? 1 : 0));

	if (rc != HACKRF_SUCCESS)
	{
		std::ostringstream err_ostr;
		err_ostr << "Could not set bias antenna to " << m_biasAnt;
		m_error = err_ostr.str();
		return false;
	}

	rc = (hackrf_error) hackrf_set_amp_enable(m_dev, (m_extAmp ? 1 : 0));

	if (rc != HACKRF_SUCCESS)
	{
		std::ostringstream err_ostr;
		err_ostr << "Could not set extra amplifier to " << m_extAmp;
		m_error = err_ostr.str();
		return false;
	}

	uint32_t hackRFBandwidth = hackrf_compute_baseband_filter_bw_round_down_lt(m_bandwidth);
	rc = (hackrf_error) hackrf_set_baseband_filter_bandwidth(m_dev, hackRFBandwidth);

	if (rc != HACKRF_SUCCESS)
	{
		std::ostringstream err_ostr;
		err_ostr << "Could not set bandwidth to " << hackRFBandwidth << " Hz (" << m_bandwidth << " Hz requested)";
		m_error = err_ostr.str();
		return false;
	}

	return true;
}
