// Report a problem, in one step: the report sheet's Save and zip runs
// bug-report.sh (the masked zip of the logs, the notes, the settings you
// changed and, when MLXR_RAY_DEBUG made one, the ray CSV) and shows where the
// zip is. The script runs off the frame loop; the frame loop polls for the end.
//
// Native (lane wave3-hud). PLAN 3.10. GPLv3.
#ifndef XR_BUGZIP_H
#define XR_BUGZIP_H

#include <stddef.h>

// The zip's path from what the script printed ("wrote <path>" on a line of its
// own, the last one counts). 1 and the path, or 0.
int xrBugzipParse(const char* output, char* path, size_t cap);

// Where bug-report.sh is: MLXR_BUG_REPORT_SCRIPT, else beside this program (the installer puts it there),
// else tools/ of the source tree this build sits in, else the installed app's. 1 and the path, or 0.
int xrBugzipFindScript(char* out, size_t cap);

// Runs the script on this thread, giving it reportDir to write to (NULL: its own default). 0 when it
// made a zip, with the path in result; else -1 with a sentence for the log in result.
int xrBugzipRun(const char* script, const char* reportDir, char* result, size_t cap);

// Starts that on a thread of its own with the script xrBugzipFindScript names. 1 when it started, 0 when
// the last one is still running.
int xrBugzipStart(const char* reportDir);

// Frame loop: 0 while nothing has finished; 1 once, when a run ended, with ok set and the zip's path (or the
// sentence saying why not) in result.
int xrBugzipPoll(int* ok, char* result, size_t cap);

#endif
