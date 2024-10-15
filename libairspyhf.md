# libairspyhf and Airspy HF+ firmware versions

## Airspy HF+ firmware version details

* See [Airspy HF+ Firmware changelog](https://airspy.com/downloads/hfplus_changelog.txt) for the details.

## libairspy version compatibility with Airspy HF+ firmware versions

* R3.0.7 and R4.0.8 both work OK on libairspyhf 1.6.8.
* For R4.0.8, use libairspy 1.8 to have full compatibility.
* R5.0.0 on libairspyhf 1.6.8 is *not tested*.
* For R5.0.0, use libairspy 1.8 to have full compatibility.

## For macOS Homebrew

To install the latest libairspyhf:

```sh
brew install airspyhf --HEAD

```

## pkgconfig precedence

You may need to add precedence of `/usr/local/lib/pkg-config` for `PKG_CONFIG_PATH` when building airspy-fmradion, as:

```
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:${PKG_CONFIG_PATH}"
```

## libairspyhf 1.8 udev rules for Linux

You have to modify `/etc/udev/rules.d/52-airspyhf.rules` as:

```
ATTR{idVendor}=="03eb", ATTR{idProduct}=="800c", SYMLINK+="airspyhf-%k", TAG+="uaccess", MODE="660", GROUP="plugdev"
```

Otherwise Airspy HF+ device will not be recognized when plugged in.

See [GitHub issue airspy/airspyhf#46](https://github.com/airspy/airspyhf/issues/46) for the details.

[End of memorandum]
