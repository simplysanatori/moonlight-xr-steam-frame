#!/usr/bin/env python3
"""Moonlight XR for Frame: what makes the app's icon show in SteamVR's dock.

    dock_icon.py install DEST [--appid N] [--art DIR]
    dock_icon.py remove  DEST [--appid N]

Two things draw the icon, and both were missing:

* The VR client (moonlight-xr) runs as SteamVR app "steam.app.<appid>". SteamVR
  serves an app's picture from the image_path of an app manifest, and Steam's
  own steamapps.vrmanifest entry for a shortcut is skipped on the Frame (no
  binary_path_linux_arm), so the app had only a generated entry and no image. This
  writes DEST/moonlight-xr.vrmanifest (is_internal: no extra Non-Steam Games entry,
  not in SteamVR's launch list) and registers it with SteamVR.
* The flat launcher's window class is moonlight-xr-launcher (the binary name, also
  its Qt desktop file name); the dock looks that name up in the icon theme and
  cannot render SVG, so PNGs go to ~/.local/share/icons/hicolor/NxN/apps and a
  NoDisplay .desktop file ties the name to the Steam shortcut.

Everything is under $HOME, nothing needs root, and running it again changes nothing.
Environment: MLXR_VRCMD (default /opt/steamvr/bin/linuxarm64/vrcmd), HOME,
MLXR_VRSERVER_RUNNING (0/1, for the tests),
XDG_DATA_HOME. Moonlight XR for Frame. GPLv3.
"""
import json, os, re, shutil, struct, subprocess, sys

ID = "moonlight-xr-launcher"          # window class / Qt desktop file name / icon name
NAME = "Moonlight XR for Frame"
SIZES = (64, 128, 256, 512)
MANIFEST = "moonlight-xr.vrmanifest"


def data_home():
    return os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")


def shortcut_appid(dest):
    """The appid of the Steam shortcut that runs DEST's launcher (or VR wrapper)."""
    want = (dest + "/moonlight-xr-launcher.sh", dest + "/moonlight-xr.sh")
    home = os.path.expanduser("~")
    for root in (os.path.join(home, ".local/share/Steam"), os.path.join(home, ".steam/steam")):
        ud = os.path.join(root, "userdata")
        if not os.path.isdir(ud):
            continue
        for u in sorted(os.listdir(ud)):
            vdf = os.path.join(ud, u, "config", "shortcuts.vdf")
            if not os.path.isfile(vdf):
                continue
            d = open(vdf, "rb").read()
            for m in re.finditer(rb"\x02appid\x00(....)\x01AppName\x00[^\x00]*\x00\x01Exe\x00([^\x00]*)\x00", d, re.S):
                if m.group(2).decode(errors="replace").strip('"') in want:
                    return struct.unpack("<I", m.group(1))[0]
    return None


def desktop_text(appid):
    gameid = (appid << 32) | 0x02000000
    return ("[Desktop Entry]\n"
            f"Name={NAME}\n"
            "Comment=Moonlight game streaming in the headset, through its Steam shortcut\n"
            f"Exec=steam steam://rungameid/{gameid}\n"
            f"Icon={ID}\n"
            f"StartupWMClass={ID}\n"
            "Terminal=false\n"
            "Type=Application\n"
            "Categories=Game;\n"
            "NoDisplay=true\n")


def manifest_obj(dest, appid, art):
    launcher = dest + "/moonlight-xr-launcher.sh"
    if not os.path.exists(launcher):
        launcher = dest + "/moonlight-xr.sh"
    app = {
        "app_key": f"steam.app.{appid}",
        "launch_type": "binary",
        "binary_path_linux": launcher,
        "binary_path_linux_arm": launcher,
        "working_directory": dest,
        "image_path": os.path.join(art, "icon-512.png"),
        "is_internal": True,
        "strings": {"en_us": {"name": NAME}},
    }
    portrait = os.path.join(art, "portrait-600x900.png")
    if os.path.exists(portrait):
        app["image_path_capsule"] = portrait
    return {"source": "builtin", "applications": [app]}


def write_if_changed(path, data):
    if isinstance(data, str):
        data = data.encode()
    try:
        if open(path, "rb").read() == data:
            return False
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    return True


