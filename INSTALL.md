# Installation

### Prerequisites

#### Debian / Ubuntu / RetroPie

```bash
sudo apt-get install git libzip-dev libsdl1.2-dev libsdl-image1.2-dev libcurl4-openssl-dev zlib1g-dev imagemagick
```

#### Fedora / RHEL / CentOS

```bash
sudo dnf install git SDL-devel SDL_image-devel libcurl-devel libzip-devel ImageMagick
```

### Clone

```bash
git clone https://github.com/linappleii/linapple.git
```

### Compile

```bash
cd linapple
make help    # View all available build options and targets
make         # Build the application (default)
```

To enable rewrite user settings:
```bash
make REGISTRY_WRITEABLE=1
```

For a faster compilation, you can add an option "-jX", where "X" is the number of threads of your CPU. For example, *AMD Ryzen 5 2600* has 6 cores, but 12 threads:
```bash
make -j12
```

#### Troubleshooting
If the build fails or you want to see the exact commands being executed, use the verbosity flag:
```bash
make V=1
```

Don't worry about spurious warning messages, which can look like errors; chances are that the program will build successfully even with these warnings.

### Run

```bash
cd build/bin
./linapple
```

Or, to boot automatically into a standard Apple floppy disk provided by LinApple:

```bash
./linapple --autoboot --d1 ../share/linapple/Master.dsk
```

### Configuration file

A configuration file can be found at `build/etc/linapple/linapple.conf`. It is highly recommended to read this file and edit it to your liking. File is self-explanatory.

Once configured, you can load the configuration file and automatically boot to floppy:

```bash
./linapple --conf ../etc/linapple/linapple.conf --autoboot --d1 ../share/linapple/Master.dsk
```

### Global Install

Optional step. Some contents of the recently created "build" folder will be installed on your system. The advantage of this step is that you will be capable to access LinApple from any directory, just typing "linapple", like any other program on the system.

```shell
make install
```

#### Custom Installation Path
To install to a different location (e.g., your home directory), you can override the `prefix` variable:
```bash
make install prefix=$HOME/.local
```

Now copy both configuration file `linapple.conf` and floppy disk `Master.dsk` to user's folder:

```bash
cp /usr/local/etc/linapple/linapple.conf ~/.config/linapple/
cp /usr/local/share/linapple/Master.dsk ~/.linapple/disks/
```

> NOTE: by default they will be in `/usr/local`, otherwise copy them from the `build` folder just mentioned.


In a global install, LinApple will load `~/.config/linapple/linapple.conf` automatically. You can set LinApple to load `Master.dsk` and boot it at startup in `linapple.conf`.

To run LinApple after a global installation, type anywhere, in any folder you are in:

```bash
linapple
```

Take a look at [README.md](README.md) for more detailed information.

### Uninstall

To uninstall a global install:

```bash
make uninstall
```

### Creating a debian package

To create a debian package installable with `dpkg`:

```bash
make
make deb
sudo dpkg -i linapple_VERSION_all.deb
```

Where "VERSION" is the LinApple version. See the created package and enter the correct name.

### Debugging and Profiling

By default, the `make` command will compile an optimized version of `linapple`.

It is possible to compile a version with debugging symbols:

```bash
make DEBUG=1
```

If you would like to also include extra code that writes profile information suitable for the analysis program `gprof`:

```bash
make PROFILING=1
```
