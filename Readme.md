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

* -d1: Specifies a disk image to load into FDD1 (drive 0)
* -d2: Specifies a disk image to load into FDD1 (drive 1)
* -f: Specifies that the emulator should run in fullscreen mode
* -b : Specifies that benchmark should be loaded
* -l: Logs output to a file called AppleWin.log
* -m: Disables direct sound
* -autoboot: Boots the system automatically, rather than displaying the splash screen
 
When specifying disk images, the full path should be used. e.g. `linapple -d1 /home/myname/disks/MYSTHOUS.DSK`

Currently, only the options to specify disks start in fullscreen, and auto boot have been tested.

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
| F7             | Reserved for debugger.                                           |
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

## Other
This fork is far from perfect, and has not been tested extensively. The main purpose is to allow users to set up custom
shell scripts which they may use to automatically load certain Apple ][ games or programs with the click of a button.
While this need is met by this fork, extensive testing has not been performed to ensure new bugs were not introduced by
these changes.

A simple script can be set up to run an Apple ][ game or program by combining the -d1, -f, and -autoboot options, for example:

```bash
linapple -d1 /path/to/disk/image -f -autoboot
```

## Todo

* Testing is needed to make sure the other command line options are working correctly. Currently, only the -d1, -d2, and
  -f options have been tested.
* Extensive testing is needed to ensure that these changes have not inadvertently broken other features of the program.
  Unfortunately, a test suite did not come with the original code, so we have not been able to test this.
* Add a command line switch which allows the user to specify different configuration files.
