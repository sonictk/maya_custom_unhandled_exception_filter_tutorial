# Writing a custom crash handler for Autodesk Maya #

## About ##

An example of a Win32 custom exception filter that allows for
overriding the existing Maya one to store additional info when a
user-mode crash occurs. Also, I show how to write a WinDbg extension,
along with a standalone executable capable of parsing minidumps for our
additional info as well

This repository hosts the sample code for the [accompanying tutorial](https://sonictk.github.io/maya_custom_unhandled_exception_filter_tutorial).


## Requirements ##

- Windows 10 x64
- Maya 2019 (or other modern equivalent)
- WinDbg or WinDbg Preview (preferred)
- Visual Studio 2019 (or other modern equivalent)


## Usage ##

Run `build.bat release` and you should get a Maya plugin, executable, and
WinDbg extension DLL built in the `msbuild\` folder. You can then load the
plug-in in Maya using the following MEL:

``` mel
loadPlugin "maya_custom_unhandled_exception_filter.mll"
```

And run the following command to generate a dump file:

``` mel
mayaForceCrash -ct 1;
```

You can then open WinDbg and load the extension DLL built. You will have the
following command `!readMayaDumpStreams` accessible to you, which should be able
to extract the information from the dump file itself.


## License ##

Please refer to the enclosed `LICENSE` file within this repository for details.
