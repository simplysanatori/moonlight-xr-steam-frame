# au-scatter lane report (branch lane/au-scatter)

The black picture of the owner's runs on 2026-10-11 at 05:33 and 05:34 (HEVC
2560x1440 at 90 fps, 126000 kbps). The hardware decoder flagged almost every
picture as an error (6673 errors, 84 good pictures in 40 s), and
moonlight-common-c sent about 190 IDR frame requests a second.

## Cause

`drSubmit()` in stream/session.c handed the hardware decoder a fixed array of
64 parts:

    const void* parts[64];
    for (PLENTRY e = du->bufferList; e && n < 64; e = e->next) ...

moonlight-common-c makes one buffer entry per video packet. At packetSize 1392
an entry holds 1376 bytes, the first packet of a frame 1368, and an IDR frame
has its VPS, SPS and PPS as three more entries. Any frame over 64 packets
(about 86 KB) was cut to 64 entries without a word. The log shows the cut
sizes in its "no free OUTPUT buffer" lines:

- 88056 = 1368 + 63 * 1376: a P frame cut at 64 entries
- 83928 = 1368 + 60 * 1376: an IDR frame, VPS, SPS and PPS took three of the 64

At 126 Mbps and 90 fps a frame is about 175 KB, so every frame reached the
decoder cut short and the firmware flagged every picture. At 80 Mbps it hits
IDR frames and high-motion frames. The software decoder copies every entry
and was not affected.

Two more faults made it worse:

- stream/hw_video.c asked for an IDR frame on every decoder error, on the
  decode thread (receive errors) and through DR_NEED_IDR (submit errors),
  with no limit. Each request makes the host send a frame at once, so the
  host sent about 185 frames a second, all of them cut and flagged again.
- `decodedOut` counted only good pictures. An AU that ended in an error
  stayed "in the decoder" forever: the stats showed "AUs in the decoder 64.00
  avg", and the frame loop's 4 ms latch waited on every refresh for a picture
  that never came ("latched 444 times, 0 got the picture").

## What changed

- stream/au_gather.c/.h (new): `mlxr_au_gather()` turns the decode unit's
  entry list into parts and sizes, every entry, in order. The arrays grow on
  the heap and are kept from call to call (as the software path keeps
  `g_au`). It returns `MLXR_AU_GATHER_MISMATCH` when the summed lengths
  differ from `du->fullLength`.
- stream/session.c `drSubmit()`: uses the gather. A mismatch logs a warning
  (at most once a second) and the AU is not submitted. It is counted as
  refused, and the session answers DR_NEED_IDR when an IDR request is due
  under the limit below, else DR_OK.
- stream/hw_video.c: one IDR request limit for the decoder error paths:
  at most one request per `MLXR_VIDEO_IDR_MIN_MS` (100 ms by default, 0 no
  limit), logged once when the decoder opens. Receive errors and submit
  errors share it; `hw_video_submit()` returns -1 (DR_NEED_IDR) only when a
  request is due. The damaged-picture path keeps its own 500 ms.
- stream/hw_video.c: a picture the decoder flags as an error
  (`MLXR_VDEC_E_DECODE`) now counts as out of the decoder. The latch also
  waits only while an AU is in the decoder by that count, and a latched
  loop stops waiting when the AU it waits for ends in an error.
- Stats: `HwVideoStats` gains `largestAuBytes`, `largestAuParts` and
  `refusedAus`. The 5 s line's "hw decode" part now ends with
  "largest AU N bytes in N parts, N refused (length mismatch)" while
  streaming, and "largest AU N bytes in N parts" for a file.
- docs/USER_GUIDE.md: `MLXR_VIDEO_IDR_MIN_MS` in the environment table.
- tests/stream/test_au_gather.c (new, ctest `stream_au_gather`): an IDR unit
  of VPS, SPS, PPS and 400 packets, a P unit of 200 packets (1368, then
  1376 each), every byte checked in order after joining the parts; the arrays
  kept across calls; mismatches (one byte more, one packet less, a negative
  entry, a negative length) refused; a short list of three entries, a single
  entry and an empty list.

video/v4l2_decoder.c is unchanged.

## Tested here (headless)

- The full ctest without audio_pulse: 133 of 133 passed (two launcher
  tests skipped, the launcher was not built).
- A 10 s run of the 1080p60 H.264 test file on the hardware decoder:
  "hw decode 1.5 ms (last 1.5), frames 613, taken 579, skipped 29, errors 0,
  largest AU 104928 bytes in 1 parts", "AUs in the decoder 1.00 avg (max 1)",
  "latched for the decoder 55 times (55 got the picture, 0.6 ms avg)".
  SteamVR kept the app in the synchronized state, so nothing was drawn. The file path
  submits one part per packet, so it does not exercise the gather; the unit
  test does.
- tools/privacy-check.sh: 0 findings.

## For the owner to confirm in the headset

Run the same settings again (HEVC 2560x1440, 90 fps, 126000 kbps). In the 5 s
stats line:

- the "hw decode" part shows errors 0 (or near 0) and a largest AU of well
  over 88056 bytes, in more than 64 parts;
- "video ... fps drawn" near 90;
- "AUs in the decoder" near 1, and the latch's "got the picture" close to its
  count;
- the latency line's "IDR requests" a handful, not hundreds.

If errors stay high with a largest AU over 64 parts, the cut was not the
only cause, and the next suspect is the OUTPUT buffer size for very large
IDR frames (an "exceeds the ... OUTPUT buffer" `[vdec]` line would show it).
