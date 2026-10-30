// The plain geometry of what hangs around the picture: which handle a point
// is over, where the bar's row hangs, and the stand in screen a room measures
// its panels against, with the arithmetic behind the settings panel's tracks
// and presets.
// Nothing in here touches GL, OpenXR beyond its plain value types, or the
// context, so it can be built and checked on a desktop as well as on the
// headset.

#ifndef XR_LAYOUT_H
#define XR_LAYOUT_H

#include "xr_math.h"
#include "xr_shared.h"

// What the ray is over. Handles only show while hovered, which is how spatial
// panels usually behave: nothing visible until you go looking for it.
#define HOVER_NONE   0
#define HOVER_SCREEN 1
#define HOVER_BAR    2
#define HOVER_CORNER 3
#define HOVER_ENVBUTTON 4
#define HOVER_PICKER    5
// Nothing under the ray, but close enough to the screen to keep drawing it
#define HOVER_HALO      6
#define HOVER_HINT      7
#define HOVER_COGBUTTON 8
#define HOVER_COGPANEL  9
#define HOVER_KBBUTTON  10
#define HOVER_KBPANEL   11
#define HOVER_EXITBUTTON 12
#define HOVER_EXITPROMPT 13
#define HOVER_STEREOBUTTON 14
#define HOVER_REPORT    15
#define HOVER_RAYBUTTON 16
#define HOVER_AIMBUTTON 17
#define HOVER_PADBUTTON 18
#define HOVER_KOFI      19
// Native (owner's request): the keyboard's own move bar, under the keyboard
#define HOVER_KBBAR     20
// Native (owner's request): the stats window, where nothing else is behind it
#define HOVER_STATS     21
// Native (lane wave2-bind-wire): the bar's Host button and the Host sheet
#define HOVER_HOSTBUTTON 22
#define HOVER_HOST      23
// How far past each edge that reaches, as a fraction of the screen
#define HALO_FRAC 0.5f

// Handle art, one small swapchain each so there is no atlas offset convention
// to get wrong
#define BAR_TEX_W 256
#define BAR_TEX_H 24
#define CORNER_TEX_W 64
#define CORNER_TEX_H 64

// All as a fraction of screen width, so the handles keep their proportions as
// the screen is resized
#define BAR_WIDTH_FRAC  0.14f
// Height follows the art rather than being picked separately. The two used to
// disagree by 2.5x, which stretched the rounded ends into a slab.
#define BAR_HEIGHT_FRAC (BAR_WIDTH_FRAC * (float)BAR_TEX_H / (float)BAR_TEX_W)
#define BAR_GAP_FRAC    0.035f
#define CORNER_FRAC     0.075f
// Hover zones are bigger than the art, since aiming at a thin bar is fussy
#define HOVER_MARGIN 1.7f
#define CORNER_HOVER 1.5f
// The bar is small on purpose, so its hover zone is proportionally wider
#define BAR_HOVER 2.0f

// The screen the panels hang against inside a room: the size and distance the
// picture starts at outside one, straight ahead of the seat at eye level. A
// room hangs its own picture on a wall metres off and as wide as the wall, so
// measuring the panels against that gave them a different size and distance in
// every room, and shrank them along with the picture. The bar keeps the size
// it has on this screen but hangs under the room's picture (see BarFrame).
#define STAND_IN_WIDTH_M 3.0f
#define STAND_IN_DISTANCE_M 3.0f

// The buttons along the bar, square, the bar's own pill between the
// environment and cog buttons, and the gap between neighbours, all as
// fractions of the width of the frame the bar is laid out in
#define ENV_BUTTON_FRAC 0.048f
#define ENV_GAP_FRAC 0.02f
#define COG_BUTTON_FRAC 0.048f
// How far under the frame's bottom edge the middle of the bar's row is
#define BAR_DROP_FRAC (BAR_GAP_FRAC + BAR_HEIGHT_FRAC * 0.5f)

// The buttons in the bar's row, numbered for the placement below
#define BAR_SLOT_ENV    0
#define BAR_SLOT_COG    1
#define BAR_SLOT_KB     2
#define BAR_SLOT_EXIT   3
#define BAR_SLOT_PAD    4
#define BAR_SLOT_AIM    5
#define BAR_SLOT_RAY    6
#define BAR_SLOT_STEREO 7
// Native (lane wave2-bind-wire): the Host sheet's, outermost on the right
#define BAR_SLOT_HOST   8
#define BAR_SLOTS       9

