# Installation


## Ubuntu 16.04

make
cd bin
./linapple

### Prerequisites

#### Ubuntu

```bash
sudo apt-get install git libzip-dev libsdl1.2-dev libsdl-image1.2-dev libcurl4-openssl-dev zlib1g-dev
```

#### Fedora

```bash
sudo dnf install git SDL-devel SDL_image-devel libcurl-devel libzip-devel
```

### Clone

```bash
git clone git@github.com:linappleii/linapple.git 
```
Linapple - crossplatfom emulator of Apple][ (Apple2, Apple 2) series computer for Linux or other OSes with SDL support.


### Compile

```bash
cd linapple
make
```

### Global Install
```shell
make install
```

### Run

```bash
bin/linapple
```

Or if you did a global install.

```bash
linapple
```

### Configure

A directory name `linapple` can be found in your home directory. Edit the `linapple.conf` file.


### Debugging and Profiling

By default, the `make` command will compile an optimized version of `linapple`.

It is possible to compile a version with debugging symbols. To do so, you must
set the `DEBUG` environment variable:

    DEBUG=1 make

If you would like to also include extra code that writes profile information
suitable for the analysis program `gprof`:

    PROFILING=1 make

