# LinApple

LinApple is an emulator for Apple ][, Apple ][+, Apple //e, and Enhanced Apple //e computers.

## History

LinApple started as a fork of [AppleWin](https://github.com/AppleWin/AppleWin), ported to run on Linux. The source code
was downloaded from [SourceForge](http://linapple.sourceforge.net/) and uploaded to Github by
[maxolasersquad](https://github.com/maxolasersquad). As the SourceForge version lay abandoned several users forked the
maxolasersquad repository and added several features. Work was done to bring these back into maxolasersquad repository.
This repository was eventually moved under the [linappleii](https://github.com/linappleii) namespace where development
currently resides.

## Command Line Switches

* `-h|--help`: Print command-line options and exit.
* `--conf path/to/file.conf`: Use only the specified configuration file.
  (The standard configuration file search is not done.)
* `-1|--d1 path/to/image1.dsk`: Specifies a disk image to load into FDD1 (drive 0)
* `-2|--d2 path/to/image2.dsk`: Specifies a disk image to load into FDD1 (drive 1)
* `-b|--autoboot`: Boots the system automatically, rather than displaying the splash screen
* `-f`: Specifies that the emulator should run in fullscreen mode
* `-l`: Logs output to a file called AppleWin.log (untested)
* `--benchmark`: Specifies that benchmark should be loaded (untested)

## Using LinApple

| Key            | Function                                                         |
| -------------- | -----------------------------------------------------------------|
| F1             | Show help screen.                                                |
| Ctrl+F2        | Cold reboot. i.e. Power off and back on.                         |
| Shift+F2       | Reload configuration file and cold reboot.                       |
| F3             | Load disk image for drive 1.                                     |
| F4             | Load disk image for drive 2.                                     |
| Shift+F3       | Attach hard disk image to slot 7.                                |
| Shift+F4       | Attach hard disk image to slot 7.                                |
| F5             | Swap drives for slot 6.                                          |
| F6             | Toggle fullscreen mode.  See Warning below.                      |
| Shift+F6       | Toggle character set (keyboard/video ROM rocker switch for       |
|                | Apple IIe/enhanced with international keyboards/video ROMs)      |
| F7             | Show debugger.                                                   |
| F8             | Save screenshot as a bitmap.                                     |
| Shift+F8       | Save runtime changes to configuration to the configuration file. |
| F9             | Cycle through video modes.                                       |
| F10            | Load snapshot file.                                              |
| F11            | Save snapshot file.                                              |
| F12            | Quit.                                                            |
| Ctrl+0-9       | Load snapshot `n`.                                               |
| Ctrl+Shift+0-9 | Save snapshot `n`.                                               |
| Pause          | Pause/resume emulation.                                          |
| Scroll Lock    | Toggle fulls peed emulation.                                     |
| Numpad +       | Increase emulation speed.                                        |
| Numpad -       | Decrease emulation speed.                                        |
| Numpad *       | Reset emulation speed.                                           |
| RtCtrl+Numpad  | Adjust pdl TrimX (4, 6) or TrimY (2, 8)                          |

**Warning**: Fullscreen mode does not properly exit in multi-monitor setups.  (This is a bug in SDL 1.2.)

When you first start the emulator, press the F3 key and select a disk image file. Press Ctrl+F2 to restart the emulator
with the disk "inserted" if the application doesn't automatically load.

Many disk images will boot straight into the application. Some disk images only have files and you must find the correct
application to run. In this case you will need to execute BASIC commands to list the files on the disk and run programs.

### Commands

#### CATALOG

This command lists the files on a disk.

The first column in the list identifies the file type. A for Applesoft BASIC. B for binary. I for Integer BASIC. T for
ASCII text.

The third column lists the name of the file.

#### RUN file

Execute the Applesoft BASIC or Integer BASIC file.

#### BRUN file

Execute the binary file.

#### EXEC file

Execute a text file as though it was typed from the keyboard.

#### LOAD file

Load a file, usually a text file, into memory.

#### LIST

List the current program in memory.


## Configuration

LinApple has several configuration options. Most values are loaded from
[INI files](https://en.wikipedia.org/wiki/INI_file) and `linapple.conf.sample`
has all possible configuration options along with documentation on each. There
are also command-line switches and environment variables that change the
behavior of LinApple.

### Load Order of Configuration Files

1. Default values are initially set by the program.
2. If the command-line switch `--conf` is used, values are read from the
   specified file.
3. Otherwise, values are:
   1. read from the system-wide configuration files.
      * These are found in the `$XDG_CONFIG_DIRS` path. On Linux, the file is
        typically found at `/etc/xdg/linapple/linapple.conf`.
   2. read from user-specific configuration files.
      * These are found in the `$XDG_CONFIG_HOME` path. On Linux, the file is
        typically found at `~/.config/linapple/linapple.conf`.


### Environment Variables

LinApple partially conforms to the [XDG Base Directory Specification]().

`XDG_CONFIG_HOME` specifies the location of _"a single base directory relative
to which user-specific configuration files should be written"_.
Defaults to `$HOME/.config`.

`XDG_CONFIG_DIRS` specifies the location of _"a set of preference ordered base
directories relative to which configuration files should be searched"_. Each
directory is separated by a colon (`:`). Defaults to `/etc/xdg`.


[XDG Base Directory Specification]:
  https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html


## Examples

To have LinApple start in fullscreen mode and automatically boot the disk
`example.dsk`, you can open a shell and run:

```bash
$ linapple --d1 example.dsk -f --autoboot
```

This command could also be placed in a shell script, which could be started
from an icon or menu on the desktop.

_Note: extensive testing has not been performed on all command-line options._
