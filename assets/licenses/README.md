# Third-party components and asset licences

This directory holds the licence texts that have no better home. The complete list of what the project contains or uses is the table below (also in `CREDITS.md` at the top of the repository and in the install directory). Licence texts for the assets sit next to them: `assets/depth/`, `assets/fonts/OFL.txt`, `assets/hrtf/kemar/LICENSE.txt`.

Files in this directory: `CC-BY-4.0.txt` (rooms, controller model), `moonlight-common-c_LICENSE.txt`, `enet_LICENSE.txt`, `nanors_LICENSE.txt`, `openxr-sdk_LICENSE.txt`, `jsoncpp_LICENSE.txt`, `zipdepth_LICENSE.txt`.

The room and controller notices as upstream words them: Copyright (c) 2026 Sean Gilleece / Woodford XR, Creative Commons Attribution 4.0, attribution to Sean Gilleece / Woodford XR (first made for Depthray, Woodford XR's VR video player). KEMAR: Copyright 1994 MIT Media Laboratory, "provided free with no restrictions on use, provided the authors are cited when the data is used in any research or commercial application"; citation: Bill Gardner and Keith Martin, "HRTF Measurements of a KEMAR Dummy-Head Microphone", MIT Media Lab Perceptual Computing Technical Report #280, May 1994.

| Component | Used for | Licence | Source | Licence text |
| --- | --- | --- | --- | --- |
| **Code that was ported or followed** | | | | |
| Moonlight XR, Sean Gilleece | Origin of every module under `xr/`, `ui/`, `shaders/`, `audio/` and `depth/`: the layout, gate, glow, grade, pinch, head-aim, gamepad and key logic, the panels, the depth pipeline, virtual surround | GPLv3 | https://github.com/Gilleece/moonlight-android-xr | `LICENSE.txt` |
| Moonlight XR for the Steam Frame (simplysanatori) | Patched Android build of Moonlight XR that ran on the Frame through Lepton; the reference for how it behaves there | GPLv3 | https://github.com/simplysanatori/moonlight-android-xr-steam-frame | `LICENSE.txt` |
| moonlight-qt | `backend/` follows its HTTP, pairing and launch behaviour line by line; `seed-from-moonlight-qt.sh` reads its settings file | GPLv3 | https://github.com/moonlight-stream/moonlight-qt | `LICENSE.txt` |
| moonlight-android | The Moonlight base that Moonlight XR is built on | GPLv3 | https://github.com/moonlight-stream/moonlight-android | `LICENSE.txt` |
| Nightfall (tB0nE) | Two ideas only (a learned upsampling head for the depth model, smoothing depth over real time), implemented independently | GPLv3 (no code taken) | https://github.com/tB0nE/nightfall | |
| **Libraries built into the binaries** | | | | |
| moonlight-common-c | The streaming protocol (RTSP, RTP, input, control, FEC) | GPLv3 | https://github.com/moonlight-stream/moonlight-common-c | `assets/licenses/moonlight-common-c_LICENSE.txt` |
| enet (Lee Salzman), inside moonlight-common-c | Reliable UDP | MIT | https://github.com/lsalzman/enet | `assets/licenses/enet_LICENSE.txt` |
| nanors (Joseph Calderon), inside moonlight-common-c | Reed-Solomon forward error correction | MIT | https://github.com/sleepybishop/nanors | `assets/licenses/nanors_LICENSE.txt` |
| Khronos OpenXR-SDK (release-1.1.63) | OpenXR loader (static) and headers | Apache-2.0 (headers also MIT) | https://github.com/KhronosGroup/OpenXR-SDK | `assets/licenses/openxr-sdk_LICENSE.txt` |
| JsonCpp, inside the OpenXR loader | Reads the OpenXR runtime manifest | MIT or public domain | https://github.com/open-source-parsers/jsoncpp | `assets/licenses/jsoncpp_LICENSE.txt` |
| stb_truetype, stb_image, stb_image_write (Sean Barrett) | Text rasterising, PNG/JPEG decoding, test PNG output; vendored in `ui/third_party/` | MIT or public domain | https://github.com/nothings/stb | end of each header |
| LiteRT / TensorFlow Lite 2.21.0 | Depth model runtime (static, built by `depth/tools/build-litert.sh`) | Apache-2.0 | https://github.com/tensorflow/tensorflow (tag v2.21.0) | `assets/depth/litert_LICENSE.txt` |
| XNNPACK, in LiteRT | CPU kernels | BSD-3-Clause | https://github.com/google/XNNPACK | `assets/depth/third_party_LICENSES.txt` |
| pthreadpool, in LiteRT | Thread pool | BSD-2-Clause | https://github.com/google/pthreadpool | `assets/depth/third_party_LICENSES.txt` |
| cpuinfo, in LiteRT | CPU feature detection | BSD-2-Clause | https://github.com/pytorch/cpuinfo | `assets/depth/third_party_LICENSES.txt` |
| FXdiv, in LiteRT | Integer division | MIT | https://github.com/Maratyszcza/FXdiv | `assets/depth/third_party_LICENSES.txt` |
| FP16, in LiteRT | Half-float conversion | MIT | https://github.com/Maratyszcza/FP16 | `assets/depth/third_party_LICENSES.txt` |
| KleidiAI, in LiteRT | Arm matrix kernels | Apache-2.0 | https://gitlab.arm.com/kleidi/kleidiai | `assets/depth/third_party_LICENSES.txt` |
| Abseil, in LiteRT | C++ utilities | Apache-2.0 | https://github.com/abseil/abseil-cpp | `assets/depth/third_party_LICENSES.txt` |
| ruy, in LiteRT | Matrix multiplication | Apache-2.0 | https://github.com/google/ruy | `assets/depth/third_party_LICENSES.txt` |
| gemmlowp, in LiteRT | Low-precision matrix multiplication | Apache-2.0 | https://github.com/google/gemmlowp | `assets/depth/third_party_LICENSES.txt` |
| FlatBuffers, in LiteRT | Model file format | Apache-2.0 | https://github.com/google/flatbuffers | `assets/depth/third_party_LICENSES.txt` |
| farmhash, in LiteRT | Hashing | MIT | https://github.com/google/farmhash | `assets/depth/third_party_LICENSES.txt` |
| fft2d (Takuya Ooura), in LiteRT | FFT | Permissive, own terms | http://www.kurims.kyoto-u.ac.jp/~ooura/fft.html | `assets/depth/third_party_LICENSES.txt` |
| Eigen, in LiteRT (MPL2-only build) | Linear algebra | MPL-2.0 | https://eigen.tuxfamily.org | `assets/depth/third_party_LICENSES.txt` |
| **Libraries linked dynamically from SteamOS (not shipped)** | | | | |
| FFmpeg 7.0 (libavcodec, libavformat, libavutil, libswscale) | Video decode helpers and software fallback | LGPL-2.1+ (SteamOS build) | https://ffmpeg.org | |
| OpenSSL 3.2 | Pairing, TLS, stream encryption | Apache-2.0 | https://www.openssl.org | |
| libcurl | HTTP and HTTPS to the host | curl licence (MIT style) | https://curl.se | |
| libxml2 | Host answers | MIT | https://gitlab.gnome.org/GNOME/libxml2 | |
| Opus | Audio decode | BSD-3-Clause | https://opus-codec.org | |
| PulseAudio client library (served by PipeWire on SteamOS) | Audio output | LGPL-2.1+ | https://www.freedesktop.org/wiki/Software/PulseAudio/ | |
| Vulkan loader | Rendering | Apache-2.0 | https://github.com/KhronosGroup/Vulkan-Loader | |
| **Models and data** | | | | |
| ZipDepth (Fabio Tosi), converted to TensorFlow Lite by Moonlight XR | Default depth model (`zipdepth_512x288_fp16.tflite`, `zipdepth_256_int8dr.tflite`) | MIT | https://github.com/fabiotosi92/ZipDepth | `assets/depth/zipdepth_LICENSE.txt` |
| MiDaS v2.1 small (Intel ISL), converted to TensorFlow Lite by Moonlight XR | Alternative depth model (`midas_v21_small_256_fp16.tflite`) | MIT | https://github.com/isl-org/MiDaS | `assets/depth/midas_LICENSE.txt` |
| KEMAR HRTF measurements, Bill Gardner and Keith Martin, MIT Media Lab, 1994 | Virtual surround (37 elevation-0 files, unchanged) | Free to use, authors must be cited | https://sound.media.mit.edu/resources/KEMAR.html | `assets/hrtf/kemar/LICENSE.txt` |
| **Art and fonts** | | | | |
| Home Theater, Grand Cinema and Synthwave environments (`.room`, `.atlas`, `thumbs/`), Sean Gilleece / Woodford XR | Viewing rooms | CC BY 4.0 | https://github.com/Gilleece/moonlight-android-xr | `assets/licenses/CC-BY-4.0.txt` |
| Controller model (`assets/models/controller.room`), Sean Gilleece / Woodford XR | Controller drawn in the headset | CC BY 4.0 | https://github.com/Gilleece/moonlight-android-xr | `assets/licenses/CC-BY-4.0.txt` |
| Noto Sans 2.013 (Regular, Medium), The Noto Project Authors | All interface text | SIL OFL 1.1 | https://github.com/notofonts/latin-greek-cyrillic | `assets/fonts/OFL.txt` |
| `settings_icon.png`, `kofi_qr.png` | Settings button, support sheet | Upstream states no separate licence; shipped under GPLv3 | https://github.com/Gilleece/moonlight-android-xr | |
| **Build-time tools (nothing from them ships)** | | | | |
| astc-encoder (Arm) | Compresses the room textures | Apache-2.0 | https://github.com/ARM-software/astc-encoder | |
| Python `qrcode` and Pillow | Made `kofi_qr.png` once | BSD / HPND | | |
| **Programs this one talks to (not shipped)** | | | | |
| Sunshine, Apollo | The streaming host | GPLv3 | https://github.com/LizardByte/Sunshine, https://github.com/ClassicOldSong/Apollo | |
| SteamVR (OpenXR runtime on the Frame) | Compositor and tracking | Valve, proprietary | | |
