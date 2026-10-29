#!/usr/bin/env python3
# Ask SteamVR's compositor for a stereo screenshot of what the headset shows. GPLv3.
#   vrshot.py <base>      writes <base>_preview.png and <base>_vr.png (give an absolute path:
#                         SteamVR's server process writes them, not this script)
# Run on the Frame with the host's libraries: env -u LD_LIBRARY_PATH python3 -I vrshot.py /tmp/x
# With no VR app drawing, the request came back with error 108 (already in progress) and no file
# appeared (seen 2026-10-29); the shots are meant for a running session. Open the PNGs with an image viewer.
# From a PC: tools/frame-deploy.sh shot <outdir>
import ctypes, sys, time
lib = ctypes.CDLL('/opt/steamvr/bin/linuxarm64/libopenvr_api.so')
err = ctypes.c_int(0)
lib.VR_InitInternal2.restype = ctypes.c_uint32
lib.VR_InitInternal2.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_int, ctypes.c_char_p]
tok = lib.VR_InitInternal2(ctypes.byref(err), 3, None)   # VRApplication_Background
print('init', err.value)
if err.value: sys.exit(1)
lib.VR_GetGenericInterface.restype = ctypes.c_void_p
lib.VR_GetGenericInterface.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]
p = lib.VR_GetGenericInterface(b'FnTable:IVRScreenshots_001', ctypes.byref(err))
print('iface', hex(p or 0), err.value)
FN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_uint32), ctypes.c_char_p, ctypes.c_char_p)
table = (ctypes.c_void_p * 7).from_address(p)
take = FN(table[5])
h = ctypes.c_uint32(0)
base = sys.argv[1]
r = take(ctypes.byref(h), (base + '_preview').encode(), (base + '_vr').encode())
print('take', r, h.value)
time.sleep(3)
lib.VR_ShutdownInternal()
