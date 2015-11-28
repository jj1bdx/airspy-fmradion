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

#ifndef INCLUDE_FASTATAN2_H_
#define INCLUDE_FASTATAN2_H_

#include <cmath>

// Fast arctan2

inline float fastatan2(float y, float x)
{
    if ( x == 0.0f )
    {
        if ( y > 0.0f )
        {
            return M_PI/2.0f;
        }

        if ( y == 0.0f ) {
            return 0.0f;
        }

        return -M_PI/2.0f;
    }

    float atan;
    float z = y/x;

    if ( fabs( z ) < 1.0f )
    {
        atan = z/(1.0f + 0.277778f*z*z);

        if ( x < 0.0f )
        {
            if ( y < 0.0f )
            {
                return atan - M_PI;
            }

            return atan + M_PI;
        }
    }
    else
    {
        atan = (M_PI/2.0f) - z/(z*z + 0.277778f);

        if ( y < 0.0f )
        {
            return atan - M_PI;
        }
    }

    return atan;
}

#endif /* INCLUDE_FASTATAN2_H_ */
