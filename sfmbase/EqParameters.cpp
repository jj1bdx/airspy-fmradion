// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2019 Kenji Rikitake, JJ1BDX
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "EqParameters.h"

// Class for calculating DiscriminatorEqualizer parameters
// based on pre-calculated tables with Boost Cubic B Spline interpolation.

// Constructor.
// Do nothing but initializing the private member variables.
EqParameters::EqParameters()
    : m_freq_initial(100000.0), m_freq_step(10000.0),
      m_vector_staticgain({
          1.5408838635599964, // 100000.0 Hz
          1.5004017717728035, // 110000.0 Hz
          1.471118672900754,  // 120000.0 Hz
          1.4491542211922221, // 130000.0 Hz
          1.432158472341712,  // 140000.0 Hz
          1.4187425067014776, // 150000.0 Hz
          1.4079776152646817, // 160000.0 Hz
          1.3990991264422705, // 170000.0 Hz
          1.3917456897011489, // 180000.0 Hz
          1.3855406732463762, // 190000.0 Hz
          1.380366072307969,  // 200000.0 Hz
          1.375885901990457,  // 210000.0 Hz
          1.3720177308068577, // 220000.0 Hz
          1.3686918737445442, // 230000.0 Hz
          1.3657048408532624, // 240000.0 Hz
          1.3631510167499297, // 250000.0 Hz
          1.360886954019956,  // 260000.0 Hz
          1.3588108049472662, // 270000.0 Hz
          1.3569942518705387, // 280000.0 Hz
          1.355386342137693,  // 290000.0 Hz
          1.3539733513544607, // 300000.0 Hz
          1.352562136209338,  // 310000.0 Hz
          1.3513763138129997, // 320000.0 Hz
          1.3503245294289734, // 330000.0 Hz
          1.34934142807926,   // 340000.0 Hz
          1.348439218014875,  // 350000.0 Hz
          1.3475785381520182, // 360000.0 Hz
          1.3468022692533563, // 370000.0 Hz
          1.3461170716614352, // 380000.0 Hz
          1.3454793350153613, // 390000.0 Hz
          1.3448991505750953, // 400000.0 Hz
          1.3443243976945485, // 410000.0 Hz
          1.343825662431628,  // 420000.0 Hz
          1.3433514039539838, // 430000.0 Hz
          1.3429123629325925, // 440000.0 Hz
          1.3424980099361972, // 450000.0 Hz
          1.3419421075557407, // 460000.0 Hz
          1.3416516887920007, // 470000.0 Hz
          1.3412951207114865, // 480000.0 Hz
          1.3410220954497123, // 490000.0 Hz
          1.3407556134526948, // 500000.0 Hz
      }),
      m_vector_fitlevel({
          0.5711387820919492,  // 100000.0 Hz
          0.5210719504091612,  // 110000.0 Hz
          0.48570203095904574, // 120000.0 Hz
          0.4597197588221378,  // 130000.0 Hz
          0.439997534299136,   // 140000.0 Hz
          0.42467500898335375, // 150000.0 Hz
          0.4125645347965949,  // 160000.0 Hz
          0.4026758759993564,  // 170000.0 Hz
          0.39459046220004196, // 180000.0 Hz
          0.3878284300421786,  // 190000.0 Hz
          0.382226567827996,   // 200000.0 Hz
          0.3774269762693636,  // 210000.0 Hz
          0.3732958514966557,  // 220000.0 Hz
          0.36975999885718236, // 230000.0 Hz
          0.3665966984587189,  // 240000.0 Hz
          0.3639160252867684,  // 250000.0 Hz
          0.3615410043213263,  // 260000.0 Hz
          0.3593717049381185,  // 270000.0 Hz
          0.3574760893264268,  // 280000.0 Hz
          0.3558046718166551,  // 290000.0 Hz
          0.35433997901465075, // 300000.0 Hz
          0.3528810075192016,  // 310000.0 Hz
          0.3516571852564142,  // 320000.0 Hz
          0.350576903137199,   // 330000.0 Hz
          0.3495620117127483,  // 340000.0 Hz
          0.3486359716148759,  // 350000.0 Hz
          0.3477558096096479,  // 360000.0 Hz
          0.34696263271037636, // 370000.0 Hz
          0.34626223591338534, // 380000.0 Hz
          0.3456051006456834,  // 390000.0 Hz
          0.3450174353355666,  // 400000.0 Hz
          0.34443016081180344, // 410000.0 Hz
          0.3439224406766675,  // 420000.0 Hz
          0.34343858461840504, // 430000.0 Hz
          0.3429925115681256,  // 440000.0 Hz
          0.34257293547112655, // 450000.0 Hz
          0.3420081524511543,  // 460000.0 Hz
          0.34171078528861276, // 470000.0 Hz
          0.3413487132091231,  // 480000.0 Hz
          0.3410742496846445,  // 490000.0 Hz
          0.3408038043449122,  // 500000.0 Hz
      }),
      m_staticgain(boost::math::cubic_b_spline<double>(
          m_vector_staticgain.data(), m_vector_staticgain.size(),
          m_freq_initial, m_freq_step)),
      m_fitlevel(boost::math::cubic_b_spline<double>(
          m_vector_fitlevel.data(), m_vector_fitlevel.size(), m_freq_initial,
          m_freq_step))
// Do nothing in the constructor.
{}

// Private function to decide whether to apply interpolation or not.
// The interpolation will be applied when ifrate is
// between 200kHz to 1MHz only.
// Table frequency (Nyquist frequency): 100kHz to 500kHz.
const double
EqParameters::fitting(double ifrate, double low_limit, double high_limit,
                      const boost::math::cubic_b_spline<double> &spline) {
  if (ifrate < 200000.0) {
    return low_limit;
  } else if (ifrate > 1000000.0) {
    return high_limit;
  } else {
    return spline(ifrate / 2.0);
  }
};

// Compute staticgain from ifrate.
const double EqParameters::compute_staticgain(const double ifrate) {
  return EqParameters::fitting(ifrate, 1.541, 1.33338, m_staticgain);
}

// Compute fitlevel from ifrate.
const double EqParameters::compute_fitlevel(const double ifrate) {
  return EqParameters::fitting(ifrate, 0.572, 0.33338, m_fitlevel);
};

// end
