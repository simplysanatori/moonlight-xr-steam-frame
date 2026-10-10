// Moonlight XR Native: plays a local video file into a decoder at its frame
// rate, looping (test source for the XR video path without a host). GPLv3.
#pragma once
#include "frame_slot.h"

typedef struct FileSource FileSource;
FileSource* file_source_start(const char* path, FrameSlot* out);
void file_source_stop(FileSource* s);
