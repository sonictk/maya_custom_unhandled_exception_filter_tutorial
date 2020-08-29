#ifndef COMMON_H
#define COMMON_H

#ifndef DLL_EXPORT
#define DLL_EXPORT __declspec(dllexport)
#endif

#define ARRAY_SIZE(x) sizeof(x) / sizeof(x[0])

#define MAYA_CRASH_INFO_STREAM_TYPE LastReservedStream + 1

#define MINIDUMP_FILE_NAME "MayaCustomCrashDump.dmp"
#define DEFAULT_TEMP_DIRECTORY "C:/temp"
#define TEMP_ENV_VAR_NAME "TEMP"


#ifndef __cplusplus
#include <stdbool.h>
#endif

#define MAYA_DAG_PATH_MAX_NAME_LEN 512
#define MAYA_DG_NODE_MAX_NAME_LEN 512

#pragma pack(1)
/// Information about the current Maya session when a crash occurred.
typedef struct MayaCrashDumpInfo
{
    char lastDagParentName[MAYA_DAG_PATH_MAX_NAME_LEN];
    char lastDagChildName[MAYA_DAG_PATH_MAX_NAME_LEN];
    char lastDGNodeAddedName[MAYA_DG_NODE_MAX_NAME_LEN];
    int verAPI;
    int verCustom;
    int verMayaFile;
    short lastDagMessage;
    bool isYUp;
} MayaCrashDumpInfo;


#endif /* COMMON_H */
