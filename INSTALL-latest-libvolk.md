# Installing the latest libvolk on macOS and Linux

## Required version

Use libvolk 2.5.1.

Note: libvolk 2.1 to 2.5.0 will work without issues, but no guarantee.

## For x86_64 and M1 macOS: use Homebrew

For libvolk 2.5.1, the bottled binary of x86\_64 works OK with the SIMD acceleration, and the bottled M1 binary detects neon capability.

```sh
brew install volk
```

## For Linux

See [libvolk README.md](https://github.com/gnuradio/volk#readme).

## After installation

Do the full benchmark by `volk_profile -b -p .`. Use the result at `~/.volk/volk_config` for optimizing the execution.

```shell
volk_profile -b -p .
cp ./volk_config ~/.volk/
```

See `doc/volk_config_data` for the example `volk_config` files.

[End of memorandum]
