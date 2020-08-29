@echo off
REM    This is the Windows build script.
REM    usage: build.bat [debug|release] [2012|2015|2017]
REM    e.g. ``build.bat release 2012`` will build in release mode using MSVC 2012 (Maya 2016 release).
REM         ``build.bat debug 2017`` will build in debug mode using MSVC 2017.
REM    If no arguments are specified, will default to building in release mode with Visual Studio 2015. (Maya 2018 release).

echo Build script started executing at %time% ...

REM Process command line arguments
set BuildType=%1
if "%BuildType%"=="" (set BuildType=release)

set MSVCCompilerVersion=%2
if "%MSVCCompilerVersion%"=="" (set MSVCCompilerVersion=2015)


REM    Set up the Visual Studio environment variables for calling the MSVC compiler;
REM    we do this after the call to pushd so that the top directory on the stack
REM    is saved correctly; the check for DevEnvDir is to make sure the vcvarsall.bat
REM    is only called once per-session (since repeated invocations will screw up
REM    the environment)
if not defined DevEnvDir (
    if "%MSVCCompilerVersion%"=="2017" (
        call "%vs2017installdir%\VC\Auxiliary\Build\vcvarsall.bat" x64
        goto start_build
    )

    if "%MSVCCompilerVersion%"=="2015" (
        call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
        goto start_build
    )

    if "%MSVCCompilerVersion%"=="2012" (
        call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" x64
        goto start_build
    )
)

if not defined DevEnvDir (
    echo Unknown compiler version specifed!
    goto error
)

echo Building in configuration: %BuildType% using MSVC %MSVCCompilerVersion%...

:start_build

REM    Make a build directory to store artifacts; remember, %~dp0 is just a special
REM    FOR variable reference in Windows that specifies the current directory the
REM    batch script is being run in
set BuildDir=%~dp0msbuild

if "%BuildType%"=="clean" (
    REM This allows execution of expressions at execution time instead of parse time, for user input
    setlocal EnableDelayedExpansion
    echo Cleaning build from directory: %BuildDir%. Files will be deleted^^!
    echo Continue ^(Y/N^)^?
    set /p ConfirmCleanBuild=
    if "!ConfirmCleanBuild!"=="Y" (
        echo Removing files in %BuildDir%...
        del /s /q %BuildDir%\*.*
    )
    goto end
)


echo Building in directory: %BuildDir%

if not exist %BuildDir% mkdir %BuildDir%
pushd %BuildDir%


REM    Set up globals
set MayaRootDir=C:\Program Files\Autodesk\Maya2021
set MayaIncludeDir=%MayaRootDir%\include
set MayaLibraryDir=%MayaRootDir%\lib

set ProjectName=maya_custom_unhandled_exception_filter

set MayaPluginEntryPoint=%~dp0src\%ProjectName%_main.cpp


REM    We pipe errors to null, since we don't care if it fails
del *.pdb > NUL 2> NUL


REM    Setup all the compiler flags
set CommonCompilerFlags=/c /MP /W4 /WX /Gy /Zc:wchar_t /Zc:forScope /Zc:inline /openmp /fp:precise /nologo /EHsc /D REQUIRE_IOSTREAM /D _CRT_SECURE_NO_WARNINGS /D _BOOL /D NT_PLUGIN /D _WINDLL /D _MBCS /Gm- /GS /Gy /Gd /TP /Fo"%BuildDir%\%ProjectName%.obj"

REM    Add the include directories for header files
set CommonCompilerFlags=%CommonCompilerFlags% /I"%MayaRootDir%\include"

set CommonCompilerFlagsDebug=/Zi /Od %CommonCompilerFlags% /D_DEBUG /MDd
set CommonCompilerFlagsRelease=/O2 %CommonCompilerFlags% /DNDEBUG /MD

set MayaPluginCompilerFlagsDebug=%CommonCompilerFlagsDebug% %MayaPluginEntryPoint%
set MayaPluginCompilerFlagsRelease=%CommonCompilerFlagsRelease% %MayaPluginEntryPoint%


REM    Setup all the linker flags
set CommonLinkerFlags=/nologo /incremental:no /subsystem:console /machine:x64 /dll

REM    Add all the Maya libraries to link against
set CommonLinkerFlags=%CommonLinkerFlags% "%MayaLibraryDir%\OpenMaya.lib" "%MayaLibraryDir%\OpenMayaAnim.lib" "%MayaLibraryDir%\OpenMayaFX.lib" "%MayaLibraryDir%\OpenMayaRender.lib" "%MayaLibraryDir%\OpenMayaUI.lib" "%MayaLibraryDir%\Foundation.lib"

REM    Now add the OS libraries to link against
set CommonLinkerFlags=%CommonLinkerFlags% /defaultlib:Kernel32.lib /defaultlib:User32.lib /defaultlib:Dbghelp.lib

set CommonLinkerFlags=%CommonLinkerFlags% /pdb:"%BuildDir%\%ProjectName%.pdb" /implib:"%BuildDir%\%ProjectName%.lib" "%BuildDir%\%ProjectName%.obj"

set CommonLinkerFlagsDebug=%CommonLinkerFlags% /debug /opt:noref
set CommonLinkerFlagsRelease=%CommonLinkerFlags% /opt:ref