// The rectangle the bar and its buttons are laid out in: the picture's shape,
// its bottom edge on the bottom edge of the picture as drawn and centred on
// it, in the picture's own plane. Outside a room that is the picture itself,
// curved with it when it is. In a room it is as wide as makes the bar look
// the size it does on the stand in, wherever the room hangs the picture and
// however small it is, and flat, since a room's picture is.
typedef struct {
    XrPosef pose;
    float width;
    float height;
    float radius;
    int curved;
} BarFrame;

// That frame under a room's picture, whose centre is at picture and whose
// height is pictureHeight, for a picture of that shape, height over width.
// Distances are from the seat, which is the origin of the space it is in.
BarFrame roomBarFrame(XrPosef picture, float pictureHeight, float aspect);

// How wide that frame is for a bar row hanging from bottomMid, the middle of
// the picture's bottom edge, with down the picture's own down: whatever makes
// the bar's width over its distance from the seat what it is on the stand in
float barFrameWidth(Vec3 bottomMid, Vec3 down, float aspect);

// Where a button's middle sits in a frame that wide and high, in the frame's
// own flat coordinates from its centre, and how big it is
void barSlotPlacement(int slot, float width, float height, Vec3* outLocal, float* outSide);

// Whether a point on that frame, u and v across it from the top left, is on
// a button, which reaches a little further than it draws
int barSlotHit(int slot, float u, float v, float width, float height);

// Every face of every button along the bar shares one texture, a strip of
// BUTTON_TEX cells side by side, so the row costs one swapchain rather than
// one a face. One row, so a cell is picked by its x offset alone and never
// depends on which corner a runtime counts an image rect's y from. A switch
// has its off face in its cell and its on face in the next.
#define BTN_CELL_KB     0
#define BTN_CELL_COG    1
#define BTN_CELL_ENV    2
#define BTN_CELL_EXIT   3
#define BTN_CELL_STEREO 4
#define BTN_CELL_RAY    6
#define BTN_CELL_AIM    8
#define BTN_CELL_PAD    10
#define BTN_CELL_HOST   12
// Native (PLAN 4.3): the 3D button's on face with a host-rendered pair, which
// says SBS or TB in place of 3D (its off face is the 3D button's own)
#define BTN_CELL_STEREO_SBS 13
#define BTN_CELL_STEREO_TB  14
#define BTN_CELLS       15
#define BTN_ATLAS_COLS  15
#define BTN_ATLAS_ROWS  1
#define BTN_ATLAS_W     (BTN_ATLAS_COLS * BUTTON_TEX)
#define BTN_ATLAS_H     (BTN_ATLAS_ROWS * BUTTON_TEX)

// Where a cell starts in that texture, in texels from its first column and
// its first row as uploaded, which is the image rect's offset. 0 for a cell
// past the strip.
int buttonCellOrigin(int cell, int* outX, int* outY);

// The 3D button's face: its off face, or the on face for the 3D source in
// force (STEREO_SOURCE_*: Depth shows 3D, SBS and Top-bottom their own)
int barStereoCell(int source, int on);

// Writes one face, BUTTON_TEX square RGBA rows running top down the way a
// Bitmap does, into its cell of a copy of the whole texture kept in upload
// order. Flipped on the way, so it shows the right way up. 0 for a cell past
// the strip, which writes nothing.
int buttonCellPut(unsigned char* atlas, int cell, const unsigned char* face);

// Which affordance a point on a screen of that size is over, u and v across
// it from the top left. cornerSide is how big the corner brackets are drawn,
// in metres, and 0 where there are none, which lets the ray fall through to
// what is behind them. Corners are numbered 0 top left, 1 top right, 2 bottom
// left, 3 bottom right.
int hoverTest(float u, float v, float width, float height, float cornerSide, int* corner);

// Where the stand in screen hangs, square to the seat
XrPosef standInPose(void);

// The Room tab's lanes. A place along a track, 0 at the left end and 1 at the
// right, turned into the whole units the preference is stored in, and back,
// and the percent of the lane drawn beside it.
int laneUnits(float t, int min, int max);
float lanePlace(int units, int min, int max);
int lanePercent(int units, int min, int max);

// Where a row sits down the settings panel, as a fraction of its height, how
// far either side of that the row's hit band reaches, and how tall a row of
// cells is drawn, half of it. The display and screen tabs pack their rows
// closer than the rest. tab is one of the COG_TAB_ values, or anything past
// them for a face that is not a tab.
float cogRowV(int tab, int row);
float cogRowHalf(int tab);
float cogCellHalf(int tab);

// How big a track's thumb is drawn, as a fraction of the panel's height,
// before it grows under the ray
float cogThumbSize(int tab);

