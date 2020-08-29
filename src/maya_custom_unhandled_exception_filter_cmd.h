#ifndef MAYA_CUSTOM_UNHANDLED_EXCEPTION_FILTER_CMD_H
#define MAYA_CUSTOM_UNHANDLED_EXCEPTION_FILTER_CMD_H

#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>
#include <maya/MArgList.h>

#define MAYA_FORCE_CRASH_CMD_NAME "mayaForceCrash"
#define MAYA_CRASH_CMD_HELP_FLAG_SHORTNAME "-h"
#define MAYA_CRASH_CMD_HELP_FLAG_NAME "-help"

#define MAYA_CRASH_CMD_CRASH_TYPE_FLAG_SHORTNAME "-ct"
#define MAYA_CRASH_CMD_CRASH_TYPE_FLAG_NAME "-crashType"

#define MAYA_CRASH_CMD_HELP_TEXT "Triggers a crash for debugging purposes."


enum MayaForceCrashType
{
    MayaForceCrashType_NoCrash = 0,
    MayaForceCrashType_NullPtrDereference,
    MayaForceCrashType_Abort,
    MayaForceCrashType_OutOfBoundsAccess,
    MayaForceCrashType_StackCorruption,
    MayaForceCrashType_PureVirtualFuncCall,
    MayaForceCrashType_StackOverflow
};


struct MayaForceCrashCmd : public MPxCommand
{
    /**
     * Creates a new instance of the command. Used for Maya plugin registration.
     *
     * @return  A pointer to the new instance.
     */
    static void *creator();

    /**
     * This function parses the arguments that were given to the command and stores
     * it in local class data. It finally calls ``redoIt`` to implement the actual
     * command functionality.
     *
     * @param args  The arguments that were passed to the command.
     * @return      The status code.
     */
    MStatus doIt(const MArgList &args);

    /**
     * This function implements the actual functionality of the command. It is also
     * Called when the user elects to perform an interactive red o of the command.
     *
     * @return      The status code.
     */
    MStatus redoIt();

    /**
     * This function is called when the user performs an undo of the command. It
     * restores the scene to its earlier state before the command was run.
     *
     * @return      The status code.
     */
    MStatus undoIt();

    /**
     * This function specifies that the command is undoable in Maya.
     *
     * @return  ``true``, as this command is undoable.
     */
    bool isUndoable() const;

    /**
     * This static function returns the syntax object for this command.
     *
     * @return The syntax object set up for this command.
     */
    static MSyntax newSyntax();

    /**
     * This function parses the given arguments to the command and stores the
     * results in local class data.
     *
     * @param args      The arguments that were passed to the command.
     * @return          The status code.
     */
    MStatus parseArgs(const MArgList &args);

    bool flagHelp;
    int crashType;
};


#endif /* MAYA_CUSTOM_UNHANDLED_EXCEPTION_FILTER_CMD_H */
