# Installing the latest libvolk on macOS and Linux

## Required version

Use libvolk 2.5.2.

Note: libvolk 2.1 to 2.5.1 will work without issues, but no guarantee.

## For x86_64 and M1 macOS: use Homebrew

For libvolk 2.5.2, the bottled binary of x86\_64 works OK with the SIMD acceleration, and the bottled M1 binary detects neon capability.

```sh
brew install volk
```

## For Linux

* For Ubuntu 22.04.3 LTS, *libvolk 3.1.0 does not work.* Use 3.0.0 or 2.5.1 in the apt repository.
* See [libvolk README.md](https://github.com/gnuradio/volk#readme).
 
## After installation

Do the full benchmark by `volk_profile -b -p .`. Use the result at `~/.volk/volk_config` for optimizing the execution.

```shell
volk_profile -b -p .
cp ./volk_config ~/.volk/
```

See `doc/volk_config_data` for the example `volk_config` files.

## For your own build of libvolk

See `jj1bdx-dev` branch of [jj1bdx fork of libvolk](https://github.com/jj1bdx/libvolk).

[End of memorandum]
