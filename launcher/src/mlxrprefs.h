// Moonlight XR Native launcher: the settings of the Artemis-style Settings page that moonlight-qt's
// StreamingPreferences does not have. New file, added to app/ by launcher/build.sh. GPLv3.
//
// Two homes, so every setting shown is honoured by the VR client:
//   * launcher settings (this app's QSettings, keys "mlxr/..."): passed to the VR client on each
//     stream as command line flags (clientArgs(), see app/main.c) or used by the launcher itself
//     (clipboard sync, quick resume)
//   * the VR client's own settings in ~/.config/moonlight-xr/config.ini (3D, stats overlay, face
//     button flip): read from and written to that file, so the headset's cog and this page show
//     the same value. Only a key whose value was changed here is rewritten (save()); the rest of
//     the file is kept byte for byte.
// The stream basics (resolution, frame rate, bitrate, codec, audio, host options, language) stay in
// StreamingPreferences under their stock keys, so existing configs keep working.
#pragma once

#include <QMap>
#include <QObject>
#include <QVariantMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariantList>

class MlxrPrefs : public QObject
{
    Q_OBJECT

    // ---- launcher settings -> VR client flags ----
    // Artemis frame_pacing: "latency" (newest frame), "balanced" (the VR client's low latency pacer,
    // its default), "smoothness" (moonlight-qt's pacer)
    Q_PROPERTY(QString framePacing MEMBER framePacing NOTIFY changed)
    Q_PROPERTY(bool ultraLowLatency MEMBER ultraLowLatency NOTIFY changed)   // --video-priority rt
    Q_PROPERTY(bool virtualDisplay MEMBER virtualDisplay NOTIFY changed)     // Apollo
    Q_PROPERTY(int scaleFactor MEMBER scaleFactor NOTIFY changed)            // Apollo, 20..200
    Q_PROPERTY(QString customRefreshRate MEMBER customRefreshRate NOTIFY changed) // Apollo, "" = off
    // "Match headset" (default): the VR client asks the host for the display's rate (--fps match);
    // off: StreamingPreferences' fps, or the custom refresh rate
    Q_PROPERTY(bool matchHeadsetFps MEMBER matchHeadsetFps NOTIFY changed)
    Q_PROPERTY(bool fullRange MEMBER fullRange NOTIFY changed)
    Q_PROPERTY(bool rumble MEMBER rumble NOTIFY changed)
    Q_PROPERTY(int deadzone MEMBER deadzone NOTIFY changed)                  // -20..20 percent
    Q_PROPERTY(bool mouseNavButtons MEMBER mouseNavButtons NOTIFY changed)
    // The PC link: "auto" (default), "home" (Wi-Fi), "adapter" (the Steam Frame Wireless
    // adapter), "usb" (a USB data cable to the PC; Native, lane wave4c-usb). Key "networkPath" (no "mlxr/" group): lane network-path's hand-off reads it and passes
    // --network to the VR client; this page only stores it
    Q_PROPERTY(QString networkPath MEMBER networkPath NOTIFY changed)
    // ---- launcher settings used by the launcher ----
    Q_PROPERTY(bool clipboardSync MEMBER clipboardSync NOTIFY changed)       // Apollo
    Q_PROPERTY(bool quickResume MEMBER quickResume NOTIFY changed)
    // Native (lane wave4-launcher): ask the PC about permissions, the virtual display and its modes before a stream
    // starts and say what is wrong (mlxrpreflight.h); on by default. Not part of a settings profile.
    Q_PROPERTY(bool preflight MEMBER preflight NOTIFY changed)
    // ---- the VR client's config.ini ----
    Q_PROPERTY(bool perfOverlay MEMBER perfOverlay NOTIFY changed)           // [display] stats
    Q_PROPERTY(int renderMode MEMBER renderMode NOTIFY changed)              // [3d] stereo: 0 2D, 1 AI 3D
    Q_PROPERTY(int strength MEMBER strength NOTIFY changed)                  // [3d] separation, as 1..100
    Q_PROPERTY(bool strengthIsDefault MEMBER strengthIsDefault NOTIFY changed)
    Q_PROPERTY(int convergence MEMBER convergence NOTIFY changed)            // [3d] convergence, 0..100
    Q_PROPERTY(bool convergenceIsDefault MEMBER convergenceIsDefault NOTIFY changed)
    // Native (lane wave3-visual): [3d] eye_balance, -100..100 percent: how the depth shift is shared by the
    // eyes, 0 evenly, +100 all in the left eye, -100 all in the right (config and this mirror only, the
    // headset's 3D tab has no row for it)
    Q_PROPERTY(int eyeBalance MEMBER eyeBalance NOTIFY changed)
    // Native (lane wave3-visual): the surroundings. [vr] background_dim, 0..100 percent the room, the
    // passthrough or the void is dimmed; [vr] void_color, "#rrggbb", the void's colour; [display]
    // glow_feather, "off", "soft" or "wide", the picture's edge faded into the glow
    Q_PROPERTY(int backgroundDim MEMBER backgroundDim NOTIFY changed)
    Q_PROPERTY(QString voidColor MEMBER voidColor NOTIFY changed)
    Q_PROPERTY(QString glowFeather MEMBER glowFeather NOTIFY changed)
    // Native (PLAN 4.3): [3d] stereo_source, where the 3D comes from: "depth" (default, the depth model),
    // "sbs" (the PC renders a side by side pair) or "top-bottom"; [3d] sbs_full: that pair holds a full
    // size picture per eye (3840x1080) rather than two squeezed halves (1920x1080)
    Q_PROPERTY(QString stereoSource MEMBER stereoSource NOTIFY changed)
    Q_PROPERTY(bool sbsFull MEMBER sbsFull NOTIFY changed)
    // Native (lane wave5-depth, PLAN 5.1): [3d] edge_sharpness, the Edge setting, 0..100 (0, the
    // default, is the soft edges the 3D always had)
    Q_PROPERTY(int edgeSharpness MEMBER edgeSharpness NOTIFY changed)
    // Native (lane wave5-depth, PLAN 5.4): [3d] motion_reset, percent 0..50 (0, the default, off)
    Q_PROPERTY(int motionReset MEMBER motionReset NOTIFY changed)
    // Native (lane wave5-depth, PLAN 5.6): [3d] depth_scale (upstream's depth scale) and depth_global,
    // percent 0..200 (100, the default, the depth as the model makes it)
    Q_PROPERTY(int depthScale MEMBER depthScale NOTIFY changed)
    Q_PROPERTY(int depthGlobal MEMBER depthGlobal NOTIFY changed)
    Q_PROPERTY(bool flipFaceButtons MEMBER flipFaceButtons NOTIFY changed)   // [vr] flip_face_buttons
    // [vr] keyboard_keymap: what the headset keyboard's keys say, "qwerty" (default), "azerty" or "qwertz"
    Q_PROPERTY(QString keyboardKeymap MEMBER keyboardKeymap NOTIFY changed)
    // Which notices the headset's toast says: "all", "important" or "off" ([vr] notices)
    Q_PROPERTY(QString notices MEMBER notices NOTIFY changed)
    // How fast a controller thumbstick scrolls in the headset, percent of 7.5 notches a
    // second, 25..400 ([vr] scroll_speed)
    Q_PROPERTY(int scrollSpeed MEMBER scrollSpeed NOTIFY changed)
    // Native (lane wave4b-zoom, PLAN 4b.6): the zoom a stream starts at in the headset, percent, 100..400
    // ([display] zoom). The headset keeps the zoom and the pan per PC and app once they are set there
    Q_PROPERTY(int zoom MEMBER zoom NOTIFY changed)
    // Which controller the headset draws in each hand: "off", "frame", "gun" or "custom"
    // ([vr] controller_model; an old "generic" reads as "frame"). Custom (your own files in the models
    // folder) is only chosen here and in the file; the headset's Display tab has a cell for the other three
    Q_PROPERTY(QString controllerModel MEMBER controllerModel NOTIFY changed)
    // Native (lane wave3-session): [stream] doff_disconnect (seconds with the headset off before the stream is
    // disconnected, the PC's app keeps running; 0 never, default 60) and idle_disconnect (minutes without input
    // before the same; 0 never, default 0)
    Q_PROPERTY(int doffDisconnect MEMBER doffDisconnect NOTIFY changed)
    Q_PROPERTY(int idleDisconnect MEMBER idleDisconnect NOTIFY changed)
    // Native (lane wave4b-abr): [stream] bitrate_wifi, bitrate_adapter, bitrate_wired, bitrate_usb: the start bitrate in
    // kbps for the network path the stream goes over; 0 uses the Video bitrate above (500..500000 otherwise). The
    // VR client reads them from config.ini when the stream starts (a running stream's bitrate cannot change)
    Q_PROPERTY(int bitrateWifi MEMBER bitrateWifi NOTIFY changed)
    Q_PROPERTY(int bitrateAdapter MEMBER bitrateAdapter NOTIFY changed)
    Q_PROPERTY(int bitrateWired MEMBER bitrateWired NOTIFY changed)
    Q_PROPERTY(int bitrateUsb MEMBER bitrateUsb NOTIFY changed)
    // Native (lane wave1-burst): [stream] rfi_idr_ms, at most one full-frame (IDR) request of the headset's
    // own per this many ms after lost video (0 no limit, default 500, 0..5000)
    Q_PROPERTY(int rfiIdrMs MEMBER rfiIdrMs NOTIFY changed)
    // Native (lane wave1-failover): [stream] failover ("off" default, "on"): when the video keeps losing packets on
    // the link a stream started on, move it to another link that measured clearly better at its start; and
    // [stream] failover_burst_ms, how many ms of loss within 10 s make it leave (500..9000, default 3000). The
    // headset reads both while the stream runs
    Q_PROPERTY(bool pathFailover MEMBER pathFailover NOTIFY changed)
    Q_PROPERTY(int failoverBurstMs MEMBER failoverBurstMs NOTIFY changed)
    // Native (lane wave2b-padmouse): physical gamepads in the headset. [vr] pad_mouse_chord: what turns a pad
    // into the PC's mouse, "start_hold" (default), "start_back" or "off"; [vr] pad_mouse_scroll: the stick that
    // scrolls then, "right" (default), "left" or "none"; [vr] pad_arbitration: what a used pad does to the
    // headset controllers' gamepad, "pointer" (default), "also_gamepad" or "sleep"
    Q_PROPERTY(QString padMouseChord MEMBER padMouseChord NOTIFY changed)
    Q_PROPERTY(QString padMouseScroll MEMBER padMouseScroll NOTIFY changed)
    Q_PROPERTY(QString padArbitration MEMBER padArbitration NOTIFY changed)
    // Native (lane wave6-i18n): [ui] language, the headset UI's language: "auto" (default, the headset's
    // locale), "en", or a code of tools/ui/gen_strings.py's list ("de", "pt-BR", ...). Read by the VR
    // client when it starts
    Q_PROPERTY(QString headsetLanguage MEMBER headsetLanguage NOTIFY changed)
    // Native (lane wave3-cards): [vr] pad_card: the gamepad card in the headset's lap, "auto" (default: up while
    // a gamepad is in use, gone 4 s after the last input), "on" or "off"; [vr] first_run_shown: the first-run
    // controls card has been shown (false brings it back on the next stream)
    Q_PROPERTY(QString padCard MEMBER padCard NOTIFY changed)
    Q_PROPERTY(bool firstRunShown MEMBER firstRunShown NOTIFY changed)
    // Native (lane wave2-lightgun-gyro): [vr] lightgun, the lightgun pointer policy, "auto" (default: on while the
    // controller model is the gun), "on" or "off"; [vr] pad_gyro, what the headset controllers' gamepad sends as
    // gyro to the PC, "off" (default), "controller" or "head". The rest (lightgun_reload, lightgun_recoil,
    // pad_gyro_ratchet) are in the file only
    Q_PROPERTY(QString lightgun MEMBER lightgun NOTIFY changed)
    Q_PROPERTY(QString padGyro MEMBER padGyro NOTIFY changed)
    // Native (lane wave4-headinput): head and pointer input of the VR client, all [vr] and off by default.
    // head_look (PLAN 4.4): the head's turn moves the PC's mouse in pointer mode with the screen head
    // locked or a room up; head_look_px_deg its pixels a degree, 1..30 (default 8)
    Q_PROPERTY(bool headLook MEMBER headLook NOTIFY changed)
    Q_PROPERTY(int headLookPxDeg MEMBER headLookPxDeg NOTIFY changed)
    // opentrack (PLAN 4.5): the head pose to opentrack on the PC over UDP while streaming; opentrack_host
    // where to ("" the PC of the current stream), opentrack_port its UDP port, 1..65535 (default 4242)
    Q_PROPERTY(bool opentrack MEMBER opentrack NOTIFY changed)
    Q_PROPERTY(QString opentrackHost MEMBER opentrackHost NOTIFY changed)
    Q_PROPERTY(int opentrackPort MEMBER opentrackPort NOTIFY changed)
    // pointer_source (PLAN 6.5): "controller" (default), "head" or "gaze"
    Q_PROPERTY(QString pointerSource MEMBER pointerSource NOTIFY changed)
    // Native (lane headinput-finish, PLAN 4b.10): mouse_mode "absolute" (default) or "relative";
    // back_as_guide, gamepad mode's Back (View) button as Guide
    Q_PROPERTY(QString mouseMode MEMBER mouseMode NOTIFY changed)
    Q_PROPERTY(bool backAsGuide MEMBER backAsGuide NOTIFY changed)
    // Native (lane a11y-tab): head_dwell_ms, the dwell click of the head or eyes pointer: 0 (default, off) or
    // 400..2000; the Accessibility section offers 600, 900, 1200 and 1500. Written only when changed here.
    Q_PROPERTY(int headDwellMs MEMBER headDwellMs NOTIFY changed)
    Q_PROPERTY(int assistDotSize MEMBER assistDotSize NOTIFY changed)   // native (lane a11y-2)
    Q_PROPERTY(bool assistHeadsetButton MEMBER assistHeadsetButton NOTIFY changed)
    // ---- Native (lane launcher-bundle, PLAN 6.7): the rest of the VR client's config.ini ----
    // Every key of ui/settings.c that a user would set and that had no row of its own: picture, pointer,
    // keyboard, gamepad mode, the screen and its surroundings (the table is kVrKeys in mlxrprefs.cpp; tests
    // check it against the VR client's own table). vr["section.key"] is the value as the VR client reads it:
    // a bool, an int, or an enum's name. setVr() takes the same (or text) and holds it to the key's range;
    // save() writes only the keys changed here. Not part of a settings profile.
    Q_PROPERTY(QVariantMap vr READ vr NOTIFY changed)
    // ---- Native (lane launcher-bundle, PLAN 6.2 and 6.4) ----
    // Once a day at most, ask GitHub for the latest release (mlxrupdate.h); on by default. Key mlxr/check_updates.
    Q_PROPERTY(bool checkUpdates MEMBER checkUpdates NOTIFY changed)
    // What the launcher does once a PC is online after it starts (mlxronstart.h): "none" (default), "desktop" (the
    // Desktop app of the PC the last stream went to) or "app" (onStartApp on onStartPc). Keys mlxr/on_start,
    // mlxr/on_start_pc, mlxr/on_start_app; the last stream's PC is mlxr/last_pc (written by the hand-off)
    Q_PROPERTY(QString onStart MEMBER onStart NOTIFY changed)
    Q_PROPERTY(QString onStartPc MEMBER onStartPc NOTIFY changed)
    Q_PROPERTY(QString onStartApp MEMBER onStartApp NOTIFY changed)
    Q_PROPERTY(QString lastPc READ lastPc NOTIFY changed)
    // ---- facts ----
    // Native (lane wave4c-usb): a USB data cable to the PC is up, i.e. the headset's USB network
    // interface (usb*, or a gadget device) has carrier. Rescanned every 10 s while the app is open.
    Q_PROPERTY(bool usbCable READ usbCable NOTIFY usbCableChanged)
    Q_PROPERTY(int headsetMaxFps READ headsetMaxFps CONSTANT)
    Q_PROPERTY(QString configIniPath READ configIniPath CONSTANT)

public:
    static MlxrPrefs* get();

