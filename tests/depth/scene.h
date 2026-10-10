// Ray traced test scenes for the depth model tests, with the true depth of
// every pixel. GPLv3.
#ifndef MLXR_DEPTH_TEST_SCENE_H
#define MLXR_DEPTH_TEST_SCENE_H

#include <stdint.h>

typedef enum {
    // A checkered floor running to a brick wall 12 m away, a large shaded
    // sphere 2.5 m from the camera on the floor in front of it, its shadow,
    // and a smaller box further back
    SCENE_ROOM = 0,
    // A near textured disc over a far, flat, textured backdrop; no floor
    SCENE_DISC = 1,
} SceneId;

// Renders w x h RGBA8 (rows top first) at a 16:9 field of view whatever w/h
// is (a 256 square is squashed the way the downscale squashes a 16:9 frame),
// and the distance along the view axis (z) of each pixel in zOut (may be
// NULL). shift moves the camera sideways in metres (for a moving sequence).
void sceneRender(SceneId id, int w, int h, float shift, uint8_t* rgba, float* zOut);

// Masks: 1 where the pixel shows that part of the scene
#define PART_FLOOR 1
#define PART_WALL 2
#define PART_NEAR 3   // the sphere or the disc
#define PART_BOX 4
void scenePart(SceneId id, int w, int h, float shift, uint8_t* partOut);

#endif
