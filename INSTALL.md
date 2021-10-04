# Installation


### Prerequisites

#### Ubuntu

```bash
sudo apt-get install git libzip-dev libsdl1.2-dev libsdl-image1.2-dev libcurl4-openssl-dev zlib1g-dev
```

#### Fedora

```bash
sudo dnf install git SDL-devel SDL_image-devel libcurl-devel libzip-devel
ImageMagick
```

### Clone

```bash
git clone https://github.com/linappleii/linapple.git
```

### Compile

```bash
cd linapple
make
```
For a faster compilation, you can add an option "-jx", where "x" is the number of threads of your CPU. For example, AMD Ryzen 1600 has 6 cores, but 12 threads:
```bash
make -j12
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

### Global Install
```shell
make install
```

### Debugging and Profiling

By default, the `make` command will compile an optimized version of `linapple`.

It is possible to compile a version with debugging symbols. To do so, you must
set the `DEBUG` environment variable:

    DEBUG=1 make

If you would like to also include extra code that writes profile information
suitable for the analysis program `gprof`:

    PROFILING=1 make
