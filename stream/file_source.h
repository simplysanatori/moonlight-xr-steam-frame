// Moonlight XR Native: plays a local video file into a decoder at its frame
// rate, looping (test source for the XR video path without a host). GPLv3.
#pragma once
#include "frame_slot.h"

typedef struct FileSource FileSource;
// hw: decode on the Iris decoder (published through hw_video_current());
// otherwise FFmpeg software decode into `out`.
FileSource* file_source_start(const char* path, FrameSlot* out, int hw);
// Call after xr_app_run returned (the hardware decoder closes here).
void file_source_stop(FileSource* s);
