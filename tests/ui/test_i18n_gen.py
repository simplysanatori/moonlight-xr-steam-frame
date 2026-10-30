#!/usr/bin/env python3
"""tools/ui/gen_strings.py: the fixture gives the committed expected table, the parsing and format
rules hold, and the committed ui/ui_strings_i18n.inc matches the script's MAP and ui/ui_strings.h
(a key added to ui_strings.h without a MAP line fails here). Standard library only. GPLv3."""
import importlib.util
import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
spec = importlib.util.spec_from_file_location("gen_strings", os.path.join(ROOT, "tools", "ui", "gen_strings.py"))
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

fails = 0
checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL: " + what)


def text_of(xml):
    return gen.android_text(ET.fromstring("<string>%s</string>" % xml))


# Android's quoting and escapes
check(text_of("  Abstand \n  ") == "Abstand", "whitespace collapses and trims")
check(text_of("a  b\n c") == "a b c", "inner whitespace collapses")
check(text_of('"a  b"') == "a  b", "quoted keeps inner spaces")
check(text_of("Don\\'t") == "Don't", "escaped apostrophe")
check(text_of('say \\"hi\\"') == 'say "hi"', "escaped quote")
check(text_of("one\\ntwo") == "one\ntwo", "newline escape")
check(text_of("\\u00e9t\\u00e9") == "été", "unicode escape")
check(text_of("a &amp; b") == "a & b", "entity")
check(text_of('x <b>bold</b> y') == "x bold y", "markup dropped")

# formats: the English's conversions, plain, in order
check(gen.c_format("Akku %1$d %%", "Battery %d%%") == ("Akku %d %%", None), "numbered to plain")
check(gen.c_format("V %1$s, c %2$s", "Version %s, commit %s") == ("V %s, c %s", None), "two in order")
check(gen.c_format("c %2$s v %1$s", "Version %s, commit %s")[0] is None, "reordered values refused")
check(gen.c_format("B %1$s", "Battery %d%%")[0] is None, "a different type refused")
check(gen.c_format("B %d", "Battery %d%%") == ("B %d", None), "%% not needed")
check(gen.c_format("100%", "space")[0] is None, "a % where the English has none refused")
check(gen.c_format("%1$d / %d", "%d / %d")[0] is None, "mixed numbering refused")
check(gen.c_string('a"b\\c\n??=') == '"a\\"b\\\\c\\n?\\?="', "C escapes, no trigraph")

# the fixture gives the committed expected table
fx = os.path.join(HERE, "i18n_fixture")
with tempfile.TemporaryDirectory() as tmp:
    out = os.path.join(tmp, "out.inc")
    rc = gen.main(["--fixture", "--res", os.path.join(fx, "res"), "--header", os.path.join(fx, "ui_strings.h"),
                   "--out", out, "--upstream-rev", "fixture"])
    check(rc == 0, "fixture run")
    got = open(out, encoding="utf-8").read()
    want = open(os.path.join(fx, "expected.inc"), encoding="utf-8").read()
    check(got == want, "fixture output equals tests/ui/i18n_fixture/expected.inc")
    if got != want:
        import difflib
        sys.stdout.writelines(difflib.unified_diff(want.splitlines(True), got.splitlines(True), "expected", "got"))
    # a key in the header without a MAP line is caught
    hdr = open(os.path.join(fx, "ui_strings.h"), encoding="utf-8").read()
    hdr = hdr.replace('X(KEY_SPACE, "vr_key_space", "space")',
                      'X(KEY_SPACE, "vr_key_space", "space") \\\n    X(NEW_ONE, "native_brand_new", "New")')
    bad = os.path.join(tmp, "ui_strings.h")
    with open(bad, "w", encoding="utf-8") as f:
        f.write(hdr)
    rc = gen.main(["--fixture", "--res", os.path.join(fx, "res"), "--header", bad, "--out", out])
    check(rc == 1, "a header key without a MAP line stops the generator")
    errs = gen.check_map(gen.read_header(bad))
    check(any("native_brand_new" in e for e in errs), "check_map names the new key")

# the real files: MAP covers ui_strings.h, the committed table was made from MAP
header = gen.read_header(gen.HEADER_PATH)
check(len(header) > 150, "ui_strings.h read (%d keys)" % len(header))
errs = gen.verify(gen.INC_PATH, gen.HEADER_PATH)
for e in errs:
    print("  " + e)
check(not errs, "committed ui/ui_strings_i18n.inc matches MAP and ui/ui_strings.h")
check(len(gen.LANGS) == 19, "upstream's 19 complete languages")
# a translated key whose English changes (it may mean something else now) is caught
with tempfile.TemporaryDirectory() as tmp:
    real = open(gen.HEADER_PATH, encoding="utf-8").read()
    changed = os.path.join(tmp, "ui_strings.h")
    with open(changed, "w", encoding="utf-8") as f:
        f.write(real.replace('"vr_panel_distance", "Distance"', '"vr_panel_distance", "Distance to the wall"'))
    errs = gen.verify(gen.INC_PATH, changed)
    check(any("English of vr_panel_distance changed" in e for e in errs), "a changed English is caught")
    # a native string's English may change freely
    with open(changed, "w", encoding="utf-8") as f:
        f.write(real.replace('"native_forget", "Forget"', '"native_forget", "Forget it"'))
    check(gen.verify(gen.INC_PATH, changed) == [], "a native string's English changes freely")
check(all(k not in [h for _, h, _ in header] or dict(gen.MAP)[k] is gen.NATIVE for k in gen.PENDING),
      "keys of lanes not merged yet are native")
shared = [k for k, s in gen.MAP if s == gen.SAME]
# 111 until lane fix-polish took out three (harder on the eyes, the Ko-fi sheet's scan and free lines)
check(len(shared) + len([k for k, s in gen.MAP if s == gen.DRIFT]) == 108,
      "108 keys shared with upstream (SAME + DRIFT)")

# the launcher's Headset language list is the same languages, in the same order
qml = open(os.path.join(ROOT, "launcher", "qml", "XrSettingsView.qml"), encoding="utf-8").read()
m = re.search(r"readonly property var headsetLanguageValues: \[([^\]]*)\]", qml)
n = re.search(r"readonly property var headsetLanguageNames: \[(.*)\]\n", qml)
check(m is not None and n is not None, "XrSettingsView.qml has headsetLanguageValues and headsetLanguageNames")
if m and n:
    codes = re.findall(r'"([^"]+)"', m.group(1))
    want = ["auto", "en"] + [c for c, _, _, _ in gen.LANGS]
    check(codes == want, "launcher codes %s == %s" % (codes, want))
    names = re.findall(r'"([^"]+)"', n.group(1))
    check(names[1:] == ["English"] + [nm for _, _, nm, _ in gen.LANGS], "launcher names %s" % names)

print("test_i18n_gen: %d checks, %d failed" % (checks, fails))
sys.exit(1 if fails else 0)
