# LinApple #
## Command-line fork ##

This is a fork of the LinApple Apple ][e emulator for Linux. This fork allows certain options to be specified from the command line when running LinApple.

Currently, the following command line options are available:

* -d1: Specifies a disk image to load into FDD1 (drive 0)
* -d2: Specifies a disk image to load into FDD1 (drive 1)
* -f: Specifies that the emulator should run in fullscreen mode
* -b : Specifies that benchmark should be loaded
* -l: Logs output to a file called AppleWin.log
* -m: Disables direct sound
* -autoboot: Boots the system automatically, rather than displaying the splash screen
 
When specifying disk images, the full path should be used (e.g. `linapple -d1 /home/myname/disks/MYSTHOUS.DSK`

Currently, only the options to specify disks start in fullscreen, and auto boot have been tested.

This fork is far from perfect, and has not been tested extensively. The main purpose is to allow users to set up custom shell scripts which they may use to automatically load
certain Apple ][ games or programs with the click of a button. While this need is met by this fork, extensive testing has not been performed to ensure new bugs were not
introduced by these changes.

A simple script can be set up to run an Apple ][ game or program by combining the -d1, -f, and -autoboot options, for example:

    linapple -d1 /path/to/disk/image -f -autoboot

### TODO ###

1. Testing is needed to make sure the other command line options are working correctly. Currently, only the -d1, -d2, and -f options have been tested.
2. Extensive testing is needed to ensure that these changes have not inadvertently broken other features of the program. Unfortunately, a test suite did not come with the
original code, so I have not been able to test this.
3. Add a command line switch which allows the user to specify different configuration files.

