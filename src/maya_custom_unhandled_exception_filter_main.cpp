/**
 * @file   maya_custom_unhandled_exception_filter_main.cpp
 * @brief  An example of a custom unhandled exception filter to write out customized
 *         crash dumps for Autodesk Maya.
 */
#ifndef _WIN32
#error "Unsupported platform for compilation."
#endif // _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Dbghelp.h>
#include <TlHelp32.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <stdlib.h> // NOTE: (sonictk) For _set_purecall_handler

#include <vector>

#include <maya/MAnimControl.h>
#include <maya/MCommandMessage.h>
#include <maya/MDGMessage.h>
#include <maya/MDagPath.h>
#include <maya/MDagMessage.h>
#include <maya/MFileIO.h>
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MQtUtil.h>
#include <maya/MSceneMessage.h>
#include <maya/MString.h>
#include <maya/MTime.h>

#include "common.h"
#include "maya_custom_unhandled_exception_filter_cmd.cpp"
#include "get_exception_info.c"

static const char MSG_UNHANDLED_EXCEPTION[] = "An unhandled exception occurred.";
static const char MSG_UNABLE_TO_WRITE_DUMP[] = "Unable to write out dump file.";

static const char PLUGIN_AUTHOR[] = "Siew Yi Liang";
static const char PLUGIN_VERSION[] = "1.0.0";
static const char PLUGIN_REQUIRED_API_VERSION[] = "Any";

static bool gHandlerCalled = false;

static LPTOP_LEVEL_EXCEPTION_FILTER gPrevFilter = NULL;
static FARPROC gOrigCRTFilter = NULL;
static bool gCRTFilterPatched = false;
static PVECTORED_EXCEPTION_HANDLER gpVectoredHandler = NULL;

typedef void (__cdecl* _purecall_handler)(void);
static _purecall_handler gOrigPurecallHandler = NULL;

typedef void (* abort_handler)(int sig);
static abort_handler gOrigAbortHandler = NULL;

/// Global .bss state that should be written into the minidump.
/// We will record the current scene open at the time of the crash.
static char gMayaCurrentScenePath[MAX_PATH] = {0};

/// And the current timeline value and FPS.
#define MAYA_MINIDUMP_TIMING_INFO_BLK_SIZE 32
static char gMayaTimingInfoBlk[MAYA_MINIDUMP_TIMING_INFO_BLK_SIZE] = "";

/// And the last 1024 characters of the last MEL command executed (not proc, command, specifically)
/// Sometimes they might give a valuable clue as to what went wrong.
#define MAYA_MINIDUMP_MEL_CMD_INFO_BLK_SIZE 1024
static char gMayaMELCmdInfoBlk[MAYA_MINIDUMP_MEL_CMD_INFO_BLK_SIZE] = "";

static MayaCrashDumpInfo gMayaCrashDumpInfo = {0};

/// Global record of callback IDs to be unregistered.
static MCallbackId gMayaSceneAfterOpen_cbid = 0;
static MCallbackId gMayaTimeChange_cbid = 0;
static MCallbackId gMayaMELCmd_cbid = 0;
static MCallbackId gMayaAllDAGChanges_cbid = 0;
static MCallbackId gMayaNodeAdded_cbid = 0;


/// Callback executed on scene open events. It is used to set the record of the last scene opened
/// in the .bss segment which should be less suspectible to heap/stack corruption.
/// It also sets other static data that's retrievable from the crash dump.
void mayaSceneAfterOpenCB(void *unused)
{
    (void)unused;

    const MString curFileNameMStr = MFileIO::currentFile();
    const unsigned int lenCurFileName = curFileNameMStr.length();
    memcpy(gMayaCurrentScenePath, curFileNameMStr.asChar(), lenCurFileName);
    memset(gMayaCurrentScenePath + lenCurFileName, 0, 1);

    memset(&gMayaCrashDumpInfo, 0, sizeof(gMayaCrashDumpInfo));
    gMayaCrashDumpInfo.verAPI = MGlobal::apiVersion();
    gMayaCrashDumpInfo.verCustom = MGlobal::customVersion();
    gMayaCrashDumpInfo.verMayaFile = MFileIO::latestMayaFileVersion();
    gMayaCrashDumpInfo.isYUp = MGlobal::isYAxisUp();

    return;
}


