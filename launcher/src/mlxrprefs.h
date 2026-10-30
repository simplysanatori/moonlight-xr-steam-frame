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

#include <QObject>
#include <QString>
#include <QStringList>

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
    // ---- the VR client's config.ini ----
    Q_PROPERTY(bool perfOverlay MEMBER perfOverlay NOTIFY changed)           // [display] stats
    Q_PROPERTY(int renderMode MEMBER renderMode NOTIFY changed)              // [3d] stereo: 0 2D, 1 AI 3D
    Q_PROPERTY(int strength MEMBER strength NOTIFY changed)                  // [3d] separation, as 1..100
    Q_PROPERTY(bool strengthIsDefault MEMBER strengthIsDefault NOTIFY changed)
    Q_PROPERTY(int convergence MEMBER convergence NOTIFY changed)            // [3d] convergence, 0..100
    Q_PROPERTY(bool convergenceIsDefault MEMBER convergenceIsDefault NOTIFY changed)
    Q_PROPERTY(bool flipFaceButtons MEMBER flipFaceButtons NOTIFY changed)   // [vr] flip_face_buttons
    // [vr] keyboard_keymap: what the headset keyboard's keys say, "qwerty" (default), "azerty" or "qwertz"
    Q_PROPERTY(QString keyboardKeymap MEMBER keyboardKeymap NOTIFY changed)
    // Which notices the headset's toast says: "all", "important" or "off" ([vr] notices)
    Q_PROPERTY(QString notices MEMBER notices NOTIFY changed)
    // How fast a controller thumbstick scrolls in the headset, percent of 7.5 notches a
    // second, 25..400 ([vr] scroll_speed)
    Q_PROPERTY(int scrollSpeed MEMBER scrollSpeed NOTIFY changed)
    // Which controller the headset draws in each hand: "off", "frame", "generic", "gun" or "custom"
    // ([vr] controller_model). Custom (your own files in the models folder) is only chosen here and
    // in the file; the headset's Display tab has a cell for the other four
    Q_PROPERTY(QString controllerModel MEMBER controllerModel NOTIFY changed)
    // Native (lane wave3-session): [stream] doff_disconnect (seconds with the headset off before the stream is
    // disconnected, the PC's app keeps running; 0 never, default 60) and idle_disconnect (minutes without input
    // before the same; 0 never, default 0)
    Q_PROPERTY(int doffDisconnect MEMBER doffDisconnect NOTIFY changed)
    Q_PROPERTY(int idleDisconnect MEMBER idleDisconnect NOTIFY changed)
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
    bool perfOverlay;
    int renderMode;
    int strength;
    bool strengthIsDefault;
    int convergence;
    bool convergenceIsDefault;
    bool flipFaceButtons;
    QString keyboardKeymap;
    QString notices;
    int scrollSpeed;
    QString controllerModel;
    int doffDisconnect = 60;
    int idleDisconnect = 0;
    QString padMouseChord;
    QString padMouseScroll;
    QString padArbitration;
    QString headsetLanguage;

    // A [ui] language value as the VR client spells it: "auto" for an empty or malformed one,
    // else lower case with the region upper case ("pt_br" -> "pt-BR")
    static QString normalizeLanguage(const QString& v);

signals:
    void changed();
    void usbCableChanged();

private:
    explicit MlxrPrefs(QObject* parent = nullptr);
    // what config.ini held at load(), to write only what this page changed
    bool m_iniPerfOverlay = false, m_iniFlip = false;
    QString m_iniNotices = "all";
    int m_iniScrollSpeed = 100;
    QString m_iniControllerModel = "off";
    int m_iniDoff = 60, m_iniIdle = 0;
    QString m_iniPadMouseChord = "start_hold", m_iniPadMouseScroll = "right", m_iniPadArbitration = "pointer";
    int m_iniRenderMode = 1, m_iniStrength = 0, m_iniConvergence = 0;
    bool m_iniStrengthDefault = true, m_iniConvergenceDefault = true;
    bool m_usbCable = false, m_usbScanned = false;
    QString m_iniKeymap = QStringLiteral("qwerty");
    QString m_iniHeadsetLanguage = QStringLiteral("auto");
};
