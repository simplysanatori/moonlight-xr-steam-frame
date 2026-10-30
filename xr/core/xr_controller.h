// The controllers as they are drawn: whether the ray from one shows, whether
// the bundled model of one does, and where that model goes. Plain arithmetic
// over values handed in, no GL, no OpenXR calls and no context, so the host
// tests reach all of it.

#ifndef XR_CONTROLLER_H
#define XR_CONTROLLER_H

#include <stddef.h>

#include "xr_math.h"
#include "xr_shared.h"

// Whether the ray is switched on for the session: the setting the session
// started from, turned over each time the bar button or the Display tab's row
// flips it
int raySwitchOn(int settingOn, int flipped);

// What the flip has to be for the switch to read wantOn
int rayFlipFor(int settingOn, int wantOn);

// Whether the beam is drawn this frame. Switched off it is not, except while
// a panel is up (the settings panel, the picker, the keyboard, the exit prompt
// or the report sheet), so those can still be aimed at, and it goes again
// when they close. The cursor dot and the hit test never ask: with the beam
// gone a lightgun game still has its dot and every press still lands.
int rayDrawn(int settingOn, int flipped, int panelOpen);

// What the pointing line looks like this frame. RAY_BEAM_FULL is the beam
// rayDrawn allows. RAY_BEAM_RESTING is a faint beam for the one case the
// bar can otherwise only be found blind: the ray is switched off, the dot is
// off too (with the dot on it is the cue), no panel is up, and the controller
// is inside the bar's hover band, so the user can see where it points. It is
// off everywhere else, so the picture stays clean.
#define RAY_BEAM_NONE 0
#define RAY_BEAM_FULL 1
#define RAY_BEAM_RESTING 2
int rayBeamKind(int settingOn, int flipped, int panelOpen, int dotOn, int overBarBand);

// The cursor dot keeps the size it has on the default screen, PTR_DOT_SIZE_M
// across at PTR_DOT_REF_M away, as an angle at any distance, so it reads the
// same on a 14 m wall as on a panel near the viewer. Held between a floor,
// where a near picture would make it a speck, and a ceiling.
#define PTR_DOT_SIZE_M 0.022f
#define PTR_DOT_REF_M 3.0f
#define PTR_DOT_MIN_M 0.005f
#define PTR_DOT_MAX_M 0.15f

// The dot's width in metres where the ray lands distance metres from the head
float pointerDotSize(float distance);

// Whether one hand's controller model is drawn: the setting on, not in
// passthrough, where the real controller is in view, a controller profile on
// that hand rather than tracked hands or nothing, its grip pose live, and the
// grip placed with a tracked orientation. Position only needs to be valid, so
// a controller out of the cameras' view for a moment keeps its model where
// the runtime puts it. kind is a PROFILE_ value and flags the GATE_ bits, both
// from xr_gate.h.
int controllerModelShown(int settingOn, int passthrough, int kind, int poseActive,
                         unsigned flags);

// The model's placement: the grip pose as a column major matrix, the model
// being a right hand controller authored in grip space, mirrored across the
// grip's x for the left hand. Its 3x3 is a turn and at most a mirror, so it
// carries the normals as it is.
void controllerModelMatrix(XrPosef grip, int leftHand, float* out16);

// Where a point of the model lands, the same placement worked without the
// matrix
Vec3 controllerModelPoint(XrPosef grip, int leftHand, Vec3 local);

// ---- which model, and from which files --------------------------------------------------------
// The kind the session draws, from the on switch and the stored kind: off is 0, a kind out of range
// (or a switch on with no kind yet, the way the first version of the row set it) is the Frame's
int controllerModelKind(int on, int kind);

// Where a kind's model is placed: at the hand's grip (the Frame's, the generic and a user's own,
// all authored for the grip), or at its aim pose with the barrel along -z (the gun, so its muzzle
// is on the ray and the dot)
#define MODEL_SPACE_GRIP 0
#define MODEL_SPACE_AIM 1
int controllerModelSpace(int kind);

// "off", "frame", "generic", "gun", "custom" (the names the setting is written with)
const char* controllerModelKindName(int kind);

// What the loader reads, in the order it tries. The first that loads is the model; one that is
// missing is left out, one that is there but unusable is skipped by the loader with the reason on
// stderr. kind is what the candidate gives, which is not the kind asked for when it fell back
// (a gun without its files is the Frame's, the Frame's without its files the generic one).
#define MODEL_FILES_TEXTURED_PAIR 1   // two MXFM files, path[0] the left hand's and path[1] the right's
#define MODEL_FILES_TEXTURED_ONE 2    // one MXFM file, path[0], the right hand's, mirrored for the left
#define MODEL_FILES_PAINTED_PAIR 3    // two painted .room files, as the pair
#define MODEL_FILES_GENERIC 4         // the bundled controller.room, path[0], mirrored for the left
#define MODEL_CANDIDATES_MAX 6
#define MODEL_PATH_MAX 1024
typedef struct ModelCandidate {
    int format;                       // MODEL_FILES_*
    int kind;                         // CONTROLLER_MODEL_* it is
    char path[2][MODEL_PATH_MAX];
} ModelCandidate;

// The candidates for a kind, assetsDir/models/ holding the bundled files and the Frame's (made at
// install), userDir the folder the user's own are read from (the config folder's models/, NULL for
// none). kind off gives none. A user's pair needs both halves; one half alone is not used. Returns
// how many were written to out, which holds MODEL_CANDIDATES_MAX.
int controllerModelCandidates(int kind, const char* assetsDir, const char* userDir, ModelCandidate* out);

// Where the user's own models are read from: $MLXR_CONFIG_DIR/models, else
// ~/.config/moonlight-xr/models. Written into buf; returns buf, or NULL when there is neither
const char* controllerModelUserDir(char* buf, size_t cap);

#endif