/// Callback executed on time change events.
void mayaSceneTimeChangeCB(MTime &time, void *unused)
{
    (void)unused;
    const MTime::Unit curUIUnit = MTime::uiUnit();
    double curFrame = time.asUnits(curUIUnit);
    snprintf(gMayaTimingInfoBlk, MAYA_MINIDUMP_TIMING_INFO_BLK_SIZE, "Frame: %.1f Unit: %d", curFrame, curUIUnit);
    return;
}


/// Callback executed every time a MEL command is run.
/// It's called once before execution, and once after end of execution.
void mayaMELCmdCB(const MString &str, unsigned int procID, bool isProcEntry, unsigned int type, void *unused)
{
    (void)unused;
    (void)procID;
    (void)isProcEntry;
    // if (type == kMELProc) { // NOTE: (sonictk) We only check against actual kMELCommand
    //     return;
    // }
    (void)type;
    const char *cmdC = str.asChar();
    size_t lenCmdC = strlen(cmdC);
    size_t lenToStore = lenCmdC > MAYA_MINIDUMP_MEL_CMD_INFO_BLK_SIZE ? MAYA_MINIDUMP_MEL_CMD_INFO_BLK_SIZE : lenCmdC;
    memcpy(gMayaMELCmdInfoBlk, cmdC, lenToStore);
    memset(gMayaMELCmdInfoBlk + lenToStore, '\0', 1);

    return;
}


/// Callback executed on every time a change is made to the Maya DAG.
void mayaAllDAGChangesCB(MDagMessage::DagMessage msgType, MDagPath &child, MDagPath &parent, void *unused)
{
    (void)unused;
    gMayaCrashDumpInfo.lastDagMessage = (short)msgType;
    MString childName = child.partialPathName();
    const char *childNameC = childName.asChar();
    size_t lenChildName = strlen(childNameC);
    lenChildName = lenChildName > MAYA_DAG_PATH_MAX_NAME_LEN ? MAYA_DAG_PATH_MAX_NAME_LEN : lenChildName;

    MString parentName = parent.partialPathName();
    const char *parentNameC = parentName.asChar();
    size_t lenParentName = strlen(parentNameC);
    lenParentName = lenParentName > MAYA_DAG_PATH_MAX_NAME_LEN ? MAYA_DAG_PATH_MAX_NAME_LEN : lenParentName;

    memcpy(gMayaCrashDumpInfo.lastDagChildName, childNameC, lenChildName);
    memset(gMayaCrashDumpInfo.lastDagChildName + lenChildName, 0, 1);

    memcpy(gMayaCrashDumpInfo.lastDagParentName, parentNameC, lenParentName);
    memset(gMayaCrashDumpInfo.lastDagParentName + lenParentName, 0, 1);

    return;
}


/// Callback executed every time a new node is added to the DG.
void mayaNodeAddedCB(MObject &node, void *unused)
{
    (void)unused;

    if (!node.hasFn(MFn::kDependencyNode)) {
        return;
    }

    MStatus mstat;
    MFnDependencyNode fnNode(node, &mstat);
    if (mstat != MStatus::kSuccess) {
        return;
    }
    MString nodeName;
    if (!fnNode.hasUniqueName()) {
        nodeName = fnNode.absoluteName(&mstat);
        if (mstat != MStatus::kSuccess) {
            return;
        }
    } else {
        nodeName = fnNode.name(&mstat);
    }

    const char *nodeNameC = nodeName.asChar();
    size_t lenNodeName = strlen(nodeNameC);
    if (lenNodeName == 0) {
        return;
    }
    memcpy(gMayaCrashDumpInfo.lastDGNodeAddedName, nodeNameC, lenNodeName);
    memset(gMayaCrashDumpInfo.lastDGNodeAddedName + lenNodeName, 0, 1);

    return;
}


