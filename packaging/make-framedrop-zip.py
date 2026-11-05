#!/usr/bin/env python3
"""Builds the FrameDrop zip and its manifest from the release archive Frametanium installs.

  make-framedrop-zip.py ARCHIVE.tar.gz|ARCHIVE.zip --run-sh run.sh --repo Repo --tag vX.Y.Z \
        --name "Display name" --zip-name NAME.zip --version X.Y.Z [--out DIR]

The zip holds the same files as the archive (a single top-level folder is dropped, as Frametanium does),
plus run.sh at the top and a VERSION file when the archive has none. Symbolic links are not stored (zip
tools on other systems turn them into files); they are listed in .framedrop-symlinks and run.sh makes
them again. Writes DIR/NAME.zip and DIR/<repo-lowercase>.framedrop.json (the url is the release's
download url for that tag) and prints the sha256 of the zip. Nothing is downloaded or uploaded here.
"""
import argparse, hashlib, json, os, shutil, stat, sys, tarfile, tempfile, zipfile

ap = argparse.ArgumentParser()
ap.add_argument("archive"); ap.add_argument("--run-sh", required=True); ap.add_argument("--repo", required=True)
ap.add_argument("--tag", required=True); ap.add_argument("--name", required=True); ap.add_argument("--zip-name", required=True)
ap.add_argument("--version", required=True); ap.add_argument("--out", default=".")
a = ap.parse_args()
out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
work = tempfile.mkdtemp(prefix="framedrop-", dir=out)
try:
    root = os.path.join(work, "t"); os.makedirs(root)
    if a.archive.endswith(".zip"):
        with zipfile.ZipFile(a.archive) as z:
            for i in z.infolist():
                t = os.path.realpath(os.path.join(root, i.filename))
                if not t.startswith(os.path.realpath(root) + os.sep) and t != os.path.realpath(root): sys.exit("unsafe path " + i.filename)
                z.extract(i, root)
                m = (i.external_attr >> 16) & 0o777
                if m and not i.is_dir(): os.chmod(t, m)
    else:
        with tarfile.open(a.archive) as t: t.extractall(root, filter="data")
    ents = os.listdir(root)
    if len(ents) == 1 and os.path.isdir(os.path.join(root, ents[0])) and not os.path.islink(os.path.join(root, ents[0])):
        root = os.path.join(root, ents[0])
    shutil.copy2(a.run_sh, os.path.join(root, "run.sh")); os.chmod(os.path.join(root, "run.sh"), 0o755)
    vf = os.path.join(root, "VERSION")
    if os.path.exists(vf):
        have = open(vf).readline().strip()
        if have != a.version and have.split()[-1:] != [a.version]: sys.exit("the archive's VERSION is %r, not %r" % (have, a.version))
    else:
        open(vf, "w").write(a.version + "\n")
    files, links = [], []
    for d, dn, fn in os.walk(root):
        for n in dn + fn:
            p = os.path.join(d, n); rel = os.path.relpath(p, root)
            if os.path.islink(p):
                tgt = os.readlink(p)
                if os.path.isabs(tgt): sys.exit("absolute symlink: %s -> %s" % (rel, tgt))
                links.append((rel, tgt))
            elif os.path.isfile(p):
                files.append(rel)
    if links:
        lf = os.path.join(root, ".framedrop-symlinks")
        open(lf, "w").write("".join("%s\t%s\n" % l for l in sorted(links))); files.append(".framedrop-symlinks")
    zpath = os.path.join(out, a.zip_name)
    if os.path.exists(zpath): os.remove(zpath)
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for rel in sorted(files, key=lambda s: s.encode()):
            p = os.path.join(root, rel)
            i = zipfile.ZipInfo(rel, (2026, 1, 1, 0, 0, 0)); i.compress_type = zipfile.ZIP_DEFLATED
            i.create_system = 3
            i.external_attr = ((0o755 if os.stat(p).st_mode & stat.S_IXUSR else 0o644) | stat.S_IFREG) << 16
            with open(p, "rb") as src, z.open(i, "w", force_zip64=True) as dst: shutil.copyfileobj(src, dst, 1 << 20)
    h = hashlib.sha256()
    with open(zpath, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    man = {"schema": "framedrop.install/v1", "name": a.name, "files": [{
        "url": "https://github.com/simplysanatori/%s/releases/download/%s/%s" % (a.repo, a.tag, a.zip_name),
        "sha256": h.hexdigest()}]}
    mp = os.path.join(out, a.repo.lower() + ".framedrop.json")
    open(mp, "w").write(json.dumps(man, indent=2) + "\n")
    print(zpath); print(mp); print(h.hexdigest() + "  " + a.zip_name)
finally:
    shutil.rmtree(work, ignore_errors=True)