// Which part of a track's row a point across the panel is on: the button at
// the left end that steps down, the one at the right that steps up, the run
// between them where a press jumps the thumb, or none of it
#define TRACK_PART_NONE 0
#define TRACK_PART_RUN 1
#define TRACK_PART_DOWN 2
#define TRACK_PART_UP 3
int cogTrackPart(float pu);

// A place along the run, 0 at its left end and 1 at its right, from a point
// across the panel and back
float cogRunPlace(float pu);
float cogRunU(float t);

// The step a press of a step button lands on, for a track of that many
// steps with the thumb at t: the next whole step that way, so a thumb a drag
// left between two goes to the nearer one in that direction. Held to the
// track's ends.
int cogStepIndex(float t, int steps, int dir);

// How many steps a track has, each the unit its value is kept in or a sensible
// share of a value kept as a float, or 0 for a row that is not a track. tab
// is one of the COG_TAB_ values, or anything past them for the Room tab.
int cogTrackSteps(int tab, int row);

// Whether a point on the settings panel is on the About tab's report button,
// or its Ko-fi button
int cogReportButtonAt(float pu, float pv);
int cogKofiButtonAt(float pu, float pv);

// Which part of the report sheet a point on it is over, one of the
// REPORT_ZONE_ values, u and v across the sheet from its top left
int reportZone(float u, float v);

// Which of the hand lock hint's two buttons a point on it is over, one of the
// HINT_ZONE_ values, u and v across the sheet from its top left
int handHintZone(float u, float v);

// Whether a point on the Ko-fi sheet is on its Close button, one of the
// KOFI_ZONE_ values, u and v across the sheet from its top left
int kofiSheetZone(float u, float v);

// The 3D tab's depth track. A separation, as a fraction of frame width, in the
// tenths of a percent the preference stores, which is also the step the track
// moves in, and back.
int separationUnits(float separation);
float separationOf(int units);

// Native (lane wave3-visual). The Picture tab's Background dim: a percent held
// to its lane and its five percent steps, and the share of the environment's
// light that is left of it (1 at 0 percent, 0 at 100).
int bgDimClamp(int percent);
float bgDimKeep(int percent);
// The Picture tab's Glow edge: how deep into the picture the fade reaches, as
// a fraction of its width and height (0 for off, and for a value out of range)
float glowFeatherFraction(int mode);
// 3D Eye Balance. The percent the preference stores as -1..1, and the share of
// one separation each eye is shifted by: left 1 + b, right 1 - b, so an even
// 0 is a whole separation each way, +1 is two in the left eye and none in the
// right, -1 the other way round, and the two always add up to two. The reach
// is the larger of the two, which the warp's search window is sized for.
// What lies behind everything else in the views, for the layer that holds it
// (a 16x16 image in a projection layer of its own, or the clear of the
// picture's). mode is BACKDROP_PASSTHROUGH, the cameras showing through, for
// which it is black at the dim's share of alpha, or BACKDROP_VOID, which is
// the void's colour taken down by the dim and opaque. Fills out with sRGB red,
// green, blue and alpha and returns 1 when a layer is wanted: not in a room
// (BACKDROP_NONE, which dims through the room's own shader), and not for a
// black void, which is what the runtime shows with nothing there.
#define BACKDROP_NONE 0
#define BACKDROP_PASSTHROUGH 1
#define BACKDROP_VOID 2
int backdropFor(int mode, int dimPercent, const float voidColor[3], float out[4]);
// One sRGB channel as the linear value a render target of an _SRGB format is
// cleared with to hold it
float srgbToLinear(float c);

float eyeBalanceOf(int percent);
void eyeShares(float balance, float* left, float* right);
float eyeShareReach(float balance);

// Which preset a separation in those units is, as its cell, or -1 when it is
// none of them. The three are in cell order. Balanced is tried first, so a
// default near an end that clamps another preset onto it still reads as
// Balanced.
int cogPresetAt(int units, const int presets[COG_PRESET_CELLS]);

// Native (PLAN 4.3): whether a 3D tab row can act. The 3D source row always
// can; the others need a stereo mode (stereoOk); and with a host-rendered pair
// (source not STEREO_SOURCE_DEPTH) the preset and depth rows have no depth to
// set, so only the convergence track and the switch act.
int cog3dRowLive(int stereoOk, int source, int row);