LONG WINAPI detouredSetUnhandledExceptionFilter(LPEXCEPTION_POINTERS exceptionInfo)
{
    (void)exceptionInfo;
    return 0;
}


LONG WINAPI unwantedUnhandledExceptionFilter(LPEXCEPTION_POINTERS exceptionInfo)
{
    (void)exceptionInfo;
    ::MessageBoxA(NULL, "If you see this...", "...something has gone wrong.", MB_OK|MB_ICONSTOP);
    return EXCEPTION_CONTINUE_SEARCH;
}


/// Our actual exception filter that does the dirty work of writing out the minidump.
LONG WINAPI mayaCustomUnhandledExceptionFilter(LPEXCEPTION_POINTERS exceptionInfo)
{
    if (gHandlerCalled == true) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    char tempDirPath[MAX_PATH] = {0};
    DWORD lenTempDirPath = ::GetEnvironmentVariable((LPCTSTR)TEMP_ENV_VAR_NAME, (LPTSTR)tempDirPath, (DWORD)MAX_PATH);
    if (lenTempDirPath == 0) {
        static const size_t lenDefaultTempDirPath = strlen(DEFAULT_TEMP_DIRECTORY);
        memcpy(tempDirPath, DEFAULT_TEMP_DIRECTORY, lenDefaultTempDirPath);
        memset(tempDirPath + lenDefaultTempDirPath, 0, 1);
    }
    char dumpFilePath[MAX_PATH] = {0};
    snprintf(dumpFilePath, MAX_PATH, "%s\\%s", tempDirPath, MINIDUMP_FILE_NAME);
    HANDLE hFile = CreateFile(dumpFilePath, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    // NOTE: (sonictk) If we can't write out the dump file, continue with normal crash handling
    // since that is pretty much the point of our custom exception handler.
    if (hFile == NULL || hFile == INVALID_HANDLE_VALUE) {
        ::MessageBoxA(NULL, MSG_UNABLE_TO_WRITE_DUMP, MSG_UNHANDLED_EXCEPTION, MB_OK|MB_ICONSTOP);
    // NOTE: (sonictk) This calls our exception handler, but also
    // allows other exception handlers to kick in since it will proceed with normal execution of
    // the filter.
        return EXCEPTION_CONTINUE_SEARCH;
    }

    MINIDUMP_EXCEPTION_INFORMATION dumpExceptionInfo = {0};
    dumpExceptionInfo.ThreadId = ::GetCurrentThreadId();
    dumpExceptionInfo.ExceptionPointers = exceptionInfo;
    dumpExceptionInfo.ClientPointers = TRUE;

    // NOTE: (sonictk) Now let's store some custom information in the dump file. Chief among which:
    // the name of the Maya scene.
    MINIDUMP_USER_STREAM dumpMayaFileInfo = {0};
    dumpMayaFileInfo.Type = CommentStreamA;
    dumpMayaFileInfo.BufferSize = MAX_PATH;
    dumpMayaFileInfo.Buffer = gMayaCurrentScenePath;

    // NOTE: (sonictk) Now the timing information.
    MINIDUMP_USER_STREAM dumpMayaTimeInfo = {0};
    dumpMayaTimeInfo.Type = CommentStreamA;
    dumpMayaTimeInfo.BufferSize = MAYA_MINIDUMP_TIMING_INFO_BLK_SIZE;
    dumpMayaTimeInfo.Buffer = gMayaTimingInfoBlk;

    // NOTE: (sonictk) Now the last MEL command executed.
    MINIDUMP_USER_STREAM dumpMayaLastMELCmdInfo = {0};
    dumpMayaLastMELCmdInfo.Type = CommentStreamA;
    dumpMayaLastMELCmdInfo.BufferSize = MAYA_MINIDUMP_MEL_CMD_INFO_BLK_SIZE;
    dumpMayaLastMELCmdInfo.Buffer = gMayaMELCmdInfoBlk;

    // NOTE: (sonictk) Now we store, as a binary block, additional crash dump information.
    MINIDUMP_USER_STREAM dumpMayaCrashInfo = {0};
    dumpMayaCrashInfo.Type = MAYA_CRASH_INFO_STREAM_TYPE;
    dumpMayaCrashInfo.BufferSize = sizeof(gMayaCrashDumpInfo);
    dumpMayaCrashInfo.Buffer = &gMayaCrashDumpInfo;

    MINIDUMP_USER_STREAM streams[] = {
        dumpMayaFileInfo,
        dumpMayaTimeInfo,
        dumpMayaLastMELCmdInfo,
        dumpMayaCrashInfo
    };
    MINIDUMP_USER_STREAM_INFORMATION dumpUserInfo = {0};
    dumpUserInfo.UserStreamCount = ARRAY_SIZE(streams);
    dumpUserInfo.UserStreamArray = streams;

    // static const DWORD miniDumpFlags = MiYniDumpWithDataSegs|MiniDumpWithPrivateReadWriteMemory|MiniDumpWithHandleData|MiniDumpWithThreadInfo|MiniDumpWithFullMemoryInfo|MiniDumpWithUnloadedModules;
    static const DWORD miniDumpFlags = MiniDumpNormal;
    BOOL dumpWritten = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), hFile, (MINIDUMP_TYPE)miniDumpFlags, &dumpExceptionInfo, &dumpUserInfo, NULL);
    if (dumpWritten == false) {
#ifdef _DEBUG
        DWORD wErr = ::GetLastError();
        LPVOID lpMsgBuf = NULL;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            wErr,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR) &lpMsgBuf,
            0,
            NULL);
        ::MessageBoxA(NULL, (LPCSTR)lpMsgBuf, "Error details", MB_OK|MB_ICONSTOP);

        LocalFree(lpMsgBuf); // NOTE: (sonictk) Honestly not that important, but sure, let's cleanup properly.
