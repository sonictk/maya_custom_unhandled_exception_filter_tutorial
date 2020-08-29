/**
 * @file   windbg_custom_ext_main.c
 * @brief  This is an example of a WinDbg extension that allows for additional comamnds in the
 *         debugger to aid in debugging our custom Maya crash dumps.
 */
#ifndef _WIN32
#error "Unsupported platform for compilation."
#endif // _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

//
// Define KDEXT_64BIT to make all wdbgexts APIs recognize 64 bit addresses
// It is recommended for extensions to use 64 bit headers from wdbgexts so
// the extensions could support 64 bit targets.
// All structs must be defined after this include, otherwise the debugger will crash.
//
#define KDEXT_64BIT
#include <wdbgexts.h>

#include <Dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"


static EXT_API_VERSION gApiVersion = { 1, 0, EXT_API_VERSION_NUMBER64, 0 };

/// This is defined with extenal linkage in wdbgexts.h
WINDBG_EXTENSION_APIS64 ExtensionApis = {0};
static USHORT gSavedMajorVersion = 0;
static USHORT gSavedMinorVersion = 0;


DLL_EXPORT BOOL DllMain(HINSTANCE hInstDLL, DWORD dwReason, DWORD dwReserved)
{
    (void)hInstDLL;
    (void)dwReserved;
    switch (dwReason) {
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
    case DLL_PROCESS_ATTACH:
    default:
        break;
    }

    return TRUE;
}


/// Required callback. When the debugger first loads our extension DLL, it calls this and passes
/// the WINDBG_EXTENSION_APIS64 struct along with version numbers.
DLL_EXPORT VOID WinDbgExtensionDllInit(PWINDBG_EXTENSION_APIS64 lpExtApis, USHORT majorVer, USHORT minorVer)
{
    ExtensionApis = *lpExtApis;
    gSavedMajorVersion = majorVer;
    gSavedMinorVersion = minorVer;

    return;
}


DLL_EXPORT LPEXT_API_VERSION ExtensionApiVersion()
{
    //
    // ExtensionApiVersion should return EXT_API_VERSION_NUMBER64 in order for APIs
    // to recognize 64 bit addresses.  KDEXT_64BIT also has to be defined before including
    // wdbgexts.h to get 64 bit headers for WINDBG_EXTENSION_APIS
    //
    return &gApiVersion;
}


/// Routine called by debugger every time an extension command is used. Can be used to
/// check for version mismatch.
DLL_EXPORT VOID CheckVersion()
{
    if (gApiVersion.MajorVersion != gSavedMajorVersion) {
        dprintf("WARNING: The major version of the debugger and extension are mismatched. %d %d\n", gApiVersion.MajorVersion, gSavedMajorVersion);
    }

    if (gApiVersion.MinorVersion != gSavedMinorVersion) {
        dprintf("WARNING: The minor version of the debugger and extension are mismatched. %d %d\n", gApiVersion.MinorVersion, gSavedMinorVersion);
    }

    return;
}


