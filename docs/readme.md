# Writing a custom crash handler for Autodesk Maya #

## About ##

An example of a Win32 custom exception filter that allows for
overriding the existing Maya one to store additional info when a
user-mode crash occurs.

This repository hosts the sample code for the [accompanying tutorial]().

## Usage ##

Run `build.bat release` and you should get a Maya plugin, executable, and
WinDbg extension DLL built in the `msbuild\` folder.

## License ##

Please refer to the enclosed `LICENSE` file within this repository.
