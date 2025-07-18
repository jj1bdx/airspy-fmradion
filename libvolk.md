# Installing the latest libvolk

## Incompatible versions

*Do not use the following versions*:

* 3.1.0 (due to NaN problem, use 3.1.1 and later)

## Suggested compatible versions

Use the following versions of VOLK aka libvolk:

* 2.5.2
* 3.1.2
* 3.2.0

## For macOS: use Homebrew

```sh
brew install volk
```

## For Linux

* See [libvolk README.md](https://github.com/gnuradio/volk#readme).
* You need to install python3-mako as: `sudo apt install python3-mako` or `pip3 install mako`
 
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
