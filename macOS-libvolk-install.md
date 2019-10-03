# Installing libvolk on macOS

## Commands

```
git clone https://github.com/gnuradio/volk
mv volk libvolk
cd libvolk
pip install mako
mkdir build
cd build
cmake ..
make
make test
# superuser
sudo zsh
umask 022
make install
exit
# got out of the root shell
```

## Installed files

```
[ 72%] Built target volk_obj
[ 74%] Built target volk
[ 80%] Built target volk_test_all
[ 88%] Built target volk_profile
[ 94%] Built target volk-config-info
[ 96%] Built target pygen_python_volk_modtool_d5caf
[100%] Built target pygen_python_volk_modtool_2b366
Install the project...
-- Install configuration: "Release"
-- Installing: /usr/local/lib/pkgconfig/volk.pc
-- Installing: /usr/local/include/volk
-- Installing: /usr/local/include/volk/volk_16ic_s32f_deinterleave_real_32f.h
-- Installing: /usr/local/include/volk/volk_8ic_s32f_deinterleave_real_32f.h
-- Installing: /usr/local/include/volk/volk_32f_log2_32f.h
-- Installing: /usr/local/include/volk/volk_64f_x2_add_64f.h
-- Installing: /usr/local/include/volk/volk_16i_max_star_horizontal_16i.h
-- Installing: /usr/local/include/volk/volk_16u_byteswap.h
-- Installing: /usr/local/include/volk/volk_16u_byteswappuppet_16u.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_power_32f.h
-- Installing: /usr/local/include/volk/volk_32f_8u_polarbutterfly_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_magnitude_squared_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_x2_dot_prod_32fc.h
-- Installing: /usr/local/include/volk/volk_32f_x2_dot_prod_16i.h
-- Installing: /usr/local/include/volk/volk_16ic_s32f_deinterleave_32f_x2.h
-- Installing: /usr/local/include/volk/volk_8u_conv_k7_r2puppet_8u.h
-- Installing: /usr/local/include/volk/volk_16i_x4_quad_max_star_16i.h
-- Installing: /usr/local/include/volk/volk_16ic_x2_multiply_16ic.h
-- Installing: /usr/local/include/volk/volk_32fc_32f_add_32fc.h
-- Installing: /usr/local/include/volk/volk_32f_binary_slicer_8i.h
-- Installing: /usr/local/include/volk/volk_32fc_conjugate_32fc.h
-- Installing: /usr/local/include/volk/volk_32f_asin_32f.h
-- Installing: /usr/local/include/volk/volk_32f_expfast_32f.h
-- Installing: /usr/local/include/volk/volk_32u_byteswappuppet_32u.h
-- Installing: /usr/local/include/volk/volk_32f_64f_multiply_64f.h
-- Installing: /usr/local/include/volk/volk_32fc_s32f_magnitude_16i.h
-- Installing: /usr/local/include/volk/asm
-- Installing: /usr/local/include/volk/asm/neon
-- Installing: /usr/local/include/volk/asm/orc
-- Installing: /usr/local/include/volk/volk_16i_max_star_16i.h
-- Installing: /usr/local/include/volk/volk_32fc_32f_dot_prod_32fc.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_s32f_mod_range_32f.h
-- Installing: /usr/local/include/volk/volk_32f_sin_32f.h
-- Installing: /usr/local/include/volk/volk_32f_x2_max_32f.h
-- Installing: /usr/local/include/volk/volk_32i_s32f_convert_32f.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_convert_16i.h
-- Installing: /usr/local/include/volk/volk_32f_x2_subtract_32f.h
-- Installing: /usr/local/include/volk/volk_32i_x2_or_32i.h
-- Installing: /usr/local/include/volk/volk_32fc_deinterleave_imag_32f.h
-- Installing: /usr/local/include/volk/volk_8ic_deinterleave_real_8i.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_convert_32i.h
-- Installing: /usr/local/include/volk/volk_32f_x3_sum_of_poly_32f.h
-- Installing: /usr/local/include/volk/volk_16ic_deinterleave_real_16i.h
-- Installing: /usr/local/include/volk/volk_32fc_index_max_32u.h
-- Installing: /usr/local/include/volk/volk_32f_x2_add_32f.h
-- Installing: /usr/local/include/volk/volk_64u_popcntpuppet_64u.h
-- Installing: /usr/local/include/volk/volk_32fc_x2_add_32fc.h
-- Installing: /usr/local/include/volk/volk_32fc_index_max_16u.h
-- Installing: /usr/local/include/volk/volk_32fc_deinterleave_64f_x2.h
-- Installing: /usr/local/include/volk/volk_32f_tanh_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_x2_square_dist_32f.h
-- Installing: /usr/local/include/volk/volk_32f_x2_interleave_32fc.h
-- Installing: /usr/local/include/volk/volk_32i_x2_and_32i.h
-- Installing: /usr/local/include/volk/volk_32fc_x2_divide_32fc.h
-- Installing: /usr/local/include/volk/volk_8ic_deinterleave_real_16i.h
-- Installing: /usr/local/include/volk/volk_32u_byteswap.h
-- Installing: /usr/local/include/volk/volk_8i_s32f_convert_32f.h
-- Installing: /usr/local/include/volk/volk_8u_x4_conv_k7_r2_8u.h
-- Installing: /usr/local/include/volk/volk_16ic_deinterleave_real_8i.h
-- Installing: /usr/local/include/volk/volk_64f_x2_max_64f.h
-- Installing: /usr/local/include/volk/volk_32fc_convert_16ic.h
-- Installing: /usr/local/include/volk/volk_16ic_magnitude_16i.h
-- Installing: /usr/local/include/volk/volk_32fc_magnitude_32f.h
-- Installing: /usr/local/include/volk/volk_32f_invsqrt_32f.h
-- Installing: /usr/local/include/volk/volk_64f_x2_multiply_64f.h
-- Installing: /usr/local/include/volk/volk_8ic_x2_multiply_conjugate_16ic.h
-- Installing: /usr/local/include/volk/volk_32f_64f_add_64f.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_multiply_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_deinterleave_real_32f.h
-- Installing: /usr/local/include/volk/volk_8u_x2_encodeframepolar_8u.h
-- Installing: /usr/local/include/volk/volk_16ic_x2_dot_prod_16ic.h
-- Installing: /usr/local/include/volk/volk_32fc_deinterleave_real_64f.h
-- Installing: /usr/local/include/volk/volk_8u_x3_encodepolar_8u_x2.h
-- Installing: /usr/local/include/volk/volk_32u_popcntpuppet_32u.h
-- Installing: /usr/local/include/volk/volk_32f_x2_fm_detectpuppet_32f.h
-- Installing: /usr/local/include/volk/volk_32f_x2_pow_32f.h
-- Installing: /usr/local/include/volk/volk_32f_8u_polarbutterflypuppet_32f.h
-- Installing: /usr/local/include/volk/volk_16i_permute_and_scalar_add.h
-- Installing: /usr/local/include/volk/volk_32fc_x2_s32f_square_dist_scalar_mult_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_s32f_power_spectrum_32f.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_normalize.h
-- Installing: /usr/local/include/volk/volk_32u_reverse_32u.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_convert_8i.h
-- Installing: /usr/local/include/volk/volk_32fc_x2_multiply_32fc.h
-- Installing: /usr/local/include/volk/volk_32f_acos_32f.h
-- Installing: /usr/local/include/volk/volk_32f_null_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_deinterleave_32f_x2.h
-- Installing: /usr/local/include/volk/volk_16ic_convert_32fc.h
-- Installing: /usr/local/include/volk/volk_16i_convert_8i.h
-- Installing: /usr/local/include/volk/volk_64f_convert_32f.h
-- Installing: /usr/local/include/volk/volk_8i_convert_16i.h
-- Installing: /usr/local/include/volk/volk_32u_popcnt.h
-- Installing: /usr/local/include/volk/volk_16i_x5_add_quad_16i_x4.h
-- Installing: /usr/local/include/volk/volk_32f_cos_32f.h
-- Installing: /usr/local/include/volk/volk_64f_x2_min_64f.h
-- Installing: /usr/local/include/volk/volk_16i_branch_4_state_8.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_stddev_32f.h
-- Installing: /usr/local/include/volk/volk_32f_convert_64f.h
-- Installing: /usr/local/include/volk/volk_16i_s32f_convert_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_s32f_x2_power_spectral_density_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_32f_multiply_32fc.h
-- Installing: /usr/local/include/volk/volk_8ic_s32f_deinterleave_32f_x2.h
-- Installing: /usr/local/include/volk/volk_64u_popcnt.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_mod_rangepuppet_32f.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_32f_fm_detect_32f.h
-- Installing: /usr/local/include/volk/volk_32f_x2_s32f_interleave_16ic.h
-- Installing: /usr/local/include/volk/volk_32fc_s32fc_rotatorpuppet_32fc.h
-- Installing: /usr/local/include/volk/volk_32fc_s32f_atan2_32f.h
-- Installing: /usr/local/include/volk/volk_16i_32fc_dot_prod_32fc.h
-- Installing: /usr/local/include/volk/volk_32f_sqrt_32f.h
-- Installing: /usr/local/include/volk/volk_64u_byteswap.h
-- Installing: /usr/local/include/volk/volk_8ic_deinterleave_16i_x2.h
-- Installing: /usr/local/include/volk/volk_64u_byteswappuppet_64u.h
-- Installing: /usr/local/include/volk/volk_32f_stddev_and_mean_32f_x2.h
-- Installing: /usr/local/include/volk/volk_32f_x2_multiply_32f.h
-- Installing: /usr/local/include/volk/volk_32f_index_max_32u.h
-- Installing: /usr/local/include/volk/volk_32fc_x2_multiply_conjugate_32fc.h
-- Installing: /usr/local/include/volk/volk_32f_index_max_16u.h
-- Installing: /usr/local/include/volk/volk_32f_tan_32f.h
-- Installing: /usr/local/include/volk/volk_16ic_deinterleave_16i_x2.h
-- Installing: /usr/local/include/volk/volk_32f_x2_divide_32f.h
-- Installing: /usr/local/include/volk/volk_32f_x2_dot_prod_32f.h
-- Installing: /usr/local/include/volk/volk_16ic_s32f_magnitude_32f.h
-- Installing: /usr/local/include/volk/volk_32f_s32f_calc_spectral_noise_floor_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_s32fc_x2_rotator_32fc.h
-- Installing: /usr/local/include/volk/volk_32f_accumulator_s32f.h
-- Installing: /usr/local/include/volk/volk_32f_binary_slicer_32i.h
-- Installing: /usr/local/include/volk/volk_32fc_s32f_power_32fc.h
-- Installing: /usr/local/include/volk/volk_32fc_s32f_deinterleave_real_16i.h
-- Installing: /usr/local/include/volk/volk_8ic_x2_s32f_multiply_conjugate_32fc.h
-- Installing: /usr/local/include/volk/volk_32f_atan_32f.h
-- Installing: /usr/local/include/volk/volk_8u_x3_encodepolarpuppet_8u.h
-- Installing: /usr/local/include/volk/volk_32f_x2_min_32f.h
-- Installing: /usr/local/include/volk/volk_32fc_x2_conjugate_dot_prod_32fc.h
-- Installing: /usr/local/include/volk/volk_32fc_s32fc_multiply_32fc.h
-- Installing: /usr/local/include/volk/volk_prefs.h
-- Installing: /usr/local/include/volk/volk_complex.h
-- Installing: /usr/local/include/volk/volk_common.h
-- Installing: /usr/local/include/volk/saturation_arithmetic.h
-- Installing: /usr/local/include/volk/volk_avx_intrinsics.h
-- Installing: /usr/local/include/volk/volk_avx2_intrinsics.h
-- Installing: /usr/local/include/volk/volk_sse_intrinsics.h
-- Installing: /usr/local/include/volk/volk_sse3_intrinsics.h
-- Installing: /usr/local/include/volk/volk_neon_intrinsics.h
-- Installing: /usr/local/include/volk/volk.h
-- Installing: /usr/local/include/volk/volk_cpu.h
-- Installing: /usr/local/include/volk/volk_config_fixed.h
-- Installing: /usr/local/include/volk/volk_typedefs.h
-- Installing: /usr/local/include/volk/volk_malloc.h
-- Installing: /usr/local/include/volk/constants.h
-- Installing: /usr/local/lib/cmake/volk/VolkConfig.cmake
-- Installing: /usr/local/lib/cmake/volk/VolkConfigVersion.cmake
-- Installing: /usr/local/lib/cmake/volk/VolkTargets.cmake
-- Installing: /usr/local/lib/cmake/volk/VolkTargets-release.cmake
-- Installing: /usr/local/lib/libvolk.2.0.dylib
-- Installing: /usr/local/lib/libvolk.dylib
-- Installing: /usr/local/bin/volk_profile
-- Installing: /usr/local/bin/volk-config-info
-- Installing: /usr/local/lib/python2.7/site-packages/volk_modtool/__init__.py
-- Installing: /usr/local/lib/python2.7/site-packages/volk_modtool/cfg.py
-- Installing: /usr/local/lib/python2.7/site-packages/volk_modtool/volk_modtool_generate.py
-- Installing: /usr/local/lib/python2.7/site-packages/volk_modtool/__init__.pyc
-- Installing: /usr/local/lib/python2.7/site-packages/volk_modtool/cfg.pyc
-- Installing: /usr/local/lib/python2.7/site-packages/volk_modtool/volk_modtool_generate.pyc
-- Installing: /usr/local/lib/python2.7/site-packages/volk_modtool/__init__.pyo
-- Installing: /usr/local/lib/python2.7/site-packages/volk_modtool/cfg.pyo
-- Installing: /usr/local/lib/python2.7/site-packages/volk_modtool/volk_modtool_generate.pyo
-- Installing: /usr/local/bin/volk_modtool

```