    // Writes the launcher settings, and the config.ini keys changed since load()
    Q_INVOKABLE void save();
    void load();
    // load() again and tell the page (a settings profile ended: mlxrprofiles.h)
    void reloadAll();

    // Native (lane wave4-launcher): the config.ini keys a settings profile carries, as they stand in memory now
    // (section, key, the text the VR client reads). A lane that adds a stream setting to the page adds its key
    // here and its property to kPrefFields in mlxrprofiles.cpp, so profiles hold it too.
    struct IniEntry {
        QString section, key, value;
    };
    QVector<IniEntry> profileIniEntries() const;

    // The VR client flags for these settings (only those that differ from the client's defaults)
    QStringList clientArgs() const;
    // Artemis's custom refresh rate as the VR client takes it (capped at headsetMaxFps), or 0
    double refreshRate() const;

    // Native (lane refresh): 144, the Frame's fastest display rate (was 90)
    int headsetMaxFps() const;

    // "Wi-Fi (<SSID>, <band>)" for the headset's managed Wi-Fi interface (not its AP interface), read
    // now from `iw dev` (read-only); plain "Wi-Fi" when unknown. MLXR_LAUNCHER_IW_FILE: a saved
    // `iw dev` output instead (tests).
    Q_INVOKABLE QString wifiLabel() const;
    static QString wifiLabelFromIw(const QString& iwDevOutput);
    QString configIniPath() const;

