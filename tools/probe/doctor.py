#!/usr/bin/env python3
"""Update doctor: what did a SteamOS or SteamVR update change that the client depends on?

    doctor.py [--hello PATH | --current FILE] [--baseline FILE] [--save] [--no-cef] [--cef-script PATH]

* Runs `mlxr-hello --probe` (PATH, default mlxr-hello on PATH or next to this script's build) or reads an
  already captured probe (--current FILE), and compares it with the baseline: ~/.config/moonlight-xr/
  probe-baseline.txt, else the repo's docs/probe/openxr-probe.txt. Extensions, system limits, controller
  profile paths, reference spaces and swapchain formats that appeared or went are listed by section.
  The display refresh rates are left out: SteamVR changes them from run to run.
* --save writes the current probe as the baseline (do this after you have looked at a difference and
  accepted it, or on a known good install).
* Also runs `steam_cef.py --probe` (the Steam debugging port the flat screen's return after a stream
  and the installer use) and prints its hint. --no-cef leaves that out.

Quit the Moonlight XR client first: the probe opens its own OpenXR session. Run it on the Frame.
Exit code: 0 nothing changed, 1 something changed (or the Steam port is not fine), 2 the probe could
not run.

Moonlight XR Native. GPLv3.
"""
import difflib, json, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
# Lines that change from run to run (SteamVR offers one rate and moves it itself)
VOLATILE = ("# display refresh rates", "# current refresh rate")


def normalise(text):
    """The probe's lines without the volatile ones and without trailing blanks."""
    return [l.rstrip() for l in text.splitlines() if l.strip() and not l.startswith(VOLATILE)]


def sectioned(lines):
    """[(section, line)] where the section is the last '#' header or 'profile ...' line above it."""
    out, section = [], "(top)"
    for l in lines:
        if l.startswith("#") or l.startswith("profile "):
            section = l
            out.append((section, l))
        else:
            out.append((section, l.strip()))
    return out


def compare(baseline_text, current_text):
    """([(section, line) added], [(section, line) removed]) from the baseline to the current probe."""
    a, b = sectioned(normalise(baseline_text)), sectioned(normalise(current_text))
    added, removed = [], []
    for op, i0, i1, j0, j1 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if op in ("delete", "replace"):
            removed += a[i0:i1]
        if op in ("insert", "replace"):
            added += b[j0:j1]
    # A header line that only moved counts (its count changes with its list); the list lines are the news
    return added, removed


def report(added, removed):
    lines = []
    sections = []
    for s, _ in added + removed:
        if s not in sections:
            sections.append(s)
    for s in sections:
        lines.append(s)
        for sec, l in removed:
            if sec == s and l != s:
                lines.append(f"  - {l}")
        for sec, l in added:
            if sec == s and l != s:
                lines.append(f"  + {l}")
    return "\n".join(lines)


def default_hello():
    for cand in (shutil.which("mlxr-hello"), os.path.join(HERE, "mlxr-hello"),
                 os.path.join(ROOT, "build", "hello", "mlxr-hello")):
        if cand and os.path.isfile(cand):
            return cand
    return None


def run_hello(path):
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "probe.txt")
        p = subprocess.run([path, "--probe", "--probe-out", out], capture_output=True, text=True, timeout=120)
        if p.returncode != 0 or not os.path.exists(out):
            raise RuntimeError(f"{path} --probe failed (exit {p.returncode}): {(p.stderr or p.stdout).strip()[-400:]}")
        with open(out, encoding="utf-8", errors="replace") as f:
            return f.read()


def baseline_path(explicit):
    if explicit:
        return explicit
    own = os.path.expanduser("~/.config/moonlight-xr/probe-baseline.txt")
    if os.path.exists(own):
        return own
    return os.path.join(ROOT, "docs", "probe", "openxr-probe.txt")


def cef_check(script):
    """steam_cef.py --probe's JSON as a dict, or {"hint": why not}."""
    if not script or not os.path.isfile(script):
        return {"ok": None, "hint": "steam_cef.py not found: pass --cef-script"}
    p = subprocess.run([sys.executable, "-I", script, "--probe"], capture_output=True, text=True, timeout=60)
    try:
        return json.loads(p.stdout)
    except ValueError:
        return {"ok": False, "hint": (p.stderr or p.stdout or "no answer").strip()[-300:]}


def main(argv):
    args = {"--hello": None, "--current": None, "--baseline": None, "--cef-script": None}
    save = no_cef = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in args and i + 1 < len(argv):
            args[a] = argv[i + 1]
            i += 2
        elif a == "--save":
            save = True
            i += 1
        elif a == "--no-cef":
            no_cef = True
            i += 1
        else:
            sys.stderr.write(__doc__)
            return 2
    try:
        if args["--current"]:
            with open(args["--current"], encoding="utf-8", errors="replace") as f:
                current = f.read()
        else:
            hello = args["--hello"] or default_hello()
            if not hello:
                sys.stderr.write("mlxr-hello not found: pass --hello PATH or --current FILE\n")
                return 2
            current = run_hello(hello)
    except (OSError, RuntimeError, subprocess.SubprocessError) as e:
        sys.stderr.write(f"probe: {e}\n")
        return 2

    changed = False
    base_file = baseline_path(args["--baseline"])
    if save:
        target = args["--baseline"] or os.path.expanduser("~/.config/moonlight-xr/probe-baseline.txt")
        os.makedirs(os.path.dirname(os.path.abspath(target)), exist_ok=True)
        with open(target, "w", encoding="utf-8") as f:
            f.write(current)
        print(f"baseline saved: {target}")
    elif os.path.exists(base_file):
        with open(base_file, encoding="utf-8", errors="replace") as f:
            added, removed = compare(f.read(), current)
        if added or removed:
            changed = True
            print(f"OpenXR probe differs from {base_file}: {len(added)} lines new, {len(removed)} gone")
            print(report(added, removed))
        else:
            print(f"OpenXR probe matches {base_file}")
    else:
        print(f"no baseline at {base_file}: run with --save to keep this probe as one")

    if not no_cef:
        cef = cef_check(args["--cef-script"] or os.path.join(ROOT, "packaging", "steam_cef.py"))
        print("Steam CEF port: " + str(cef.get("hint", "")))
        if cef.get("ok") is False:
            changed = True
    return 1 if changed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