#endif // _DEBUG
        ::MessageBoxA(NULL, MSG_UNABLE_TO_WRITE_DUMP, MSG_UNHANDLED_EXCEPTION, MB_OK|MB_ICONSTOP);
        return EXCEPTION_CONTINUE_SEARCH;
    } else {
        char msg[MAX_PATH] = {0};
        snprintf(msg, MAX_PATH, "An unrecoverable error has occured and the application will now close.\nA minidump file has been written to the following location for debugging purposes:\n%s", dumpFilePath);
        ::MessageBoxA(NULL, msg, MSG_UNHANDLED_EXCEPTION, MB_OK|MB_ICONSTOP);
    }

    CloseHandle(hFile);

    return EXCEPTION_EXECUTE_HANDLER;
}


LONG WINAPI mayaCustomVectoredExceptionHandler(PEXCEPTION_POINTERS exceptionInfo)
{
    LONG result = mayaCustomUnhandledExceptionFilter(exceptionInfo);
    gHandlerCalled = true;
    return result;
}


bool patchOverIATEntryInOneModule(const char *calleeModName, PROC pfnCurrent, PROC pfnNew, HMODULE hmodCaller)
{
    ULONG importSectionSize = 0;
    PIMAGE_IMPORT_DESCRIPTOR pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)::ImageDirectoryEntryToDataEx(hmodCaller, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &importSectionSize, NULL);
    if (pImportDesc == NULL) {
        return false;
    }

    // NOTE: (sonictk) Now we search within the import directory itself to find the specific entry we want to patch over.
    bool foundEntry = false;
    while (pImportDesc->Name != NULL && pImportDesc->Characteristics != NULL) {
        PSTR pszImportName = (PSTR)((PBYTE)hmodCaller + pImportDesc->Name);
        if (_stricmp(pszImportName, calleeModName) == 0) {
            foundEntry = true;
            break;
        }
        ++pImportDesc;
    }
    if (foundEntry != true) {
        return false;
    }

    // NOTE: (sonictk) Ok, now that we found the entry in the import table, we'll go over each
    // IAT thunk from the import desc. to find the one used to call our desired proc.
    PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((PBYTE)hmodCaller + pImportDesc->FirstThunk);
    while (pThunk->u1.Function != NULL) {
        // NOTE: (sonictk) Get a address of the function address to compare against.
        PROC *pFunc = (PROC *)&pThunk->u1.Function;

        if (*pFunc == *pfnCurrent) { // NOTE: (sonictk) Found! Now we can patch over it.
            // NOTE: (sonictk) To do that, let's check the memory page where this is stored,
            // set its permissions to be writable, write to it, and then restore the permissions.
            MEMORY_BASIC_INFORMATION memDesc = {0};
            ::VirtualQuery(pFunc, &memDesc, sizeof(MEMORY_BASIC_INFORMATION));
            // NOTE: (sonictk) Try to set the permissions to be R/W. Old permissions are stored in the Protect member.
            if (!::VirtualProtect(memDesc.BaseAddress, memDesc.RegionSize, PAGE_READWRITE, &memDesc.Protect)) {
                return false;
            }

            // NOTE: (sonictk) Now store our filter and overwrite the current one.
            HANDLE hProcess = GetCurrentProcess();
            ::WriteProcessMemory(hProcess, pFunc, &pfnNew, sizeof(pfnNew), NULL);

            // NOTE: (sonictk) Restore memory page protections.
            DWORD dwOldProtect = 0;
            ::VirtualProtect(memDesc.BaseAddress, memDesc.RegionSize, memDesc.Protect, &dwOldProtect);
            return true;
        }
        ++pThunk;
    }

    return false;
}


