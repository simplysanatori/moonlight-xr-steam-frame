# Moonlight XR Native

A native Linux Arm64 OpenXR game-streaming client for the Valve Steam Frame, written in C. It aims at feature parity with Sean Gilleece's [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr), but decodes on the Frame's Qualcomm Iris hardware decoder through V4L2 and renders with Vulkan on Turnip under SteamVR.

Work in progress. See `STATUS.md` for what has been confirmed on hardware.

## Build (on the Frame)
```
git clone --recursive https://github.com/simplysanatori/moonlight-xr-native
cd moonlight-xr-native && ./build.sh && ./packaging/install.sh
```

## Credits and licence
GPLv3 (`LICENSE.txt`). The XR modules are ported from Moonlight XR by Sean Gilleece; streaming uses [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c); the OpenXR loader comes from the Khronos [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) (Apache 2.0).
