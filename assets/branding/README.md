# Branding art

Artwork for the app's icon and its Steam library entry. Every image is made from one source file by `tools/branding/make-art.py`; run `python3 -I tools/branding/make-art.py` to regenerate them.

| File | Used for | Made from |
| --- | --- | --- |
| `source/moonlight-xr-logo.png` | Source of everything below | `moonlight-xr-logo-transparent.png` from Moonlight XR (Sean Gilleece, https://github.com/Gilleece/moonlight-android-xr), unchanged, GPLv3 |
| `icon-512.png` | Steam shortcut icon (`<appid>_icon.png`) | The logo's disc, cropped, transparent corners |
| `icon-256.png` | Window icon of the flat launcher (compiled into it by `launcher/build.sh`) | Same |
| `portrait-600x900.png` | Steam portrait capsule (`<appid>p.png`) | Logo disc and the name on a dark blue gradient |
| `wide-920x430.png` | Steam wide capsule (`<appid>.png`) | Same layout, side by side |
| `hero-3840x1240.png` | Steam hero (`<appid>_hero.png`) | The gradient with the logo, large and faint, at the right edge |
| `logo-1280x540.png` | Steam logo with transparency (`<appid>_logo.png`) | Disc and the name, on a transparent background |

The name is set in Noto Sans (SIL OFL 1.1, `assets/fonts/`). The gradient and layout are new.

Moonlight XR has no wordmark, hero or capsule art of its own, only the logo and the Android launcher and store images, so the Steam images are composed here. From base Moonlight (moonlight-qt) nothing was needed: Moonlight XR's computer icon (`ic_computer.xml`) is the same Material Design glyph as moonlight-qt's `desktop_windows-48px.svg`, so the flat launcher's PC icon is unchanged, and the in-headset PC list draws its own monitor icon from lines.
