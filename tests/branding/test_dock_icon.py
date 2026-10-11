#!/usr/bin/env python3
"""Moonlight XR for Frame: packaging/dock_icon.py in a temporary HOME.

    test_dock_icon.py <dock_icon.py> <assets/branding>

Checks the icon theme PNGs (every size, real PNGs of that size), the .desktop file
(Icon and StartupWMClass match the launcher's window class, Exec runs the Steam
shortcut, NoDisplay), the SteamVR app manifest (key steam.app.<appid>, an arm binary
path, an image that exists, internal), that a second run changes nothing, that the
launcher patch still sets the same desktop file name, and that remove undoes it.
"""
import json, os, struct, subprocess, sys, tempfile, shutil

script, art = sys.argv[1], sys.argv[2]
fails = []


def check(cond, msg):
    print(("ok: " if cond else "FAIL: ") + msg)
    if not cond:
        fails.append(msg)


def png_size(path):
    d = open(path, "rb").read(24)
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", d[16:24])


def vdf_entry(appid, name, exe):
    return (b"\x00" + b"0\x00" + b"\x02appid\x00" + struct.pack("<I", appid) +
            b"\x01AppName\x00" + name.encode() + b"\x00\x01Exe\x00" + exe.encode() + b"\x00\x08")


tmp = tempfile.mkdtemp()
try:
    home = os.path.join(tmp, "home"); dest = os.path.join(tmp, "app")
    appid = 2761093165
    os.makedirs(os.path.join(dest, "assets"))
    shutil.copytree(art, os.path.join(dest, "assets", "branding"))
    open(os.path.join(dest, "moonlight-xr-launcher.sh"), "w").write("#!/bin/sh\n")
    cfg = os.path.join(home, ".local/share/Steam/userdata/1/config")
    os.makedirs(cfg)
    open(os.path.join(cfg, "shortcuts.vdf"), "wb").write(
        b"\x00shortcuts\x00" + vdf_entry(111, "Other", '"/bin/true"') +
        vdf_entry(appid, "Moonlight XR for Frame", '"%s/moonlight-xr-launcher.sh"' % dest) + b"\x08\x08")
    env = {"PATH": os.environ["PATH"], "HOME": home, "MLXR_VRSERVER_RUNNING": "0",
           "MLXR_VRCMD": os.path.join(tmp, "no-vrcmd")}

    def run(*args, e=env):
        r = subprocess.run([sys.executable, "-I", script, *args], env=e, capture_output=True, text=True)
        print(r.stdout.strip(), r.stderr.strip())
        return r

    r = run("install", dest)
    check(r.returncode == 0, "install succeeds, the appid found from shortcuts.vdf")
    check(("steam.app.%d" % appid) in r.stdout, "the appid is the shortcut's")
    for s in (64, 128, 256, 512):
        p = os.path.join(home, ".local/share/icons/hicolor/%dx%d/apps/moonlight-xr-launcher.png" % (s, s))
        check(os.path.exists(p) and png_size(p) == (s, s), "hicolor %dx%d PNG" % (s, s))
    d = open(os.path.join(home, ".local/share/applications/moonlight-xr-launcher.desktop")).read()
    check("Icon=moonlight-xr-launcher\n" in d and "StartupWMClass=moonlight-xr-launcher\n" in d, "desktop file: Icon and StartupWMClass")
    check("Exec=steam steam://rungameid/%d\n" % ((appid << 32) | 0x02000000) in d, "desktop file: runs the Steam shortcut")
    check("NoDisplay=true" in d, "desktop file: not in menus")
    m = json.load(open(os.path.join(dest, "moonlight-xr.vrmanifest")))
    a = m["applications"][0]
    check(a["app_key"] == "steam.app.%d" % appid, "manifest app_key")
    check(a["is_internal"] is True and a["launch_type"] == "binary", "manifest internal binary app")
    check(a["binary_path_linux_arm"].endswith("moonlight-xr-launcher.sh"), "manifest has the arm binary path SteamVR needs")
    check(os.path.exists(a["image_path"]) and png_size(a["image_path"]) == (512, 512), "manifest image is the 512 PNG")
    cfgp = json.load(open(os.path.join(home, ".config/openvr/config/appconfig.json")))
    check(os.path.join(dest, "moonlight-xr.vrmanifest") in cfgp["manifest_paths"], "registered in appconfig.json")

    before = {}
    for root, _, files in os.walk(tmp):
        for f in files:
            p = os.path.join(root, f); before[p] = (open(p, "rb").read(), os.stat(p).st_mtime_ns)
    run("install", dest)
    after = {}
    for root, _, files in os.walk(tmp):
        for f in files:
            p = os.path.join(root, f); after[p] = (open(p, "rb").read(), os.stat(p).st_mtime_ns)
    check(before == after, "a second install changes nothing")
    cfgp2 = json.load(open(os.path.join(home, ".config/openvr/config/appconfig.json")))
    check(cfgp2["manifest_paths"].count(os.path.join(dest, "moonlight-xr.vrmanifest")) == 1, "registered once")

    # with a running SteamVR the manifest goes through vrcmd
    fake = os.path.join(tmp, "vrcmd"); log = os.path.join(tmp, "vrcmd.log")
    open(fake, "w").write('#!/bin/sh\necho "$@" >> %s\n' % log); os.chmod(fake, 0o755)
    e2 = dict(env, MLXR_VRSERVER_RUNNING="1", MLXR_VRCMD=fake)
    run("install", dest, e=e2)
    check(os.path.exists(log) and "--appmanifest" in open(log).read(), "a running SteamVR gets the manifest from vrcmd")

    # no shortcut for another install: nothing written, non-zero
    r = run("install", os.path.join(tmp, "elsewhere"))
    check(r.returncode != 0, "no shortcut, no install")

    # the launcher's window class matches the icon name
    here = os.path.dirname(os.path.abspath(script))
    patches = os.path.join(here, "..", "launcher", "patches")
    txt = "".join(open(os.path.join(patches, f)).read() for f in sorted(os.listdir(patches)))
    check('setDesktopFileName("moonlight-xr-launcher")' in txt and 'SDL_VIDEO_X11_WMCLASS", "moonlight-xr-launcher"' in txt,
          "launcher patches set the desktop file name and window class to moonlight-xr-launcher")

    run("remove", dest)
    check(not os.path.exists(os.path.join(home, ".local/share/applications/moonlight-xr-launcher.desktop")), "remove: desktop file gone")
    check(not os.path.exists(os.path.join(dest, "moonlight-xr.vrmanifest")), "remove: manifest gone")
    cfgp3 = json.load(open(os.path.join(home, ".config/openvr/config/appconfig.json")))
    check(os.path.join(dest, "moonlight-xr.vrmanifest") not in cfgp3["manifest_paths"], "remove: unregistered")
finally:
    shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