bool patchOverIATEntriesInAllModules(const char *calleeModName, PROC pfnCurrent, PROC pfnNew)
{
    // NOTE: (sonictk) We get a handle to the current module by finding which region of memory
    // contains this function being run, and gettng a handle to its allocation base.
    // MEMORY_BASIC_INFORMATION mbi;
    // SIZE_T bufSize = VirtualQuery(patchOverIATEntriesInAllModules, &mbi, sizeof(mbi));
    // if (bufSize == 0) {
    //     return false;
    // }
    // HMODULE thisMod = (HMODULE)mbi.AllocationBase;

    DWORD currentProcId = ::GetCurrentProcessId();
    // NOTE: (sonictk) We use CreateToolhelp32Snapshot instead of EnumProcessModules since
    // it has less bookkeeping requirements and will take care of the memory
    // management. This isn't a bleeding-edge performance requirement.
    HANDLE hProcSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, currentProcId);

    MODULEENTRY32 modEntry = {0};
    modEntry.dwSize = sizeof(modEntry);

    for (BOOL bStat = Module32First(hProcSnapshot, &modEntry); bStat == TRUE; bStat = Module32Next(hProcSnapshot, &modEntry)) {
        // if (modEntry.hModule == thisMod) {
        //     continue;
        // }

        patchOverIATEntryInOneModule(calleeModName, pfnCurrent, pfnNew, modEntry.hModule);
    }

    ::CloseHandle(hProcSnapshot);

    return true;
}


/**
 * We patch over the ``SetUnhandledExceptionFilter`` function in the kernel32.dll's import address table
 * so that any calls to it end up calling ``pFnFilterReplace`` instead. This used to work to invoke our
 * exception filter on CRT exceptions as well (using VS 2008), but doesn't anymore.
 *
 * @param pFnFilterReplace    The filter to replace the existing one with.
 * @param pOrigFilter         A pointer to a storage for the original filter that is to be replaced.
 *
 * @return                    ``true`` if the operation was successful, ``false`` otherwise.
 */
