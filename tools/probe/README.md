# Frame probe scripts

Two read-only probes for the owner to run when the Steam Frame is back online.

- `frame-probe.sh` runs on the Frame (bash over SSH). It covers plan row 1.14:
  the decoder in its HEVC state (formats, framesizes, a TRY_FMT check for Q10C
  and P010, frame rate on the OUTPUT queue), an 8-bit control clip and a Main10
  clip through `mlxr-decode-test` if they exist, OpenCL, Chinese fonts, the
  Qt/FFmpeg/Mesa package versions, the USB gadget tree/UDC/Type-C partner/network
  state (plan row 4c.1), battery sysfs (every readable attribute), Wi-Fi power
  save, and the SteamVR render-model directories.
- `usb-probe-windows.ps1` runs on the PC. It is the PC side of plan row 4c.1:
  USB Ethernet adapters and their IPs, routes into the PC's link subnet, a ping to the
  Frame's device-side address (from `$env:MLXR_FRAME_USB_IP`), and an iperf3 hint.

Neither script changes system state. Each section prints "not present" for a
missing tool or file and the scripts keep going.

## Before you run the Frame probe

- Developer Mode on the Frame, and the SSH key already set up (`steamos@`, not `deck@`).
- Quit the Moonlight XR client on the Frame first if it is running. The probe
  reports with `fuser` whether a process holds `/dev/video-dec0` open. On the
  2026-10-29 run SteamVR was up and the decoder was free, so SteamVR itself does
  not need to be quit.

## Run it on the Frame

From the fork root in Git Bash:

```
tr -d '\r' < tools/probe/frame-probe.sh | ssh steamos@<frame-address> 'bash -s'
```

The `tr` is needed because the Windows checkout uses CRLF line endings, and bash
on the Frame cannot run a script with `\r` in it. From PowerShell:

```
(Get-Content -Raw tools\probe\frame-probe.sh) -replace "`r", '' | ssh steamos@<frame-address> 'bash -s'
```

The report is written on the Frame to `~/moonlight-xr-probe/<timestamp>/report.txt`
and printed to the terminal too.

### Optional inputs

- `mlxr-decode-test`: the probe looks on `PATH`, then in
  `~/moonlight-xr-probe/bin/` and `~/Games/MoonlightXRNative/app/`. `install.sh`
  does not install it, so copy a build from the PC side. Build the fork's
  `tests/video` target (aarch64 only) and run:

  ```
  ssh steamos@<frame-address> 'mkdir -p moonlight-xr-probe/bin'
  scp path/to/build/tests/video/mlxr-decode-test steamos@<frame-address>:moonlight-xr-probe/bin/
  ```

  Or set `MLXR_DECODE_TEST=/full/path` in the environment of the `bash -s` run.
- Two test clips are not downloaded. The owner makes both on the PC, from the
  same short source, so the two runs compare directly. Each is an Annex-B `.hevc`
  file (the source can be any mp4). The 8-bit control, `main8.hevc`, runs first;
  the Main10 clip, `main10.hevc`, is the one under test:

  ```
  ffmpeg -i source.mp4 -t 10 -an -c:v libx265 -pix_fmt yuv420p10le -profile:v main10 -f hevc main10.hevc
  ffprobe -v error -select_streams v -show_entries stream=profile,pix_fmt main10.hevc
  ffmpeg -i source.mp4 -t 10 -an -c:v libx265 -pix_fmt yuv420p -profile:v main -f hevc main8.hevc
  ffprobe -v error -select_streams v -show_entries stream=profile,pix_fmt main8.hevc
  ```

  The first ffprobe should report `Main 10` and `yuv420p10le`; the second should
  report `Main` and `yuv420p`. Copy both to `~/moonlight-xr-probe/clips/` on the
  Frame, or point `MLXR_MAIN8_CLIP` and `MLXR_MAIN10_CLIP` at them. A missing
  clip prints "not present" for its step.
- The decode runs are paced (`--fps 60`), which is the result. Lockstep
  (`--fps 0`) is printed too, for information only: the HEVC decoder buffers a
  few access units, so a lockstep FAIL on an 8-bit clip is expected and does not
  mean the decode failed. `mlxr-decode-test` asks the decoder for NV12 or Q08C
  only, so a Main10 PASS shows a 10-bit stream decoded into an 8-bit surface. It
  does not show a Q10C decode; the TRY_FMT lines show what the driver advertises.

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
LATEST=$(ssh steamos@<frame-address> 'ls -1t moonlight-xr-probe | head -n 1')
scp -r "steamos@<frame-address>:moonlight-xr-probe/$LATEST" ./frame-probe-results/
```

The reports include device identifiers (USB and Wi-Fi MAC addresses, the
`iw dev` output, the `lsusb` tree). Do not commit them; see `docs/PRIVACY_AUDIT.md`.
