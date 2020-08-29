/**
 * @file   maya_custom_unhandled_exception_filter_cmd.cpp
 * @brief  A command to forcibly crash Maya in various ways in order to test our
 *         custom unhandled exception filter.
 */
#include "maya_custom_unhandled_exception_filter_cmd.h"

#include <maya/MArgDatabase.h>


void *MayaForceCrashCmd::creator()
{
    MayaForceCrashCmd *cmd = new MayaForceCrashCmd();

    cmd->flagHelp = false;
    cmd->crashType = MayaForceCrashType_NoCrash;

    return cmd;
}


MSyntax MayaForceCrashCmd::newSyntax()
{
    MSyntax syntax;

    syntax.enableQuery(false);
    syntax.enableEdit(false);
    syntax.useSelectionAsDefault(false);

    syntax.addFlag(MAYA_CRASH_CMD_HELP_FLAG_SHORTNAME,
                   MAYA_CRASH_CMD_HELP_FLAG_NAME);

    syntax.addFlag(MAYA_CRASH_CMD_CRASH_TYPE_FLAG_SHORTNAME,
                   MAYA_CRASH_CMD_CRASH_TYPE_FLAG_NAME,
                   MSyntax::kLong);

    return syntax;
}


MStatus MayaForceCrashCmd::parseArgs(const MArgList &args)
{
    MStatus result;

    MArgDatabase argDb(this->syntax(), args, &result);
    CHECK_MSTATUS_AND_RETURN_IT(result);

    if (argDb.isFlagSet(MAYA_CRASH_CMD_HELP_FLAG_SHORTNAME)) {
        MGlobal::displayInfo(MAYA_CRASH_CMD_HELP_TEXT);
        this->flagHelp = true;
        return MStatus::kSuccess;
    }

    if (argDb.isFlagSet(MAYA_CRASH_CMD_CRASH_TYPE_FLAG_SHORTNAME)) {
        result = argDb.getFlagArgument(MAYA_CRASH_CMD_CRASH_TYPE_FLAG_SHORTNAME, 0, this->crashType);
        CHECK_MSTATUS_AND_RETURN_IT(result);
    }

    return result;
}


#pragma warning(disable : 4717)
__declspec(noinline) void StackOverflow1 (volatile unsigned int* param)
{
  volatile unsigned int dummy[256];
  dummy[*param] %= 256;

  StackOverflow1 (&dummy[*param]);
}



// TODO: (sonictk) Make sure that each of these crash types will invoke the unhandled exception filter.
MStatus MayaForceCrashCmd::redoIt()
{
    MStatus result = MStatus::kSuccess;

    switch (this->crashType) {
    case MayaForceCrashType_NullPtrDereference:
    {
        // NOTE: (sonictk) Ok, let's trigger a crash now to test our exception handler.
        // The simplest method here is a NULL ptr deference operation, which should result in
        // an access violation.
        char *p = NULL;
        *p = 5;
        break;
    }
    case MayaForceCrashType_Abort: // TODO: (sonictk) This doesn't seem to call it either.
    {
        // NOTE: (sonictk) Ok, for more comprehensiveness, let's call some CRT functions
        // that _normally_ would not invoke our exception filter:
        abort(); // NOTE: (sonictk) This is normally called internally in the CRT.
        break;
    }
    case MayaForceCrashType_OutOfBoundsAccess:
    {
        std::vector<int> v;
        v[0] = 5; // NOTE: (sonictk) Out of bounds vector access is normally caught in the CRT.
        break;
    }
    case MayaForceCrashType_StackCorruption:
    {
        // NOTE: (sonictk) Simulate stack corruption. _AddressOfReturnAddress provides the
        // address of the memory location that holds the return address of the current function.
        *(uintptr_t *)_AddressOfReturnAddress() = 0x1234;
        break;
    }
    case MayaForceCrashType_PureVirtualFuncCall: // TODO: (sonictk) This doesn't seem to trigger even the patched CRT handlers
    {
        // NOTE: (sonictk) Simulate a pure virtual function call.
        struct A
        {
            A() { bar(); }
            virtual void foo() = 0;
            void bar() { foo(); }
        };

        struct B: A
        {
            void foo() {}
        };

        A *a = new B;
        a->foo();
        break;
    }
    case MayaForceCrashType_StackOverflow: // TODO: (sonictk) Also doesn't get caught by the handlers.
    {
        unsigned int initial = 3;
        StackOverflow1(&initial);
        break;
    }
    case MayaForceCrashType_NoCrash:
    default:
        MGlobal::displayWarning("Invalid crash type specified.");
        break;
    }

    return result;
}


MStatus MayaForceCrashCmd::doIt(const MArgList &args)
{
    this->clearResult();

    MStatus stat = this->parseArgs(args);
    CHECK_MSTATUS_AND_RETURN_IT(stat);

    if (this->flagHelp == true) {
        return MStatus::kSuccess;
    }

    return this->redoIt();
}


MStatus MayaForceCrashCmd::undoIt()
{
    return MStatus::kSuccess;
}


bool MayaForceCrashCmd::isUndoable() const
{
    return false;
}
