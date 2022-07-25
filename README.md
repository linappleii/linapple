LinApple
========

LinApple is an emulator for Apple ][, Apple ][+, Apple //e, and
Enhanced Apple //e computers.

History
-------

LinApple started as a fork of [AppleWin], ported to run on Linux. The
source code was downloaded from [SourceForge] and uploaded to Github
by [maxolasersquad]. As the SourceForge version lay abandoned several
users forked the maxolasersquad repository and added several features.
Work was done to bring these back into maxolasersquad repository. This
repository was eventually moved under the [linappleii] namespace where
development currently resides.

[AppleWin]: https://github.com/AppleWin/AppleWin
[SourceForge]: http://linapple.sourceforge.net/
[maxolasersquad]: https://github.com/maxolasersquad
[linappleii]: https://github.com/linappleii


Command Line Switches
---------------------

* `-h|--help`: Print command-line options and exit.
* `--conf path/to/file.conf`: Use only the specified configuration file.
  (The standard configuration file search is not done.)
* `-1|--d1 path/to/image1.dsk`: Specifies a disk image to load into FDD1 (drive 0)
* `-2|--d2 path/to/image2.dsk`: Specifies a disk image to load into FDD1 (drive 1)
* `-b|--autoboot`: Boots the system automatically, rather than displaying the splash screen
* `-f`: Specifies that the emulator should run in fullscreen mode
* `-l`: Logs output to a file called AppleWin.log (untested)
* `--benchmark`: Specifies that benchmark should be loaded (untested)

_Note: some command-line options have not been fully tested._

### Command-line Examples

To have LinApple start in fullscreen mode and automatically boot the
disk `example.dsk`, you can open a shell and run:

    $ linapple --d1 example.dsk -f --autoboot

This command could also be placed in a shell script, which could be
started from an icon or menu on the desktop.


Using LinApple
--------------

Clicking in the LinApple window will capture the mouse. It may be
released by pressing any function key.

| Key            | Function                                                         |
| -------------- | -----------------------------------------------------------------|
| F1             | Show help screen.                                                |
| Ctrl+F2        | Cold reboot, i.e. power off and back on.                         |
| Shift+F2       | Reload configuration file and cold reboot.                       |
| Ctrl+F10       | Hot Reset (Control+Reset)                                        |
| F12            | Quit.                                                            |
| -------------- | -----------------------------------------------------------------|
| F3             | Load disk image to slot 6 drive 1.                               |
| F4             | Load disk image to slot 6 drive 2.                               |
| F5             | Swap drives for slot 6.                                          |
| Alt+F3         | Load disk image to slot 6 drive 1 from FTP server.               |
| Alt+F4         | Load disk image to slot 6 drive 2 from FTP server.               |
| Shift+F3       | Attach hard disk image to slot 7 drive 1.                        |
| Shift+F4       | Attach hard disk image to slot 7 drive 2.                        |
| Alt+Shift+F3   | Attach hard disk image to slot 7 from FTP server.                |
| Alt+Shift+F4   | Attach hard disk image to slot 7 from FTP server.                |
| Ctrl+F3        | Eject disk image to slot 6 drive 1.                              |
| Ctrl+F4        | Eject disk image to slot 6 drive 2.                              |
| Ctrl+Shift+F3  | Eject hard disk image to slot 7 drive 1.                         |
| Ctrl+Shift+F4  | Eject hard disk image to slot 7 drive 2.                         |
| -------------- | -----------------------------------------------------------------|
| F6             | Toggle fullscreen mode. See Warning below.                       |
| Shift+F6       | Toggle character set (keyboard/video ROM rocker switch for       |
|                | Apple IIe/enhanced with international keyboards/video ROMs)      |
| F7             | Show debugger.                                                   |
| F8             | Save screenshot as a bitmap.                                     |
| Shift+F8       | Save runtime changes to configuration to the configuration file. |
| F9             | Cycle through video modes.                                       |
| Shift+F9       | Budget video, for smoother music/audio.                          |
| F10            | Load snapshot file.                                              |
| F11            | Save snapshot file.                                              |
| Ctrl+0-9       | Load snapshot `n`.                                               |
| Ctrl+Shift+0-9 | Save snapshot `n`.                                               |
| -------------- | -----------------------------------------------------------------|
| Pause          | Pause/resume emulation.                                          |
| Scroll Lock    | Toggle full speed emulation.                                     |
| Numpad +       | Increase emulation speed.                                        |
| Numpad -       | Decrease emulation speed.                                        |
| Numpad *       | Reset emulation speed.                                           |
| RtCtrl+Numpad  | Adjust pdl TrimX (4, 6) or TrimY (2, 8)                          |
| -------------- | -----------------------------------------------------------------|

**Warning**: Fullscreen mode does not properly exit in multi-monitor
setups.  (This is a bug in SDL 1.2.)

When you first start the emulator, press the F3 key and select a disk
image file. Press Ctrl+F2 to restart the emulator with the disk
"inserted" if the application doesn't automatically load.

Many disk images will boot straight into the application. Some disk
images only have files and you must find the correct application to
run. In this case you will need to execute BASIC commands to list the
files on the disk and run programs.

### Apple II Commands

This is a brief guide to get you started. For more information see
[_Apple II: The DOS Manual_][dos3.3] (for DOS 3.3) and the [_ProDOS 8
Technical Reference Manual_][prodos].

- `CATALOG`: List the files on a disk. The first column in the list
  identifies the file type: A for Applesoft BASIC, B for binary. I for
  Integer BASIC. T for text.
- `RUN file`: Load and run an Applesoft or Integer BASIC file.
- `BRUN file`: Load and run a binary file. (Not all binary files can
  be run.)
- `EXEC file`: Read commands from a text file and execute them as if
  they were typed at the keyboard.
- `LOAD file`: Load an Applesoft or Integer BASIC file into memory.
- `LIST`: List the current program in memory.

[prodos]: http://www.easy68k.com/paulrsm/6502/PDOS8TRM.HTM
[dos3.3]: https://archive.org/details/a2_the_DOS_Manual/page/n2/mode/1up


Configuration
-------------

LinApple has several configuration options. Most values are loaded
from [INI files](https://en.wikipedia.org/wiki/INI_file) and
`linapple.conf.sample` has all possible configuration options along
with documentation on each. There are also command-line switches and
environment variables that change the behavior of LinApple.

### Load Order of Configuration Files

1. Default values are initially set by the program.
2. If the command-line switch `--conf` is used, values are read from
   the specified file.
3. Otherwise, values are:
   1. read from the system-wide configuration files.
      * These are found in the `$XDG_CONFIG_DIRS` path. On Linux, the
        file is typically found at `/etc/xdg/linapple/linapple.conf`.
   2. read from user-specific configuration files.
      * These are found in the `$XDG_CONFIG_HOME` path. On Linux, the
        file is typically found at `~/.config/linapple/linapple.conf`.

### Environment Variables

LinApple partially conforms to the [XDG Base Directory Specification][xdg].

`XDG_CONFIG_HOME` specifies the location of _"a single base directory
relative to which user-specific configuration files should be
written"_. Defaults to `$HOME/.config`.

`XDG_CONFIG_DIRS` specifies the location of _"a set of preference
ordered base directories relative to which configuration files should
be searched"_. Each directory is separated by a colon (`:`). Defaults
to `/etc/xdg`.

[xdg]: https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