def vrserver_running():
    if "MLXR_VRSERVER_RUNNING" in os.environ:      # for the tests
        return os.environ["MLXR_VRSERVER_RUNNING"] == "1"
    return subprocess.run(["pgrep", "-x", "vrserver"], capture_output=True).returncode == 0


def appconfig_path():
    return os.path.join(os.path.expanduser("~"), ".config/openvr/config/appconfig.json")


def appconfig_edit(path, add=None, drop=None):
    """Add or drop one manifest path in SteamVR's appconfig.json (only when SteamVR is not running)."""
    cfg_path = appconfig_path()
    try:
        cfg = json.load(open(cfg_path))
    except (OSError, ValueError):
        cfg = {}
    paths = [p for p in cfg.get("manifest_paths", []) if p != drop]
    if add and add not in paths:
        paths.append(add)
    if paths == cfg.get("manifest_paths", []):
        return False
    cfg["manifest_paths"] = paths
    os.makedirs(os.path.dirname(cfg_path), exist_ok=True)
    json.dump(cfg, open(cfg_path, "w"), indent=3)
    return True


def install(dest, appid, art):
    out = []
    if appid is None:
        appid = shortcut_appid(dest)
    if appid is None:
        print("dock icon: no Steam shortcut for this install yet; run install.sh again once the shortcut exists")
        return 1
    # flat launcher window: icon theme + desktop file
    n = 0
    for s in SIZES:
        src = os.path.join(art, f"icon-{s}.png")
        if not os.path.exists(src):
            continue
        dst = os.path.join(data_home(), "icons/hicolor", f"{s}x{s}", "apps", ID + ".png")
        data = open(src, "rb").read()
        n += write_if_changed(dst, data)
    desk = os.path.join(data_home(), "applications", ID + ".desktop")
    n += write_if_changed(desk, desktop_text(appid))
    out.append(f"{n} icon/desktop file(s) updated ({ID}.png in hicolor, {ID}.desktop)")
    # VR client: SteamVR app manifest
    man = os.path.join(dest, MANIFEST)
    changed = write_if_changed(man, json.dumps(manifest_obj(dest, appid, art), indent=2) + "\n")
    vrcmd = os.environ.get("MLXR_VRCMD", "/opt/steamvr/bin/linuxarm64/vrcmd")
    if os.path.exists(vrcmd) and vrserver_running():
        env = {k: v for k, v in os.environ.items() if k != "LD_LIBRARY_PATH"}
        r = subprocess.run([vrcmd, "--appmanifest", man], env=env, capture_output=True, timeout=30)
        out.append("SteamVR app manifest registered with the running SteamVR" if r.returncode == 0
                   else "SteamVR app manifest written; vrcmd could not register it (it loads at the next SteamVR start)")
        if r.returncode != 0:
            appconfig_edit(man, add=man)
    else:
        out.append("SteamVR app manifest " + ("written" if changed else "unchanged") +
                   ("; added to SteamVR's appconfig.json" if appconfig_edit(man, add=man) else "") +
                   " (SteamVR is not running: it is read at the next SteamVR start)")
    print(f"dock icon (steam.app.{appid}): " + "; ".join(out))
    return 0


def remove(dest):
    for s in SIZES:
        p = os.path.join(data_home(), "icons/hicolor", f"{s}x{s}", "apps", ID + ".png")
        if os.path.exists(p):
            os.remove(p)
    p = os.path.join(data_home(), "applications", ID + ".desktop")
    if os.path.exists(p):
        os.remove(p)
    man = os.path.join(dest, MANIFEST)
    if not vrserver_running():
        appconfig_edit(None, drop=man)
    if os.path.exists(man):
        os.remove(man)
    print("dock icon: files removed (SteamVR forgets the manifest at its next start)")
    return 0


def main(argv):
    if len(argv) < 3 or argv[1] not in ("install", "remove"):
        print(__doc__)
        return 2
    cmd, dest = argv[1], argv[2].rstrip("/")
    appid, art = None, os.path.join(dest, "assets", "branding")
    i = 3
    while i < len(argv):
        if argv[i] == "--appid":
            appid = int(argv[i + 1]); i += 2
        elif argv[i] == "--art":
            art = argv[i + 1]; i += 2
        else:
            print("unknown option", argv[i]); return 2
    return install(dest, appid, art) if cmd == "install" else remove(dest)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
