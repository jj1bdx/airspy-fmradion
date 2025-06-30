# Installing the latest libfmt

You need to install [{fmt}](https://fmt.dev/) aka libfmt,for supporting C++23-compatible formatted text printing.

## Incompatible versions

*Do not use the following version(s) and older ones*:

* 9.1.0 (stock version for Ubuntu 24.04 LTS)

## Suggested compatible versions

Use the following versions of libfmt:

* [11.2.0](https://github.com/fmtlib/fmt/releases/tag/11.2.0)

## For macOS: use Homebrew

```sh
brew install fmt
```

## For Linux

* See [libfmt README](https://github.com/fmtlib/fmt).
* Install the shared library.

An example sequence:

```sh
mkdir build
cd build
cmake -DBUILD_SHARED_LIBS=TRUE ..
make
make test
sudo zsh
umask 022
make install
exit  
```
 
[End of memorandum]
