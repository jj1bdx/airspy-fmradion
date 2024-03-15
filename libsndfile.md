# Installing the latest libsndfile

You need to install the latest libsndfile for using the MP3 output file capability of airspy-fmradion.

## MP3 capability

* libsndfile 1.1 and later is MP3-capable when LAME is installed.
* libsndfile 1.0.31, the stock version for Ubuntu 22.04.4 LTS, *does not have* MP3 handling capability.

## For macOS: use Homebrew

```sh
brew install libsndfile
```

## Installing the latest libsndfile on Linux

### For Ubuntu 22.04.4 LTS

You need to install at least the related MP3 library:

```sh
sudo apt install lame libmp3lame-dev
```

Use the following cmake command in the `CMakeBuild/` directory:

```sh
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DBUILD_SHARED_LIBS=ON \
      -DBUILD_TESTING=ON
```

[End of memorandum]
