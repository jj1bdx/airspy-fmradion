# Installing the latest libvolk on macOS and Linux

## For x86_64 macOS: use Homebrew

For libvolk 2.5, the bottled binary works OK with the SIMD acceleration.

```sh
brew install volk
```

## For M1 macOS: build independently, but patch is required

* Use libvolk 2.5, but apply the patch in libvolk-Apple-M1/ directory.
  - copy `libvolk-Apple-M1/patch-cpu_features-add-support-for-ARM64.diff.txt` into `cpu_features` directory
  - apply the patch with `patch -p1`
  - Use the [patched repository](https://github.com/jj1bdx/cpu_features/tree/m1-patched) for the fix
* For M1 Mac, `volk_32f_log2_32f` does not work
* So use the generic setting for the function by rewriting the entry as `volk_32f_log2_32f generic generic`
* Copy `libvolk-Apple-M1/volk_config_M1` as `~/.volk/volk_config`

## Build process change

The latest libvolk requires git submodule files called [cpu\_features](https://github.com/google/cpu_features/). To enable this, run:

```sh
git submodule init
git submodule update --init
```

## Required version

libvolk v2.4 and later.

The software was first tested OK with libvolk v2.1 to v2.3.

## Suggested tools

These tools are not the requirement but may help increasing the execution speed.

### Common tools

* [The Oil Runtime Compiler (orc)](https://github.com/GStreamer/orc)

### macOS

* For Xcode 12.1 and later: use Apple's clang.
* For Xcode 11.x and before: while macOS AppleClang works OK for the generic running environment, full support for Intel instructions such as MMX and AVX require GNU cc `gcc`.

```shell
brew install gcc
brew install orc
```

#### GCC 10 fails test #18 for qa_volk_16ic_x2_dot_prod_16ic

GCC 10 (current version of `brew install gcc`) fails at the test of `qa_volk_16ic_x2_dot_prod_16ic`. GCC 9.3.0 (as `brew install gcc@9`) didn't.

Note as the last resort: if a bug of GCC is unsolvable, use the Xcode clang for the fallback (albeit with the slower code). (Note: homebrew clang is unusable.)

### Ubuntu Linux

* For Linux, gcc is the default compiler.

```shell
sudo apt install liborc-0.4-dev
```

### Raspbian Stretch

* CMake requires OpenSSL.
* You need to compile and install CMake from the scratch. VOLK requires CMake 3.8 and later. See [the installation guide](http://osdevlab.blogspot.com/2015/12/how-to-install-latest-cmake-for.html).
* Stretch has an old GCC 6, so Boost is also required.

```
sudo apt install libssl-dev libboost1.62-all
```

## Commands

```
# Fetch also submodule
git clone --recursive https://github.com/gnuradio/volk libvolk
cd libvolk
# use the master branch
git checkout master
# Fetch git submodule (cpu_features) for the latest libvolk
git submodule init
git submodule update
# Latest libvolk prefers Python 3 to Python 2,
# so you might need to run pip3 first
# install pip3 if needed
pip3 install mako
# install pip if needed and you don't have Python 3 installed
# pip install mako
mkdir build
cd build
# For Xcode 12.1 and later, clang uses SIMD instructions
cmake ..
# Use GNU cc (gcc) for Xcode 11.x and before
# C++ compiler optimization set to default for safe programming
#env CC=/usr/local/opt/gcc/bin/gcc-9 CXX=/usr/local/opt/gcc/bin/g++-9 \
#  cmake ..
# For ARM, use the proper sequence documented in VOLK
# Raspberry Pi 4
# cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchains/arm_cortex_a72_hardfp_native.cmake ..
# Raspberry Pi 3
# cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchains/arm_cortex_a53_hardfp_native.cmake ..
# if build fails, try
# `sudo rm -rf /usr/local/include/volk`
# then try `make` again
#
# For M1 Mac:
# * copy libvolk-Apple-M1/patch-cpu_features-add-support-for-ARM64.diff.txt
#   into cpu_features directory
# * apply the patch with `patch -p1`
# 
make
make test
# For M1 Mac: the test fails on volk_32f_log2_32f
# superuser
sudo zsh
umask 022
make install
exit
# got out of the root shell
```

## After installation

Do the full benchmark by `volk_profile -b -p .`. Use the result at `~/.volk/volk_config` for optimizing the execution.

```shell
volk_profile -b -p .
# For M1 Mac: volk_32f_log2_32f does not work
# So use the generic setting for the function by rewriting the entry as:
#     volk_32f_log2_32f generic generic
#
cp ./volk_config ~/.volk/
```

See `doc/volk_config_data` for the example `volk_config` files.

## Installed files for libvolk v2.4

```
[  1%] Built target unix_based_hardware_detection
[  4%] Built target utils
[  5%] Built target cpu_features
[  7%] Built target list_cpu_features
[ 86%] Built target volk_obj
[ 86%] Built target volk
[ 89%] Built target volk_test_all
[ 93%] Built target volk_profile
[ 96%] Built target volk-config-info
[ 98%] Built target pygen_python_volk_modtool_90adc
[100%] Built target pygen_python_volk_modtool_b0ff2
Install the project...
-- Install configuration: "Release"
-- Installing: /usr/local/lib/pkgconfig/volk.pc
-- Up-to-date: /usr/local/include/volk
-- Up-to-date: /usr/local/include/volk/volk_16ic_s32f_deinterleave_real_32f.h
-- Up-to-date: /usr/local/include/volk/volk_8ic_s32f_deinterleave_real_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_log2_32f.h
-- Up-to-date: /usr/local/include/volk/volk_64f_x2_add_64f.h
-- Up-to-date: /usr/local/include/volk/volk_16i_max_star_horizontal_16i.h
-- Up-to-date: /usr/local/include/volk/volk_16u_byteswap.h
-- Up-to-date: /usr/local/include/volk/volk_16u_byteswappuppet_16u.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_power_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_8u_polarbutterfly_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_magnitude_squared_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_x2_dot_prod_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_dot_prod_16i.h
-- Up-to-date: /usr/local/include/volk/volk_16ic_s32f_deinterleave_32f_x2.h
-- Up-to-date: /usr/local/include/volk/volk_8u_conv_k7_r2puppet_8u.h
-- Up-to-date: /usr/local/include/volk/volk_16i_x4_quad_max_star_16i.h
-- Up-to-date: /usr/local/include/volk/volk_16ic_x2_multiply_16ic.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_32f_add_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32f_binary_slicer_8i.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_conjugate_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_x2_s32fc_multiply_conjugate_add_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32f_asin_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_expfast_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32u_byteswappuppet_32u.h
-- Up-to-date: /usr/local/include/volk/volk_32f_64f_multiply_64f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32f_magnitude_16i.h
-- Installing: /usr/local/include/volk/asm
-- Installing: /usr/local/include/volk/asm/neon
-- Installing: /usr/local/include/volk/asm/orc
-- Up-to-date: /usr/local/include/volk/volk_16i_max_star_16i.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_32f_dot_prod_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_s32f_mod_range_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_sin_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_max_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32i_s32f_convert_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_convert_16i.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_subtract_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32i_x2_or_32i.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_deinterleave_imag_32f.h
-- Up-to-date: /usr/local/include/volk/volk_8ic_deinterleave_real_8i.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_add_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_convert_32i.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x3_sum_of_poly_32f.h
-- Up-to-date: /usr/local/include/volk/volk_16ic_deinterleave_real_16i.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_index_max_32u.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_add_32f.h
-- Up-to-date: /usr/local/include/volk/volk_64u_popcntpuppet_64u.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_x2_add_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_index_max_16u.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_deinterleave_64f_x2.h
-- Up-to-date: /usr/local/include/volk/volk_32f_tanh_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_x2_square_dist_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_interleave_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32i_x2_and_32i.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_x2_divide_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_8ic_deinterleave_real_16i.h
-- Up-to-date: /usr/local/include/volk/volk_32u_byteswap.h
-- Up-to-date: /usr/local/include/volk/volk_8i_s32f_convert_32f.h
-- Up-to-date: /usr/local/include/volk/volk_8u_x4_conv_k7_r2_8u.h
-- Up-to-date: /usr/local/include/volk/volk_16ic_deinterleave_real_8i.h
-- Up-to-date: /usr/local/include/volk/volk_64f_x2_max_64f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_convert_16ic.h
-- Up-to-date: /usr/local/include/volk/volk_16ic_magnitude_16i.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_magnitude_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_invsqrt_32f.h
-- Up-to-date: /usr/local/include/volk/volk_64f_x2_multiply_64f.h
-- Up-to-date: /usr/local/include/volk/volk_8ic_x2_multiply_conjugate_16ic.h
-- Up-to-date: /usr/local/include/volk/volk_32f_64f_add_64f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_multiply_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_deinterleave_real_32f.h
-- Up-to-date: /usr/local/include/volk/volk_8u_x2_encodeframepolar_8u.h
-- Up-to-date: /usr/local/include/volk/volk_16ic_x2_dot_prod_16ic.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_deinterleave_real_64f.h
-- Up-to-date: /usr/local/include/volk/volk_8u_x3_encodepolar_8u_x2.h
-- Up-to-date: /usr/local/include/volk/volk_32u_popcntpuppet_32u.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_fm_detectpuppet_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_pow_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_8u_polarbutterflypuppet_32f.h
-- Up-to-date: /usr/local/include/volk/volk_16i_permute_and_scalar_add.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_x2_s32f_square_dist_scalar_mult_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32f_power_spectrum_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_normalize.h
-- Up-to-date: /usr/local/include/volk/volk_32u_reverse_32u.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_convert_8i.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_x2_multiply_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32f_acos_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_null_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_deinterleave_32f_x2.h
-- Up-to-date: /usr/local/include/volk/volk_16ic_convert_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_16i_convert_8i.h
-- Up-to-date: /usr/local/include/volk/volk_64f_convert_32f.h
-- Up-to-date: /usr/local/include/volk/volk_8i_convert_16i.h
-- Up-to-date: /usr/local/include/volk/volk_32u_popcnt.h
-- Up-to-date: /usr/local/include/volk/volk_16i_x5_add_quad_16i_x4.h
-- Up-to-date: /usr/local/include/volk/volk_32f_cos_32f.h
-- Up-to-date: /usr/local/include/volk/volk_64f_x2_min_64f.h
-- Up-to-date: /usr/local/include/volk/volk_16i_branch_4_state_8.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_stddev_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_convert_64f.h
-- Up-to-date: /usr/local/include/volk/volk_16i_s32f_convert_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32f_x2_power_spectral_density_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_32f_multiply_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_8ic_s32f_deinterleave_32f_x2.h
-- Up-to-date: /usr/local/include/volk/volk_64u_popcnt.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_mod_rangepuppet_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_32f_fm_detect_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_s32f_interleave_16ic.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32fc_rotatorpuppet_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32f_atan2_32f.h
-- Up-to-date: /usr/local/include/volk/volk_16i_32fc_dot_prod_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32f_sqrt_32f.h
-- Up-to-date: /usr/local/include/volk/volk_64u_byteswap.h
-- Up-to-date: /usr/local/include/volk/volk_8ic_deinterleave_16i_x2.h
-- Up-to-date: /usr/local/include/volk/volk_64u_byteswappuppet_64u.h
-- Up-to-date: /usr/local/include/volk/volk_32f_stddev_and_mean_32f_x2.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_multiply_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_index_max_32u.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_x2_multiply_conjugate_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32f_index_max_16u.h
-- Up-to-date: /usr/local/include/volk/volk_32f_tan_32f.h
-- Up-to-date: /usr/local/include/volk/volk_16ic_deinterleave_16i_x2.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_divide_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_dot_prod_32f.h
-- Up-to-date: /usr/local/include/volk/volk_16ic_s32f_magnitude_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_s32f_calc_spectral_noise_floor_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32fc_x2_rotator_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32f_accumulator_s32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_binary_slicer_32i.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32f_power_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32f_deinterleave_real_16i.h
-- Up-to-date: /usr/local/include/volk/volk_8ic_x2_s32f_multiply_conjugate_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32f_atan_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32f_exp_32f.h
-- Up-to-date: /usr/local/include/volk/volk_8u_x3_encodepolarpuppet_8u.h
-- Up-to-date: /usr/local/include/volk/volk_32f_x2_min_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32f_power_spectral_densitypuppet_32f.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_x2_conjugate_dot_prod_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_32fc_s32fc_multiply_32fc.h
-- Up-to-date: /usr/local/include/volk/volk_prefs.h
-- Up-to-date: /usr/local/include/volk/volk_alloc.hh
-- Up-to-date: /usr/local/include/volk/volk_complex.h
-- Up-to-date: /usr/local/include/volk/volk_common.h
-- Up-to-date: /usr/local/include/volk/saturation_arithmetic.h
-- Up-to-date: /usr/local/include/volk/volk_avx_intrinsics.h
-- Up-to-date: /usr/local/include/volk/volk_avx2_intrinsics.h
-- Up-to-date: /usr/local/include/volk/volk_sse_intrinsics.h
-- Up-to-date: /usr/local/include/volk/volk_sse3_intrinsics.h
-- Up-to-date: /usr/local/include/volk/volk_neon_intrinsics.h
-- Installing: /usr/local/include/volk/volk.h
-- Installing: /usr/local/include/volk/volk_cpu.h
-- Installing: /usr/local/include/volk/volk_config_fixed.h
-- Installing: /usr/local/include/volk/volk_typedefs.h
-- Up-to-date: /usr/local/include/volk/volk_malloc.h
-- Installing: /usr/local/include/volk/volk_version.h
-- Up-to-date: /usr/local/include/volk/constants.h
-- Installing: /usr/local/lib/cmake/volk/VolkConfig.cmake
-- Installing: /usr/local/lib/cmake/volk/VolkConfigVersion.cmake
-- Installing: /usr/local/lib/cmake/volk/VolkTargets.cmake
-- Installing: /usr/local/lib/cmake/volk/VolkTargets-release.cmake
-- Installing: /usr/local/lib/libcpu_features.a
-- Installing: /usr/local/include/cpu_features/cpu_features_macros.h
-- Installing: /usr/local/include/cpu_features/cpu_features_cache_info.h
-- Installing: /usr/local/include/cpu_features/cpuinfo_x86.h
-- Installing: /usr/local/bin/list_cpu_features
-- Installing: /usr/local/lib/cmake/CpuFeatures/CpuFeaturesTargets.cmake
-- Installing: /usr/local/lib/cmake/CpuFeatures/CpuFeaturesTargets-release.cmake
-- Installing: /usr/local/lib/cmake/CpuFeatures/CpuFeaturesConfig.cmake
-- Installing: /usr/local/lib/cmake/CpuFeatures/CpuFeaturesConfigVersion.cmake
-- Installing: /usr/local/lib/libvolk.2.4.dylib
-- Installing: /usr/local/lib/libvolk.dylib
-- Installing: /usr/local/bin/volk_profile
-- Installing: /usr/local/bin/volk-config-info
-- Up-to-date: /usr/local/lib/python3.9/site-packages/volk_modtool/__init__.py
-- Up-to-date: /usr/local/lib/python3.9/site-packages/volk_modtool/cfg.py
-- Up-to-date: /usr/local/lib/python3.9/site-packages/volk_modtool/volk_modtool_generate.py
-- Installing: /usr/local/lib/python3.9/site-packages/volk_modtool/__init__.pyc
-- Installing: /usr/local/lib/python3.9/site-packages/volk_modtool/cfg.pyc
-- Installing: /usr/local/lib/python3.9/site-packages/volk_modtool/volk_modtool_generate.pyc
-- Installing: /usr/local/lib/python3.9/site-packages/volk_modtool/__init__.pyo
-- Installing: /usr/local/lib/python3.9/site-packages/volk_modtool/cfg.pyo
-- Installing: /usr/local/lib/python3.9/site-packages/volk_modtool/volk_modtool_generate.pyo
-- Installing: /usr/local/bin/volk_modtool
```
