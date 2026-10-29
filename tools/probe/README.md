# Frame probe scripts

Two read-only probes for the owner to run when the Steam Frame is back online.

- `frame-probe.sh` runs on the Frame (bash over SSH). It covers plan row 1.14:
  the decoder in its HEVC state (formats, framesizes, S_PARM), a Main10 clip
  through `mlxr-decode-test` if both exist, clinfo, Chinese fonts, the Qt/FFmpeg/
  Mesa package versions, the USB gadget/UDC/Type-C state (plan row 4c.1), battery
  sysfs, Wi-Fi power save, and the SteamVR render-model directories.
- `usb-probe-windows.ps1` runs on the PC. It is the PC side of plan row 4c.1:
  USB Ethernet adapters and their IPs, routes into 10.86.200.0/24, a ping to the
  Frame's 10.86.200.233, and an iperf3 hint.

Neither script changes system state. Each section prints "not present" for a
missing tool or file and the scripts keep going.

## Before you run the Frame probe

- Developer Mode on the Frame, and the SSH key already set up (`steamos@`, not `deck@`).
- Quit the VR client and SteamVR on the Frame first. A running client can hold
  `/dev/video-dec0` open, and the decoder section then reports busy.

## Run it on the Frame

From the fork root in Git Bash:

```
tr -d '\r' < tools/probe/frame-probe.sh | ssh steamos@192.168.0.168 'bash -s'
```

The `tr` is needed because the Windows checkout uses CRLF line endings, and bash
on the Frame cannot run a script with `\r` in it. From PowerShell:

```
(Get-Content -Raw tools\probe\frame-probe.sh) -replace "`r", '' | ssh steamos@192.168.0.168 'bash -s'
```

The report is written on the Frame to `~/moonlight-xr-probe/<timestamp>/report.txt`
and printed to the terminal too.

### Optional inputs

- `mlxr-decode-test`: the probe looks on `PATH`, then in
  `~/moonlight-xr-probe/bin/` and `~/Games/MoonlightXRNative/app/`. `install.sh`
  does not install it, so copy a build from the PC side. Build the fork's
  `tests/video` target (aarch64 only) and run:

  ```
  ssh steamos@192.168.0.168 'mkdir -p moonlight-xr-probe/bin'
  scp path/to/build/tests/video/mlxr-decode-test steamos@192.168.0.168:moonlight-xr-probe/bin/
  ```

  Or set `MLXR_DECODE_TEST=/full/path` in the environment of the `bash -s` run.
- A Main10 clip is not downloaded. Make one on the PC from any short source:

  ```
  ffmpeg -i source.mp4 -t 10 -an -c:v libx265 -pix_fmt yuv420p10le -profile:v main10 -f hevc main10.hevc
  ffprobe -v error -select_streams v -show_entries stream=profile,pix_fmt main10.hevc
  ```

  The second command should report `Main 10` and `yuv420p10le`. Copy it to
  `~/moonlight-xr-probe/clips/main10.hevc` on the Frame, or point
  `MLXR_MAIN10_CLIP` at it. Without a clip the decode-test step prints "not present".

## Run the PC side

With the Frame on its USB-C data cable:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\probe\usb-probe-windows.ps1
```

If the Frame is in Developer Mode and the USB Ethernet link is up, an adapter
with NCM, RNDIS or "USB Ethernet" in its name should show with a 10.86.200.x
address. If none matches, the script prints all adapters so you can see the
real name. Also check by hand whether the PC starts charging the Frame over the
cable. The script cannot tell.

## Copy the results back

```
LATEST=$(ssh steamos@192.168.0.168 'ls -1t moonlight-xr-probe | head -n 1')
scp -r "steamos@192.168.0.168:moonlight-xr-probe/$LATEST" ./frame-probe-results/
```

The reports include device identifiers (USB and Wi-Fi MAC addresses, the
`iw dev` output, the `lsusb` tree). Do not commit them; see `docs/PRIVACY_AUDIT.md`.