bool patchOverUnhandledExceptionFilter(PROC pFnFilterReplace, FARPROC *pOrigFilter)
{
    // HMODULE hMayaExe = NULL;
    // ::GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, NULL, &hMayaExe);
    // assert(hMayaExe != NULL);

    HMODULE hKernel32Mod = NULL;
    // NOTE: (sonictk) Kernel32.dll should be guaranteed to have been loaded by Maya at this point.
    ::GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, "kernel32.dll", &hKernel32Mod);
    if (hKernel32Mod == NULL) {
        return false;
    }
    if (*pOrigFilter == NULL) {
        *pOrigFilter = ::GetProcAddress(hKernel32Mod, "SetUnhandledExceptionFilter");
        if (*pOrigFilter == NULL) {
            return false;
        }
    }

    bool bStat = patchOverIATEntriesInAllModules("kernel32.dll", *pOrigFilter, pFnFilterReplace);
    return bStat;
}


MStatus initializePlugin(MObject obj)
{
    MFnPlugin plugin(obj, PLUGIN_AUTHOR, PLUGIN_VERSION, PLUGIN_REQUIRED_API_VERSION);

    // NOTE: (sonictk) All the vectored handlers will be called first before any unhandled exception filters.
    gpVectoredHandler = (PVECTORED_EXCEPTION_HANDLER)::AddVectoredExceptionHandler(1, mayaCustomVectoredExceptionHandler);

    gPrevFilter = ::SetUnhandledExceptionFilter(mayaCustomUnhandledExceptionFilter);

    // NOTE: (sonictk) Fix up kernel32.dll and patch the address of SetUnhandledExceptionFilter
    // so that the CRT can't call SetUnhandledExceptionFilter(0) to remove our custom crash
    // handler before it itself crashes. This will also have the effect of making sure
    // _no one else_ can set yet another unhandled exception filter, as long as our plugin
    // is loaded first before anyone tries to make a call to SetUnhandledExceptionFilter.
    // We do this by literally patching the Import Address Table in kernel32.dll, which
    // contains the SetUnhandledExceptionFilter for the CRT.
    gCRTFilterPatched = patchOverUnhandledExceptionFilter((PROC)detouredSetUnhandledExceptionFilter, &gOrigCRTFilter);
    if (!gCRTFilterPatched) {
        MGlobal::displayError("Could not patch over the CRT unhandled exception filter. CRT exceptions will not be handled by this plugin.");
    }

    // TODO: (sonictk) Should I just register all the other possible CRT handlers, along with
    // signal handlers from the CRT as well?
    // https://www.codeproject.com/Articles/207464/Exception-Handling-in-Visual-Cplusplus

    // NOTE: (sonictk) One way to get our exception handler invoked: format the exception record ourselves and call the exception filter with it.
    gOrigPurecallHandler = _set_purecall_handler([]() {
      EXCEPTION_POINTERS *ppExceptionPointers = NULL;
      GetExceptionPointers(EXCEPTION_NONCONTINUABLE, &ppExceptionPointers);
      // NOTE: (sonictk) Here, it might be a good idea to have a different exception handler instead so that we can stuff in information
      // about the pure virtual function call and the call site, etc.
      mayaCustomVectoredExceptionHandler(ppExceptionPointers);
      ::ExitProcess(0);
    });

    // NOTE: (sonictk) Other way: just raise a SEH exception which will trigger our unhandled exception filter to be caslled anyway!
    gOrigAbortHandler = signal(SIGABRT, [](int signal){ (void)signal; ::RaiseException(SIGABRT, EXCEPTION_NONCONTINUABLE, 0, NULL); });

    // NOTE: (sonictk) Test that our detour-ing function works. If it is working,
    // unwantedUnhandledExceptionFilter should never get called.
    ::SetUnhandledExceptionFilter(unwantedUnhandledExceptionFilter);

    MGlobal::displayInfo("Custom Maya unhandled exception filter/handler(s) registered successfully.");

    // NOTE: (sonictk) Install scene callbacks to set static variables in the data segment
    // that will actually be written into the dump. Why do we do this? Well, during a crash,
    // we're unwinding the stack, and the last thing we want to do is call into Maya functions
    // to grow the stack again...especially if the crash was due to stack overflow or stack
    // corruption. Let's just use the data from the .data segment of the DLL, which should
    // be (hopefully) free from most types of memory corruption.
    // Aside; this is also why we're using fixed size buffers in the .bss segment instead of
    // dynamically allocating memory to hold the information we want to write out.
    MStatus mstat;
    gMayaSceneAfterOpen_cbid = MSceneMessage::addCallback(MSceneMessage::kAfterOpen, mayaSceneAfterOpenCB, NULL, &mstat);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    gMayaTimeChange_cbid = MDGMessage::addTimeChangeCallback(mayaSceneTimeChangeCB, NULL, &mstat);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    gMayaMELCmd_cbid = MCommandMessage::addProcCallback(mayaMELCmdCB, NULL, &mstat);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    gMayaAllDAGChanges_cbid = MDagMessage::addAllDagChangesCallback(mayaAllDAGChangesCB, NULL, &mstat);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    gMayaNodeAdded_cbid = MDGMessage::addNodeAddedCallback(mayaNodeAddedCB, "dependNode", NULL, &mstat);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    // NOTE: (sonictk) We'll trigger the callbacks immediately anyway so that even on a fresh load of the plugin,
    // we get some basic information about the Maya session.
    mayaSceneAfterOpenCB(NULL);
    MTime curTime = MAnimControl::currentTime();
    mayaSceneTimeChangeCB(curTime, NULL);

    mstat  = plugin.registerCommand(MAYA_FORCE_CRASH_CMD_NAME,
                                    MayaForceCrashCmd::creator,
                                    MayaForceCrashCmd::newSyntax);

    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    return mstat;
}


