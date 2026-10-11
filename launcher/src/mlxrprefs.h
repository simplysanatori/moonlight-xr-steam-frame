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
    Q_PROPERTY(bool fullRange MEMBER fullRange NOTIFY changed)
    Q_PROPERTY(bool rumble MEMBER rumble NOTIFY changed)
    Q_PROPERTY(int deadzone MEMBER deadzone NOTIFY changed)                  // -20..20 percent
    Q_PROPERTY(bool mouseNavButtons MEMBER mouseNavButtons NOTIFY changed)
    // The PC link: "auto" (default), "home" (Wi-Fi), "adapter" (the Steam Frame Wireless
    // adapter). Key "networkPath" (no "mlxr/" group): lane network-path's hand-off reads it and passes
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
    // ---- facts ----
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

    int headsetMaxFps() const { return 90; }

    // "Wi-Fi (<SSID>, <band>)" for the headset's managed Wi-Fi interface (not its AP interface), read
    // now from `iw dev` (read-only); plain "Wi-Fi" when unknown. MLXR_LAUNCHER_IW_FILE: a saved
    // `iw dev` output instead (tests).
    Q_INVOKABLE QString wifiLabel() const;
    static QString wifiLabelFromIw(const QString& iwDevOutput);
    QString configIniPath() const;

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

signals:
    void changed();

private:
    explicit MlxrPrefs(QObject* parent = nullptr);
    // what config.ini held at load(), to write only what this page changed
    bool m_iniPerfOverlay = false, m_iniFlip = false;
    int m_iniRenderMode = 1, m_iniStrength = 0, m_iniConvergence = 0;
    bool m_iniStrengthDefault = true, m_iniConvergenceDefault = true;
};
