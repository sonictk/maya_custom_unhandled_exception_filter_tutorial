/**
 * @file   maya_read_custom_dump_user_streams_main.c
 * @brief  This is an example of reading a custom struct that is stored in a user stream in
 *         the minidump generated.
 */
#ifndef _WIN32
#error "Unsupported platform for compilation."
#endif // _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Dbghelp.h>

#include "common.h"

#include <stdio.h>


void parseAndPrintCustomStreamFromMiniDump(const char *dumpFilePath)
{
    if (dumpFilePath == NULL) {
        return;
    }

    HANDLE hFile = CreateFile(dumpFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        printf("ERROR: Could not open the dump file requested.\n");
        return;
    }

    HANDLE hMapFile = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMapFile == NULL) {
        printf("ERROR: Could not create the file mapping for the dump.\n");
        return;
    }

    PVOID pFileView = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0);
    if (pFileView == NULL) {
        printf("ERROR: Failed to map view of the dump file.\n");
        return;
    }

    PMINIDUMP_DIRECTORY miniDumpDirPath = NULL;
    PVOID pUserStream = NULL;
    ULONG streamSize = 0;
    BOOL bStat = MiniDumpReadDumpStream(pFileView,
                                        MAYA_CRASH_INFO_STREAM_TYPE,
                                        &miniDumpDirPath,
                                        &pUserStream,
                                        &streamSize);
    if (bStat != TRUE) {
        printf("ERROR: Failed to find stream in dump file. Check if it was generated correctly.\n");
        return;
    }

    if (streamSize != sizeof(MayaCrashDumpInfo)) {
        printf("ERROR: Stream size mismatch. Check if the dump file was written correctly.\n");
        return;
    }

    MayaCrashDumpInfo *dumpInfo = (MayaCrashDumpInfo *)pUserStream;
    printf("Maya API version: %d\n"
           "Custom API version: %d\n"
           "Maya file version: %d\n"
           "Y is up: %d\n"
           "Last DAG parent: %s\n"
           "Last DAG child: %s\n"
           "Last DAG message: %d\n"
           "Last DG node added: %s\n"
           "End of crash info.\n",
           dumpInfo->verAPI,
           dumpInfo->verCustom,
           dumpInfo->verMayaFile,
           dumpInfo->isYUp,
           dumpInfo->lastDagParentName,
           dumpInfo->lastDagChildName,
           dumpInfo->lastDagMessage,
           dumpInfo->lastDGNodeAddedName);

    return;
}


int main(int argc, char *argv[])
{
    if (argc == 1) {
        char tempDirPath[MAX_PATH] = {0};
        DWORD lenTempDirPath = GetEnvironmentVariable((LPCTSTR)TEMP_ENV_VAR_NAME, (LPTSTR)tempDirPath, (DWORD)MAX_PATH);
        if (lenTempDirPath == 0) {
            const size_t lenDefaultTempDirPath = strlen(DEFAULT_TEMP_DIRECTORY);
            memcpy(tempDirPath, DEFAULT_TEMP_DIRECTORY, lenDefaultTempDirPath);
            memset(tempDirPath + lenDefaultTempDirPath, 0, 1);
        }
        char dumpFilePath[MAX_PATH] = {0};
        snprintf(dumpFilePath, MAX_PATH, "%s\\%s", tempDirPath, MINIDUMP_FILE_NAME);
        parseAndPrintCustomStreamFromMiniDump(dumpFilePath);
    } else {
        for (int i=1; i < argc; ++i) {
            char *path = argv[i];
            parseAndPrintCustomStreamFromMiniDump(path);
        }
    }

    return 0;
}