    bool usbCable() const { return m_usbCable; }

    // Looks at the network interfaces under netRoot (/sys/class/net) now and emits usbCableChanged()
    // when the cable appeared or went away. MLXR_LAUNCHER_NET_ROOT: another directory (tests).
    Q_INVOKABLE void rescanUsb();
    // The name of the first USB network interface under netRoot that has carrier ("1" in its carrier
    // file), "" when none. USB means the name starts with "usb" or uevent says DEVTYPE=gadget.
    static QString usbCableInterface(const QString& netRoot);

    // PLAN 6.7: see the vr property
    QVariantMap vr() const;
    Q_INVOKABLE void setVr(const QString& id, const QVariant& value);
    // One line per key of kVrKeys, "section.key kind default min max names" (kind b, i or e; names joined by |,
    // "-" for none), the format of `mlxr-settings-dump --table`, which prints the VR client's own
    // (MLXR_LAUNCHER_SELFTEST=vrkeys)
    static QStringList vrKeyTable();
    // The value text the VR client would read from raw for that key: a bool as true or false, an int held to its
    // range, an enum's name (by name or index); anything it would not take is the key's default. "" for an unknown id.
    static QString vrNormalize(const QString& id, const QString& raw);

    // PLAN 6.4: the PCs and apps the launcher knows (from its saved PC list), [{pc, app}], for the On start row
    Q_INVOKABLE QVariantList onStartChoices() const;
    QString lastPc() const;