MStatus uninitializePlugin(MObject obj)
{
    if (gpVectoredHandler != NULL) {
        ULONG stat = ::RemoveVectoredExceptionHandler(gpVectoredHandler);
        if (stat == 0) {
            MGlobal::displayError("Could not remove the vectored exception handler.");
            return MStatus::kFailure;
        }
    }

    // NOTE: (sonictk) Also re-patch the kernel32.dll's version to the original CRT one.
    if (gCRTFilterPatched) {
        FARPROC customUnhandledExceptionFilter = (FARPROC)detouredSetUnhandledExceptionFilter;
        bool bStat = patchOverUnhandledExceptionFilter((PROC)gOrigCRTFilter, &customUnhandledExceptionFilter);
        if (!bStat) {
            MGlobal::displayError("Could not restore the original CRT exception filter.");
            return MStatus::kFailure;
        }
    }

    // NOTE: (sonictk) We'll un-register our exception filter and restore the original Maya
    // one instead.
    ::SetUnhandledExceptionFilter(gPrevFilter);
    _set_purecall_handler(gOrigPurecallHandler);
    signal(SIGABRT, gOrigAbortHandler);

    MStatus mstat = MMessage::removeCallback(gMayaSceneAfterOpen_cbid);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    mstat = MMessage::removeCallback(gMayaTimeChange_cbid);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    mstat = MMessage::removeCallback(gMayaMELCmd_cbid);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    mstat = MMessage::removeCallback(gMayaAllDAGChanges_cbid);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    mstat = MMessage::removeCallback(gMayaNodeAdded_cbid);
    CHECK_MSTATUS_AND_RETURN_IT(mstat);

    MGlobal::displayInfo("All Maya custom unhandled exception filter(s) unregistered successfully.");

    MFnPlugin plugin(obj);
    mstat = plugin.deregisterCommand(MAYA_FORCE_CRASH_CMD_NAME);

    return mstat;
}
