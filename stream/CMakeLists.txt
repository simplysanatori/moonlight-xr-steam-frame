# Moonlight XR Native: stream side (orchestrator). GPLv3.
add_library(mlxr_stream STATIC frame_slot.c sw_decoder.c file_source.c)
target_link_libraries(mlxr_stream PUBLIC PkgConfig::AV pthread)