set MayaPluginExtension=mll
set MayaPluginLinkerFlagsCommon=/export:initializePlugin /export:uninitializePlugin /out:"%BuildDir%\%ProjectName%.%MayaPluginExtension%"
set MayaPluginLinkerFlagsRelease=%CommonLinkerFlagsRelease% %MayaPluginLinkerFlagsCommon%
set MayaPluginLinkerFlagsDebug=%CommonLinkerFlagsDebug% %MayaPluginLinkerFlagsCommon%


if "%BuildType%"=="debug" (
    echo Building in debug mode...

    set MayaPluginCompilerFlags=%MayaPluginCompilerFlagsDebug%
    set MayaPluginLinkerFlags=%MayaPluginLinkerFlagsDebug%
) else (
    echo Building in release mode...

    set MayaPluginCompilerFlags=%MayaPluginCompilerFlagsRelease%
    set MayaPluginLinkerFlags=%MayaPluginLinkerFlagsRelease%
)


REM Now build the Maya plugin
echo Compiling Maya plugin (command follows)...
echo cl %MayaPluginCompilerFlags%
cl %MayaPluginCompilerFlags%
if %errorlevel% neq 0 goto error


:link
echo Linking (command follows)...
echo link %MayaPluginLinkerFlags%
link %MayaPluginLinkerFlags%
if %errorlevel% neq 0 goto error


REM    Now build the accompanying WinDbg extension
set WinDbgExtCommonCompilerFlags=/nologo /W4 /WX
set WinDbgExtDebugCompilerFlags=%WinDbgExtCommonCompilerFlags% /Zi /Od
set WinDbgExtReleaseCompilerFlags=%WinDbgExtCommonCompilerFlags% /O2

set WinDbgExtCommonLinkerFlags=/nologo /dll /machine:x64 /incremental:no /subsystem:windows /out:"%BuildDir%\windbg_%ProjectName%.dll" /pdb:"%BuildDir%\windbg_%ProjectName%.pdb" /defaultlib:Kernel32.lib /defaultlib:User32.lib
set WinDbgExtDebugLinkerFlags=%WinDbgExtCommonLinkerFlags% /opt:noref /debug
set WinDbgExtReleaseLinkerFlags=%WinDbgExtCommonLinkerFlags% /opt:ref

set WinDbgExtEntryPoint=%~dp0src\windbg_custom_ext_main.c

if "%BuildType%"=="debug" (
    set WinDbgExtBuildCmd=cl %WinDbgExtDebugCompilerFlags% "%WinDbgExtEntryPoint%" /link %WinDbgExtDebugLinkerFlags%
) else (
    set WinDbgExtBuildCmd=cl %WinDbgExtReleaseCompilerFlags% "%WinDbgExtEntryPoint%" /link %WinDbgExtReleaseLinkerFlags%
)

echo Compiling WinDbg extension (command follows)...
echo %WinDbgExtBuildCmd%
%WinDbgExtBuildCmd%
if %errorlevel% neq 0 goto error

if not "%_NT_DEBUGGER_EXTENSION_PATH%"=="" (
    echo Copying WinDbg extension to deployment location %_NT_DEBUGGER_EXTENSION_PATH% ...
    echo .
    copy /Y "%BuildDir%\windbg_%ProjectName%.dll" "%_NT_DEBUGGER_EXTENSION_PATH%"
)


REM    Now build the custom dump file reader
set DumpReaderCommonCompilerFlags=/nologo /W4 /WX /Fe:"%BuildDir%\dump_reader.exe"
set DumpReaderDebugCompilerFlags=%DumpReaderCommonCompilerFlags% /Zi /Od
set DumpReaderReleaseCompilerFlags=%DumpReaderCommonCompilerFlags% /O2

set DumpReaderCommonLinkerFlags=/nologo /machine:x64 /incremental:no /subsystem:console /defaultlib:Kernel32.lib /defaultlib:Dbghelp.lib /pdb:"%BuildDir%\dump_reader.pdb"
set DumpReaderDebugLinkerFlags=%DumpReaderCommonLinkerFlags% /opt:noref /debug
set DumpReaderReleaseLinkerFlags=%DumpReaderCommonLinkerFlags% /opt:ref

set DumpReaderEntryPoint=%~dp0src\maya_read_custom_dump_user_streams_main.c

if "%BuildType%"=="debug" (
    set DumpReaderBuildCmd=cl %DumpReaderDebugCompilerFlags% "%DumpReaderEntryPoint%" /link %DumpReaderDebugLinkerFlags%
) else (
    set DumpReaderBuildCmd=cl %DumpReaderReleaseCompilerFlags% "%DumpReaderEntryPoint%" /link %DumpReaderReleaseLinkerFlags%
)

echo Compiling custom dump file reader (command follows)...
echo %DumpReaderBuildCmd%
%DumpReaderBuildCmd%
if %errorlevel% neq 0 goto error
if %errorlevel% == 0 goto success


:error
echo ***************************************
echo *      !!! An error occurred!!!       *
echo ***************************************
goto end


:success
echo ***************************************
echo *    Build completed successfully!    *
echo ***************************************
goto end


:end
echo Build script finished execution at %time%.
popd
exit /b %errorlevel%