// Native (PLAN 4.3): the host-rendered pair's plain arithmetic, apart from the
// session so a desktop test can hold it. The eye shown for each view: eye 0 the
// left (or top) half, eye 1 the right (or bottom), and with eye_swap on the
// halves exchanged (0 and 1 only; anything else is eye 0).
int sbsEyeHalf(int eye, int swap);
// What one eye sees of a decoded frame of frameW x frameH: the whole frame for
// a half size pair (stretched back to the frame's shape), one eye's half for a
// full size one (side by side halves the width, top-bottom the height).
void sbsPictureSize(int source, int full, int frameW, int frameH, int* w, int* h);
// A point on the picture (0..1 each way) onto the left (top) eye's half of the
// frame, where the PC's pointer lives
void sbsPointerUv(int source, float* u, float* v);
// The pixel rectangle of an eye's half in the decoded frame (x, y, w, h)
void sbsHalfRect(int source, int half, int frameW, int frameH, int rect[4]);
// Whether the letterbox detector runs and its crop applies: not on a
// host-rendered pair (stereoMode DEPTH_MODE_SBS), whose seam is no letterbox
int ambiBarDetectApplies(int detectSetting, int stereoMode);

// How much of a room's screen the picture hangs at, in whole percent: inside
// the size row's lane where the room may be resized, and all of it where not
int roomScreenClamp(int percent, int resizable);

// Where a corner drag in a room leaves the picture: the size it started at,
// scaled by how far along the half diagonal the ray now reaches against how
// far it reached when the grab began, in the same whole percent and the same
// lane. Against the start rather than the corner, so taking hold of a bracket
// that hangs well outside a small picture does not throw the picture out to
// the ray.
int roomResizePercent(int startPercent, float startReach, float reach);

// ---- screen presets, bringing the screen here, lazy follow ----------------------------
// The Screen tab's presets: how far the screen is from the seat and how wide
// it is, in centimetres. Only those two: it stays in the direction it sits in
// (so its height scales with its distance, as the Distance track does), and
// its tilt, roll and curve stay as they were set.
#define LAYOUT_PRESET_DESK 0
#define LAYOUT_PRESET_COUCH 1
#define LAYOUT_PRESET_CINEMA 2
#define LAYOUT_PRESETS 3
typedef struct {
    int distanceCm;
    int widthCm;
} LayoutPreset;
LayoutPreset layoutPresetOf(int preset);

// Which preset a distance and width are, or -1: a centimetre or two either
// way still counts, since both went through a float on the way to the file
int layoutPresetAt(int distanceCm, int widthCm);

// The way the head looks, as a unit vector, held to LAYOUT_GAZE_PITCH_MAX_DEG
// up or down so a glance at the ceiling does not take the screen there
#define LAYOUT_GAZE_PITCH_MAX_DEG 40.0f
Vec3 layoutGazeDir(XrQuaternionf head);

// The screen turned about the vertical to face the head, with its tilt and
// roll kept. Unchanged when it is nearly over or under the head.
XrQuaternionf layoutFaceHead(XrQuaternionf orientation, Vec3 screenPos, Vec3 headPos);

// "Bring here": the screen where the head looks, as far from the head as it
// was, facing it, the same size. Only its pose; the width is not touched.
XrPosef layoutBringHere(XrPosef screen, XrPosef head);

// Where the screen is when it sits dir from the head, as far from it as it
// was, facing it with its tilt and roll kept
XrPosef layoutScreenAlong(XrPosef screen, Vec3 headPos, Vec3 dir);

// Lazy follow. The screen is world locked until the gaze is more than the
// leash from it in yaw or in pitch; then it eases after the gaze, a slerp over
// FOLLOW_EASE_SEC, and is world locked again where it lands. Directions are
// unit vectors from the head: screenDir to the screen's middle, gazeDir the way
// the head looks.
#define FOLLOW_EASE_SEC 0.6f
typedef struct {
    int easing;     // 1 from the moment the leash breaks until it lands
    float t;        // 0..1 along the ease
    Vec3 from;
    Vec3 to;
} FollowState;
void followReset(FollowState* st);
int followOutsideLeash(Vec3 screenDir, Vec3 gazeDir, float leashRad);
// Where the screen should be this frame, dt seconds on from the last. Not
// moving, it returns screenDir untouched. A gaze that moves on while it eases
// is chased: the ease starts again from where the screen is.
Vec3 followStep(FollowState* st, Vec3 screenDir, Vec3 gazeDir, float dt, float leashRad);

// Angular-size lock: the width that keeps a screen the same size in the view
// after it has been pushed or pulled from distFrom to distNow metres from the
// head, held to the widths a screen may have
float layoutAngularWidth(float width, float distFrom, float distNow, float minWidth, float maxWidth);

// How big a corner bracket on a room's picture is drawn, in metres, for a
// picture that far from the seat: whatever size looks the same as a bracket
// on the stand in screen, so a picture on a far wall or shrunk to a quarter
// still has corners the size of the ones outside a room
float roomCornerSide(float distance);

#endif