    // config.ini access (exposed for tests): the active value of section.key, else the commented
    // default line's, else def
    static QString iniValue(const QString& path, const QString& section, const QString& key, const QString& def);
    // Sets section.key (replacing the active or the commented line; adding it to the section, or the
    // section, when absent); returns false when the file cannot be written
    static bool setIniValue(const QString& path, const QString& section, const QString& key, const QString& value);

    // Artemis's 3D strength 1..100 <-> the VR client's separation 0..15 (tenths of a percent)
    static int strengthFromSeparation(int sep);
    static int separationFromStrength(int strength);

    QString framePacing;
    bool ultraLowLatency;
    bool virtualDisplay;
    int scaleFactor;
    QString customRefreshRate;
    bool matchHeadsetFps;
    bool fullRange;
    bool rumble;
    int deadzone;
    bool mouseNavButtons;
    QString networkPath;
    bool clipboardSync;
    bool quickResume;
    bool preflight = true;
    bool perfOverlay;
    int renderMode;
    int strength;
    bool strengthIsDefault;
    int convergence;
    bool convergenceIsDefault;
    int eyeBalance = 0;
    int backgroundDim = 0;
    QString voidColor = QStringLiteral("#000000");
    QString glowFeather = QStringLiteral("off");
    QString stereoSource;
    bool sbsFull = false;
    int edgeSharpness = 0;
    int motionReset = 0;
    int depthScale = 100;
    int depthGlobal = 100;
    bool flipFaceButtons;
    QString keyboardKeymap;
    QString notices;
    int scrollSpeed;
    int zoom = 100;
    QString controllerModel;
    int doffDisconnect = 60;
    int idleDisconnect = 0;
    int bitrateWifi = 0, bitrateAdapter = 0, bitrateWired = 0, bitrateUsb = 0;
    int rfiIdrMs = 500;
    bool pathFailover = false;      // Native (lane wave1-failover)
    int failoverBurstMs = 3000;
    QString padMouseChord;
    QString padMouseScroll;
    QString padArbitration;
    QString headsetLanguage;

