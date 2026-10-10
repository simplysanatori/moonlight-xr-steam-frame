// Moonlight XR Native: the launcher's state machine, without the network or
// the headset. What Moonlight's PcView and AppView activities do, for the
// in-headset PC_LIST and APP_LIST panels: the computers from hosts.ini with
// their online state, a computer's apps, launching or resuming one, and where
// leaving a stream goes (back to the app list, the host's session ended).
// The app's worker thread does the network side, takes commands from here and
// reports back; the frame loop presses the lists and reads the view. Not
// thread safe: the caller locks. GPLv3.
#pragma once
#include "panels.h"

#define LAUNCH_MAX_PCS 16
#define LAUNCH_MAX_APPS 96

typedef enum {
    LAUNCH_PCS = 0,     // the PC list
    LAUNCH_APPS,        // a PC's app list
    LAUNCH_STARTING,    // launching or resuming an app, the stream coming up
    LAUNCH_STREAMING,   // the stream is the picture
    LAUNCH_STOPPING,    // the stream going down and the host's session ending
    LAUNCH_QUIT         // the app quits
} LaunchState;

typedef struct {
    char name[64];
    int online;         // UI_PC_OFFLINE / UI_PC_ONLINE / UI_PC_REFRESHING
    int paired;
    int canWake;        // a MAC is stored
    int currentGame;    // the app id the host is running, 0 none
} LaunchPc;

typedef struct {
    char name[96];
    int id;
} LaunchApp;

typedef enum {
    LCMD_NONE = 0,
    LCMD_REFRESH_PCS,   // serverinfo on every PC
    LCMD_FETCH_APPS,    // pc: its applist
    LCMD_LAUNCH,        // pc, appId: launch or resume, start the stream (quitFirst: end another app first)
    LCMD_STOP,          // stop the stream; quitHost: end the host's session too
    LCMD_WAKE,          // pc: Wake-on-LAN, then refresh
    LCMD_EXIT           // quit the app
} LaunchCmdKind;

typedef struct {
    LaunchCmdKind kind;
    int pc;
    int appId;
    int quitFirst;
    int quitHost;
} LaunchCmd;

typedef struct Launcher {
    LaunchState state;
    LaunchPc pcs[LAUNCH_MAX_PCS];
    int pcCount;
    int pcPage;
    int pc;                 // the PC the app list is of, -1 none
    LaunchApp apps[LAUNCH_MAX_APPS];
    int appCount;
    int appPage;
    int appsLoading;
    int appsFailed;
    int startingApp;        // the app id being launched
    int fileMode;           // --file: no hosts, exit quits
    LaunchCmd cmds[8];
    int ncmd;
    char notice[160];       // a toast for the frame loop to show, "" none
    // The panels' items, rebuilt by launcher_view
    UiPcItem pcItems[LAUNCH_MAX_PCS];
    UiAppItem appItems[LAUNCH_MAX_APPS];
} Launcher;

void launcher_init(Launcher* l);
// --file: the picture is up from the start and exit quits
void launcher_init_file(Launcher* l);

// ---- what the worker reports ----
void launcher_hosts(Launcher* l, const LaunchPc* pcs, int n);
void launcher_pc_status(Launcher* l, int pc, int online, int paired, int currentGame);
void launcher_apps(Launcher* l, int pc, const LaunchApp* apps, int n, int ok);
// --host/--app: launch this as soon as the hosts are known (by the worker)
void launcher_autostart(Launcher* l, int pc, int appId);
void launcher_stream_started(Launcher* l);
void launcher_stream_failed(Launcher* l, const char* why);
// The stream went down: stopped by the exit, or ended by the host
void launcher_stream_ended(Launcher* l, int byHost);

// ---- what the headset does ----
// A press on the list (UiHit kind, a, b from the panel's hit regions)
void launcher_press(Launcher* l, int kind, int a, int b);
// The exit prompt's Exit: leaves the stream for the app list (ending the
// host's session), or quits the app from the lists
void launcher_exit(Launcher* l);
// Leaves the stream but not the host's session (the gamepad chord Start+Select+
// L2+R2, "disconnect your session"): LCMD_STOP with quitHost 0, then the app
// list. Outside a stream it does nothing; in file mode it behaves as exit.
void launcher_disconnect(Launcher* l);
// The same from a physical keyboard's Ctrl+Alt+Shift+Q (Moonlight's own
// keyboard combo), with its own notice
void launcher_disconnect_keys(Launcher* l);

// ---- what the worker and the frame loop take ----
int launcher_take_cmd(Launcher* l, LaunchCmd* out);
// Which list to show (0 none, 1 PCs, 2 apps), its items and its state
typedef struct {
    int mode;
    const UiPcItem* pcs;
    int pcCount;
    const char* pcName;
    const UiAppItem* apps;
    int appCount;
    int page;
    int loading, failed;
} LaunchView;
void launcher_view(Launcher* l, LaunchView* v);
const char* launcher_state_name(LaunchState s);
