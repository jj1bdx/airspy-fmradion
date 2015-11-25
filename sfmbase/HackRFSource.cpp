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

#include "HackRFSource.h"

// Open HackRF device.
HackRFSource::HackRFSource(int dev_index)
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