    // A [ui] language value as the VR client spells it: "auto" for an empty or malformed one,
    // else lower case with the region upper case ("pt_br" -> "pt-BR")
    static QString normalizeLanguage(const QString& v);
    QString padCard;
    bool firstRunShown = false;
    QString lightgun;
    QString padGyro;
    bool checkUpdates = true;
    QString onStart = QStringLiteral("none");
    QString onStartPc;
    QString onStartApp;
    // Native (lane wave4-headinput)
    bool headLook = false;
    int headLookPxDeg = 8;
    bool opentrack = false;
    QString opentrackHost;
    int opentrackPort = 4242;
    QString pointerSource = QStringLiteral("controller");
    QString mouseMode = QStringLiteral("absolute"); // native (lane headinput-finish)
    bool backAsGuide = false;
    int headDwellMs = 0;   // native (lane a11y-tab)
    int assistDotSize = 100;   // native (lane a11y-2): [vr] assist_dot_size, percent, 50..300
    bool assistHeadsetButton = false;                // [vr] assist_headset_button

signals:
    void changed();
    void usbCableChanged();

private:
    explicit MlxrPrefs(QObject* parent = nullptr);
    // what config.ini held at load(), to write only what this page changed
    bool m_iniPerfOverlay = false, m_iniFlip = false;
    QString m_iniNotices = "all";
    int m_iniScrollSpeed = 100;
    int m_iniZoom = 100;
    QString m_iniControllerModel = "off";
    int m_iniDoff = 60, m_iniIdle = 0;
    int m_iniBitrateWifi = 0, m_iniBitrateAdapter = 0, m_iniBitrateWired = 0, m_iniBitrateUsb = 0;
    int m_iniRfiIdr = 500;
    bool m_iniFailover = false;     // Native (lane wave1-failover)
    int m_iniFailoverBurst = 3000;
    QString m_iniPadMouseChord = "start_hold", m_iniPadMouseScroll = "right", m_iniPadArbitration = "pointer";
    QString m_iniPadCard = "auto";
    bool m_iniFirstRunShown = false;
    QString m_iniLightgun = "auto", m_iniPadGyro = "off";
    // Native (lane wave4-headinput): what config.ini held for the head and pointer input keys, and the
    // load and save of those keys (called from load() and save())
    bool m_iniHeadLook = false;
    int m_iniHeadLookPxDeg = 8;
    bool m_iniOpentrack = false;
    QString m_iniOpentrackHost;
    int m_iniOpentrackPort = 4242;
    QString m_iniPointerSource = QStringLiteral("controller");
    QString m_iniMouseMode = QStringLiteral("absolute");
    bool m_iniBackAsGuide = false;
    int m_iniHeadDwellMs = 0;   // native (lane a11y-tab)
    int m_iniAssistDotSize = 100;   // native (lane a11y-2)
    bool m_iniAssistHeadsetButton = false;
    void loadHeadInput(const QString& ini);
    void saveHeadInput(const QString& ini);
    int m_iniRenderMode = 1, m_iniStrength = 0, m_iniConvergence = 0;
    int m_iniEyeBalance = 0, m_iniBackgroundDim = 0;
    QString m_iniVoidColor = QStringLiteral("#000000"), m_iniGlowFeather = QStringLiteral("off");
    bool m_iniStrengthDefault = true, m_iniConvergenceDefault = true;
    QString m_iniStereoSource = QStringLiteral("depth");
    bool m_iniSbsFull = false;
    int m_iniEdgeSharpness = 0, m_iniMotionReset = 0, m_iniDepthScale = 100, m_iniDepthGlobal = 100;
    bool m_usbCable = false, m_usbScanned = false;
    // PLAN 6.7: kVrKeys' values now and as config.ini held them at load(), as the VR client's text
    QMap<QString, QString> m_vr, m_iniVr;
    QString m_iniKeymap = QStringLiteral("qwerty");
    QString m_iniHeadsetLanguage = QStringLiteral("auto");
};
