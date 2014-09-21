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
 
When specifying disk images, the full path should be used (e.g. `linapple -d1 /home/myname/disks/MYSTHOUS.DSK`

Currently, only the options to specify disks and start in fullscreen have been tested.

This fork is far from perfect, and has not been tested extensively. The main purpose is to allow users to set up custom shell scripts which they may use to automatically load
certain Apple ][ games or programs with the click of a button. While this need is met by this fork, extensive testing has not been performed to ensure new bugs were not
introduced by these changes.

If you specify a disk image for drive 0, LinApple will automatically boot from that disk, rather than showing the splash screen on startup. This will, in turn, cause it
not to load the configuration file, so you will always start with the default configuration settings if you use this option. This is probably the best option for now,
since LinApple updates the configuration file to its current settings when it closes, which you probably don't want it to do if you're creating autoload scripts with these
command line options. By not loading a configuration file, we effectively stop it from updating that file when it closes.

### TODO ###

1. Testing is needed to make sure the other command line options are working correctly. Currently, only the -d1, -d2, and -f options have been tested.
2. Extensive testing is needed to ensure that these changes have not inadvertently broken other features of the program. Unfortunately, a test suite did not come with the
original code, so I have not been able to test this.
3. Currently, specifying the -d1 option causes the program to throw a Segmentation Fault if the user tries to change disks during gameplay. This can most likely be addressed
by setting the Slot 6 property when the -d1 option is given. Slot 6 is typically set by the configuration file, which is what led to this problem.
4. Modify the code to load the configuration file even if the -d1 option is specified.
5. Add a command line switch which allows the user to specify different configuration files.