#pragma warning(disable : 4100)
DLL_EXPORT DECLARE_API(readMayaDumpStreams)
{
    // NOTE: (sonictk) Since using dbghelp functions from within WinDbg extensions is discouraged,
    // here's an alternative: parsing the file ourselves instead to get what we want.
    char tempDirPath[MAX_PATH] = {0};
    DWORD lenTempDirPath = GetEnvironmentVariable((LPCTSTR)TEMP_ENV_VAR_NAME, (LPTSTR)tempDirPath, (DWORD)MAX_PATH);
    if (lenTempDirPath == 0) {
        const size_t lenDefaultTempDirPath = strlen(DEFAULT_TEMP_DIRECTORY);
        memcpy(tempDirPath, DEFAULT_TEMP_DIRECTORY, lenDefaultTempDirPath);
        memset(tempDirPath + lenDefaultTempDirPath, 0, 1);
    }
    char dumpFilePath[MAX_PATH] = {0};
    snprintf(dumpFilePath, MAX_PATH, "%s\\%s", tempDirPath, MINIDUMP_FILE_NAME);
    HANDLE hFile = CreateFile(dumpFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        dprintf("ERROR: Could not open the dump file requested.\n");
        return;
    }

    // NOTE: (sonictk) Structure of minidump file is documented in ``minidumpapiset.h``.
    PMINIDUMP_HEADER pDumpHeader = NULL;
    PMINIDUMP_DIRECTORY pDumpDirPath = NULL;
    PMINIDUMP_USER_STREAM pDumpUserStream = NULL;
    static const ULONG gBufSize = 0x1000; // NOTE: (sonictk) Minidump header size is 4096 bytes according to: https://computer.forensikblog.de/en/2006/03/dmp-file-structure.html
    void *pBuf = malloc(gBufSize);
    DWORD bytesRead = 0;

    BOOL bStat = ReadFile(hFile, pBuf, sizeof(MINIDUMP_HEADER), &bytesRead, NULL);
    if (bStat == 0) {
        dprintf("ERROR: File read failure.\n");
        return;
    }

    ULONG numUserStreams = 0;
    if (bytesRead == sizeof(MINIDUMP_HEADER)) {
        pDumpHeader = (PMINIDUMP_HEADER)pBuf;
        numUserStreams = pDumpHeader->NumberOfStreams;
        for (ULONG i=0; i < numUserStreams; ++i) {
            bStat = ReadFile(hFile, pBuf, sizeof(MINIDUMP_DIRECTORY), &bytesRead, NULL);
            if (bStat == 0) {
                dprintf("ERROR: Failed to read minidump directory.\n");
                break;
            }

            // NOTE: (sonictk) According to the header documentation:
            //
            // The MINIDUMP_HEADER field StreamDirectoryRva points to
            // an array of MINIDUMP_DIRECTORY structures.
            //
            pDumpDirPath = (PMINIDUMP_DIRECTORY)pBuf;
            if (pDumpDirPath->StreamType != MAYA_CRASH_INFO_STREAM_TYPE) {
                continue;
            }
            // NOTE: (sonictk) According to the header documentation:
            //
            // The MINIDUMP_DIRECTORY field StreamType may be one of the following types.
            // i.e.
            //
            // TODO: (sonictk) This part confuses me: is this really the format spec?
            // How does all this casting around even work?
            pDumpUserStream = (PMINIDUMP_USER_STREAM)pBuf;
            ULONG streamBufSize = pDumpUserStream->BufferSize;
            if (streamBufSize != sizeof(MayaCrashDumpInfo)) {
                dprintf("ERROR: The stream size does not match that of the known crash dump structure.\n");
                continue;
            }
            ULONG streamType = pDumpUserStream->Type;
            PCHAR pUserStreamBuf = (PCHAR)pDumpUserStream->Buffer;
            DWORD curPos = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
#pragma warning(disable : 4311)
            SetFilePointer(hFile, (LONG)pUserStreamBuf, NULL, FILE_BEGIN);
#pragma warning(default : 4311)
            MayaCrashDumpInfo crashInfo = {0};
            bStat = ReadFile(hFile, &crashInfo, streamBufSize, &bytesRead, NULL);
            if (bStat == 0 || bytesRead != streamBufSize) {
                dprintf("ERROR: Failed to read user stream.\n");
                break;
            }

            dprintf("\n"
                    "-------------------------------------------------\n"
                    "Maya dump file information is as follows:\n"
                    "Stream type %d:\n"
                    "Maya API version: %d\n"
                    "Custom API version: %d\n"
                    "Maya file version:: %d\n"
                    "Is Y-axis up: %d \n"
                    "Last DAG parent: %s \n"
                    "Last DAG child: %s \n"
                    "Last DAG message: %d \n"
                    "Last DG node added: %s \n"
                    "\nEnd of crash info. \n"
                    "-------------------------------------------------\n"
                    "\n\n", streamType, crashInfo.verAPI, crashInfo.verCustom, crashInfo.verMayaFile, crashInfo.isYUp, crashInfo.lastDagParentName, crashInfo.lastDagChildName, crashInfo.lastDagMessage, crashInfo.lastDGNodeAddedName);

            SetFilePointer(hFile, curPos, NULL, FILE_BEGIN);
        }
    } else {
        dprintf("ERROR: Unable to read minidump header.\n");
    }

    CloseHandle(hFile);

    free(pBuf);

    return;
}


#pragma warning(disable : 4100)
DLL_EXPORT DECLARE_API(readMayaDumpStreamsHelp)
{
    dprintf("This is a custom WinDbg extension that allows for reading extended user stream information from our custom Maya minidump files.\n"
            "Use the command !readMayaDumpStreams to attempt crossing the streams.\n");
    return;
#pragma warning(default : 4100)
}
