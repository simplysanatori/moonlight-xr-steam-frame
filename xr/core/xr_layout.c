// Which handle a point is over, the stand in screen the panels hang against
// in a room, where the bar's row hangs under the picture, and the settings
// panel's tracks and presets. No GL and no context, so the host tests reach
// all of it.
#include <stdlib.h>
#include <string.h>

#include "xr_layout.h"
#include "xr_shared.h"

int hoverTest(float u, float v, float width, float height, float cornerSide, int* corner) {
    if (cornerSide > 0.0f) {
        // Centred where the bracket is drawn, which is a half bracket outside
        // the corner in both axes, with the same reach each way as before
        float reachM = cornerSide * CORNER_HOVER * 0.5f;
        float cu = reachM / width;
        float cv = reachM / height;
        float outU = cornerSide * 0.5f / width;
        float outV = cornerSide * 0.5f / height;

        int left = fabsf(u + outU) < cu;
        int right = fabsf(u - (1.0f + outU)) < cu;
        int top = fabsf(v + outV) < cv;
        int bottom = fabsf(v - (1.0f + outV)) < cv;
        if ((left || right) && (top || bottom)) {
            *corner = (top ? 0 : 2) + (right ? 1 : 0);
            return HOVER_CORNER;
        }
    }

    if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
        return HOVER_SCREEN;
    }

    // The move bar sits under the bottom edge, so v runs past 1 here
    float barU = BAR_WIDTH_FRAC * BAR_HOVER * 0.5f;
    float reach = (BAR_GAP_FRAC + BAR_HEIGHT_FRAC * 3.0f) * width / height;
    if (v > 1.0f && v < 1.0f + reach && fabsf(u - 0.5f) < barU) {
        return HOVER_BAR;
    }

    // Beyond the picture the ray still draws out to a margin, so it does not
    // blink out on the way to the handles underneath
    if (u > -HALO_FRAC && u < 1.0f + HALO_FRAC && v > -HALO_FRAC && v < 1.0f + HALO_FRAC) {
        return HOVER_HALO;
    }

    return HOVER_NONE;
}

// Straight ahead at eye level, facing back at the seat the way the picture
// faces the viewer, which is the pose the screen starts at outside a room
XrPosef standInPose(void) {
    XrPosef pose;
    memset(&pose, 0, sizeof(pose));
    pose.orientation.w = 1.0f;
    pose.position.z = -STAND_IN_DISTANCE_M;
    return pose;
}

// How far the middle of the bar is from the seat on the stand in, for a
// picture of that shape
static float standInBarDistance(float aspect) {
    float drop = STAND_IN_WIDTH_M * (aspect * 0.5f + BAR_DROP_FRAC);
    return sqrtf(STAND_IN_DISTANCE_M * STAND_IN_DISTANCE_M + drop * drop);
}

// The bar's row hangs a share of the frame's width under the edge, so its
// distance, and with it the width wanted, moves a little with the width. Each
// round moves it a twentieth as far as the last, so four leave nothing over.
float barFrameWidth(Vec3 bottomMid, Vec3 down, float aspect) {
    float perMetre = STAND_IN_WIDTH_M / standInBarDistance(aspect);
    float width = perMetre * sqrtf(vecDot(bottomMid, bottomMid));
    for (int i = 0; i < 4; i++) {
        float drop = BAR_DROP_FRAC * width;
        Vec3 bar = { bottomMid.x + down.x * drop, bottomMid.y + down.y * drop,
                     bottomMid.z + down.z * drop };
        width = perMetre * sqrtf(vecDot(bar, bar));
    }
    return width;
}

BarFrame roomBarFrame(XrPosef picture, float pictureHeight, float aspect) {
    Vec3 downLocal = { 0.0f, -1.0f, 0.0f };
    Vec3 down = quatRotate(picture.orientation, downLocal);
    float half = pictureHeight * 0.5f;
    Vec3 bottomMid = { picture.position.x + down.x * half, picture.position.y + down.y * half,
                       picture.position.z + down.z * half };

    BarFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = barFrameWidth(bottomMid, down, aspect);
    frame.height = frame.width * aspect;
    // Up from the picture's bottom edge by half its own height, in the
    // picture's plane, so the two bottom edges are one
    float up = frame.height * 0.5f;
    frame.pose.orientation = picture.orientation;
    frame.pose.position.x = bottomMid.x - down.x * up;
    frame.pose.position.y = bottomMid.y - down.y * up;
    frame.pose.position.z = bottomMid.z - down.z * up;
    return frame;
}

// Out from the middle in the order the buttons are added, the pill between
// the first pair: the picker and the cog, then the exit and keyboard
// buttons, then gamepad mode and the ray, then head aim and the 3D, and on
// the right past the 3D the Host sheet's
void barSlotPlacement(int slot, float width, float height, Vec3* outLocal, float* outSide) {
    static const int OUT[BAR_SLOTS] = { 0, 0, 1, 1, 2, 3, 2, 3, 4 };
    static const int LEFT[BAR_SLOTS] = { 1, 0, 0, 1, 1, 1, 0, 0, 0 };
    if (slot < 0 || slot >= BAR_SLOTS) {
        slot = BAR_SLOT_ENV;
    }
    float side = width * (slot == BAR_SLOT_ENV ? ENV_BUTTON_FRAC : COG_BUTTON_FRAC);
    float gap = width * ENV_GAP_FRAC;
    float x = width * BAR_WIDTH_FRAC * 0.5f + gap + side * 0.5f + OUT[slot] * (side + gap);
    outLocal->x = LEFT[slot] ? -x : x;
    outLocal->y = -(height * 0.5f + width * BAR_DROP_FRAC);
    outLocal->z = 0.005f;
    *outSide = side;
}

int barSlotHit(int slot, float u, float v, float width, float height) {
    Vec3 local;
    float side;
    barSlotPlacement(slot, width, height, &local, &side);
    float cu = 0.5f + local.x / width;
    float cv = 0.5f - local.y / height;
    float halfU = side * HOVER_MARGIN * 0.5f / width;
    float halfV = side * HOVER_MARGIN * 0.5f / height;
    return fabsf(u - cu) < halfU && fabsf(v - cv) < halfV;
}

_Static_assert(BTN_CELLS <= BTN_ATLAS_COLS * BTN_ATLAS_ROWS, "a cell for every button face");

int buttonCellOrigin(int cell, int* outX, int* outY) {
    if (cell < 0 || cell >= BTN_CELLS) {
        *outX = 0;
        *outY = 0;
        return 0;
    }
    *outX = (cell % BTN_ATLAS_COLS) * BUTTON_TEX;
    *outY = (cell / BTN_ATLAS_COLS) * BUTTON_TEX;
    return 1;
}

int cog3dRowLive(int stereoOk, int source, int row) {
    if (row == COG_ROW3D_SOURCE) {
        return 1;
    }
    if (!stereoOk) {
        return 0;
    }
    if (source != STEREO_SOURCE_DEPTH) {
        // A pair the PC drew has no depth model behind it: the preset, the
        // depth track and the five depth-model rows (Edge, Depth detail,
        // Depth shape, Motion reset, Eye balance) have nothing to set
        return row == COG_ROW3D_CONVERGENCE || row == COG_ROW3D_SWITCH;
    }
    return 1;
}

int sbsEyeHalf(int eye, int swap) {
    int e = eye == 1 ? 1 : 0;
    return swap ? 1 - e : e;
}

void sbsPictureSize(int source, int full, int frameW, int frameH, int* w, int* h) {
    *w = frameW;
    *h = frameH;
    if (!full) {
        return;
    }
    if (source == STEREO_SOURCE_TB) {
        *h = frameH / 2 > 0 ? frameH / 2 : 1;
    }
    else {
        *w = frameW / 2 > 0 ? frameW / 2 : 1;
    }
}

void sbsPointerUv(int source, float* u, float* v) {
    if (source == STEREO_SOURCE_TB) {
        *v *= 0.5f;
    }
    else {
        *u *= 0.5f;
    }
}

void sbsHalfRect(int source, int half, int frameW, int frameH, int rect[4]) {
    int h = half == 1 ? 1 : 0;
    if (source == STEREO_SOURCE_TB) {
        int eh = frameH / 2;
        rect[0] = 0;
        rect[1] = h * eh;
        rect[2] = frameW;
        rect[3] = eh;
    }
    else {
        int ew = frameW / 2;
        rect[0] = h * ew;
        rect[1] = 0;
        rect[2] = ew;
        rect[3] = frameH;
    }
}

int ambiBarDetectApplies(int detectSetting, int stereoMode) {
    return detectSetting && stereoMode != DEPTH_MODE_SBS;
}

int barStereoCell(int source, int on) {
    if (!on) {
        return BTN_CELL_STEREO;
    }
    return source == STEREO_SOURCE_SBS ? BTN_CELL_STEREO_SBS
            : source == STEREO_SOURCE_TB ? BTN_CELL_STEREO_TB : BTN_CELL_STEREO + 1;
}

int buttonCellPut(unsigned char* atlas, int cell, const unsigned char* face) {
    int x, y;
    if (atlas == NULL || face == NULL || !buttonCellOrigin(cell, &x, &y)) {
        return 0;
    }
    const size_t row = (size_t)BUTTON_TEX * 4;
    for (int r = 0; r < BUTTON_TEX; r++) {
        unsigned char* dst = atlas + ((size_t)(y + BUTTON_TEX - 1 - r) * BTN_ATLAS_W + x) * 4;
        memcpy(dst, face + row * r, row);
    }
    return 1;
}

// Snapped to whole units, so the thumb shows exactly what gets written when
// the drag ends, and never off either end
int laneUnits(float t, int min, int max) {
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    return min + (int)roundf(t * (float)(max - min));
}

float lanePlace(int units, int min, int max) {
    if (max <= min) {
        return 0.0f;
    }
    float t = (float)(units - min) / (float)(max - min);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// The readout is where the thumb sits rather than the value itself: a
// brightness running past what the room was baked at reading over a hundred
// percent is more confusing than useful
int lanePercent(int units, int min, int max) {
    return (int)roundf(lanePlace(units, min, max) * 100.0f);
}

// Rounded rather than cut, so a value that went through a float comes back
// to the unit it started as. Not held to the track: the debug property can
// ask for more than the track shows.
int separationUnits(float separation) {
    return (int)roundf(separation * 1000.0f);
}

float separationOf(int units) {
    return units * 0.001f;
}

int bgDimClamp(int percent) {
    int steps = (percent + BG_DIM_STEP / 2) / BG_DIM_STEP;
    if (percent < 0) {
        steps = 0;
    }
    int units = steps * BG_DIM_STEP;
    return units < BG_DIM_MIN ? BG_DIM_MIN : (units > BG_DIM_MAX ? BG_DIM_MAX : units);
}

float bgDimKeep(int percent) {
    int p = percent < BG_DIM_MIN ? BG_DIM_MIN : (percent > BG_DIM_MAX ? BG_DIM_MAX : percent);
    return 1.0f - (float)p / 100.0f;
}

float glowFeatherFraction(int mode) {
    return mode == GLOW_FEATHER_SOFT ? GLOW_FEATHER_SOFT_FRAC
            : mode == GLOW_FEATHER_WIDE ? GLOW_FEATHER_WIDE_FRAC : 0.0f;
}

float srgbToLinear(float c) {
    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

int backdropFor(int mode, int dimPercent, const float voidColor[3], float out[4]) {
    float keep = bgDimKeep(dimPercent);
    if (mode == BACKDROP_PASSTHROUGH) {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f - keep;
        return 1;
    }
    if (mode == BACKDROP_VOID) {
        for (int i = 0; i < 3; i++) {
            float c = voidColor != NULL ? voidColor[i] : 0.0f;
            out[i] = (c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c)) * keep;
        }
        out[3] = 1.0f;
        return out[0] > 0.0f || out[1] > 0.0f || out[2] > 0.0f;
    }
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    return 0;
}

float eyeBalanceOf(int percent) {
    int p = percent < EYE_BALANCE_MIN ? EYE_BALANCE_MIN : (percent > EYE_BALANCE_MAX ? EYE_BALANCE_MAX : percent);
    return (float)p / 100.0f;
}

void eyeShares(float balance, float* left, float* right) {
    float b = balance < -1.0f ? -1.0f : (balance > 1.0f ? 1.0f : balance);
    *left = 1.0f + b;
    *right = 1.0f - b;
}

float eyeShareReach(float balance) {
    float b = balance < 0.0f ? -balance : balance;
    return 1.0f + (b > 1.0f ? 1.0f : b);
}

int cogPresetAt(int units, const int presets[COG_PRESET_CELLS]) {
    static const int order[COG_PRESET_CELLS] = {
        COG_PRESET_BALANCED, COG_PRESET_COMFORT, COG_PRESET_STRONG
    };
    for (int i = 0; i < COG_PRESET_CELLS; i++) {
        if (presets[order[i]] == units) {
            return order[i];
        }
    }
    return -1;
}

int roomScreenClamp(int percent, int resizable) {
    if (!resizable || percent > ROOM_SCREEN_MAX) {
        return ROOM_SCREEN_MAX;
    }
    return percent < ROOM_SCREEN_MIN ? ROOM_SCREEN_MIN : percent;
}

// Whole percent, so a drag moves in the steps the size row does and what the
// picture shows is what gets written when the hand lets go. A reach at or
// behind the centre is held a little in front of it, the way the free resize
// holds its scale.
int roomResizePercent(int startPercent, float startReach, float reach) {
    if (startReach < 0.05f) {
        startReach = 0.05f;
    }
    if (reach < 0.05f) {
        reach = 0.05f;
    }
    float wanted = (float)startPercent * reach / startReach;
    if (wanted < (float)ROOM_SCREEN_MIN) {
        return ROOM_SCREEN_MIN;
    }
    if (wanted > (float)ROOM_SCREEN_MAX) {
        return ROOM_SCREEN_MAX;
    }
    return (int)roundf(wanted);
}

float roomCornerSide(float distance) {
    return CORNER_FRAC * STAND_IN_WIDTH_M * distance / STAND_IN_DISTANCE_M;
}

float cogRowV(int tab, int row) {
    if (tab == COG_TAB_DISPLAY) {
        return COG_DISPLAY_ROW_V0 + row * COG_DISPLAY_ROW_STEP;
    }
    if (tab == COG_TAB_SCREEN) {
        return COG_SCREEN_ROW_V0 + row * COG_SCREEN_ROW_STEP;
    }
    if (tab == COG_TAB_3D) {
        return COG_3D_ROW_V0 + row * COG_3D_ROW_STEP;
    }
    return COG_ROW_V0 + row * COG_ROW_STEP;
}

float cogRowHalf(int tab) {
    return tab == COG_TAB_DISPLAY ? COG_DISPLAY_ROW_HALF
            : tab == COG_TAB_SCREEN ? COG_SCREEN_ROW_HALF
            : tab == COG_TAB_3D ? COG_3D_ROW_HALF : COG_ROW_HALF;
}

float cogCellHalf(int tab) {
    return tab == COG_TAB_DISPLAY ? COG_DISPLAY_CELL_HALF
            : tab == COG_TAB_SCREEN ? COG_SCREEN_CELL_HALF
            : tab == COG_TAB_3D ? COG_3D_CELL_HALF : COG_CELL_HALF;
}

// The screen and 3D tabs' rows sit closer than a full size thumb, so their
// thumbs are a little smaller and two at the same place on neighbouring rows
// stay apart. The display tab has only the one.
float cogThumbSize(int tab) {
    return tab == COG_TAB_SCREEN || tab == COG_TAB_3D ? 0.06f : 0.085f;
}

int cogTrackPart(float pu) {
    if (pu >= COG_TRACK_L - COG_CHEVRON_REACH && pu <= COG_TRACK_L + COG_CHEVRON_W) {
        return TRACK_PART_DOWN;
    }
    if (pu >= COG_TRACK_R - COG_CHEVRON_W && pu <= COG_TRACK_R + COG_CHEVRON_REACH) {
        return TRACK_PART_UP;
    }
    if (pu > COG_TRACK_L + COG_CHEVRON_W && pu < COG_TRACK_R - COG_CHEVRON_W) {
        return TRACK_PART_RUN;
    }
    return TRACK_PART_NONE;
}

float cogRunPlace(float pu) {
    float t = (pu - COG_RUN_L) / (COG_RUN_R - COG_RUN_L);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

float cogRunU(float t) {
    return COG_RUN_L + t * (COG_RUN_R - COG_RUN_L);
}

// A thousandth of a step either way is still that step, since a place read
// back from a pose or a float is rarely exactly on one
int cogStepIndex(float t, int steps, int dir) {
    if (steps <= 0) {
        return 0;
    }
    float at = t * (float)steps;
    int step = dir > 0 ? (int)floorf(at + 0.001f) + 1 : (int)ceilf(at - 0.001f) - 1;
    return step < 0 ? 0 : (step > steps ? steps : step);
}

int cogTrackSteps(int tab, int row) {
    if (tab == COG_TAB_SCREEN) {
        // Tenths of a metre out, five centimetres up, two and a half degrees
        // of tilt and six of roll, which both step clear of the snap to level
        // either side of it, a twentieth of the curve, tenths of a metre wide,
        // then head aim's two in their own whole units. The rows after those
        // are cells and have no steps.
        static const int SCREEN_STEPS[COG_SCREEN_TRACK_ROWS] = {
            78, 80, 32, 30, 20, 72,
            HEAD_AIM_SENSITIVITY_MAX - HEAD_AIM_SENSITIVITY_MIN,
            HEAD_AIM_DEADZONE_MAX - HEAD_AIM_DEADZONE_MIN
        };
        return row >= 0 && row < COG_SCREEN_TRACK_ROWS ? SCREEN_STEPS[row] : 0;
    }
    if (tab == COG_TAB_DISPLAY) {
        // The glow level's five percent steps
        return row == COG_DISPLAY_SLIDER_ROW ? 20 : 0;
    }
    if (tab == COG_TAB_3D) {
        // The tenths of a percent the depth is kept in, whole percent of
        // convergence, then the depth model's five in the steps they are
        // stored in (COG_EDGE_STEP and the others)
        switch (row) {
        case COG_ROW3D_SEPARATION: return COG_SEP_STEPS;
        case COG_ROW3D_CONVERGENCE: return 100;
        case COG_ROW3D_EDGE: return COG_EDGE_STEPS;
        case COG_ROW3D_DETAIL:
        case COG_ROW3D_SHAPE: return COG_DEPTH_STEPS;
        case COG_ROW3D_MOTION: return COG_MOTION_STEPS;
        case COG_ROW3D_BALANCE: return COG_BALANCE_STEPS;
        default: return 0;
        }
    }
    if (tab == COG_TAB_ABOUT) {
        // No rows at all, only the button
        return 0;
    }
    if (tab == COG_TAB_PICTURE) {
        // Whole units, the ones each value is kept in and its readout shows
        // then Background's five percent steps and Glow edge's three cells
        static const int PICTURE_STEPS[PICTURE_TAB_ROWS] = {
            PICTURE_BRIGHTNESS_MAX - PICTURE_BRIGHTNESS_MIN,
            PICTURE_CONTRAST_MAX - PICTURE_CONTRAST_MIN,
            PICTURE_GAMMA_MAX - PICTURE_GAMMA_MIN,
            PICTURE_SATURATION_MAX - PICTURE_SATURATION_MIN,
            (BG_DIM_MAX - BG_DIM_MIN) / BG_DIM_STEP,
            GLOW_FEATHER_COUNT - 1
        };
        return row >= 0 && row < PICTURE_TAB_ROWS ? PICTURE_STEPS[row] : 0;
    }
    // The Room tab's lanes in five unit steps, and the size in whole percent
    if (row == COG_ROOM_ROW_BRIGHTNESS) {
        return (ROOM_BRIGHTNESS_MAX - ROOM_BRIGHTNESS_MIN) / 5;
    }
    if (row == COG_ROOM_ROW_LIGHT_LEVEL) {
        return (ROOM_LIGHT_MAX - ROOM_LIGHT_MIN) / 5;
    }
    if (row == COG_ROOM_ROW_SIZE) {
        return ROOM_SCREEN_MAX - ROOM_SCREEN_MIN;
    }
    return 0;
}

int cogReportButtonAt(float pu, float pv) {
    return pu >= COG_REPORT_L && pu <= COG_REPORT_R && pv >= COG_REPORT_T && pv <= COG_REPORT_B;
}

int cogKofiButtonAt(float pu, float pv) {
    return pu >= COG_KOFI_L && pu <= COG_KOFI_R && pv >= COG_KOFI_T && pv <= COG_KOFI_B;
}

// The note field across the sheet, then the three buttons side by side under it. Everything else on it is words.
int reportZone(float u, float v) {
    if (v >= REPORT_BTN_T && v <= REPORT_BTN_B) {
        if (u >= REPORT_CANCEL_L && u <= REPORT_CANCEL_R) {
            return REPORT_ZONE_CANCEL;
        }
        if (u >= REPORT_SEND_L && u <= REPORT_SEND_R) {
            return REPORT_ZONE_SEND;
        }
        if (u >= REPORT_ZIP_L && u <= REPORT_ZIP_R) {
            return REPORT_ZONE_ZIP;
        }
        return REPORT_ZONE_NONE;
    }
    if (u < REPORT_FIELD_L || u > REPORT_FIELD_R) {
        return REPORT_ZONE_NONE;
    }
    if (v >= REPORT_NOTE_T && v <= REPORT_NOTE_B) {
        return REPORT_ZONE_NOTE;
    }
    return REPORT_ZONE_NONE;
}

// The two buttons along the bottom. The code and the words are nothing.
int kofiSheetZone(float u, float v) {
    if (v < KOFI_BTN_T || v > KOFI_BTN_B) {
        return KOFI_ZONE_NONE;
    }
    if (u >= KOFI_CLOSE_L && u <= KOFI_CLOSE_R) {
        return KOFI_ZONE_CLOSE;
    }
    return u >= KOFI_OPEN_L && u <= KOFI_OPEN_R ? KOFI_ZONE_OPEN : KOFI_ZONE_NONE;
}

// The two buttons side by side along the bottom. The words over them are
// nothing.
int handHintZone(float u, float v) {
    if (v < HINT_BTN_T || v > HINT_BTN_B) {
        return HINT_ZONE_NONE;
    }
    if (u >= HINT_OK_L && u <= HINT_OK_R) {
        return HINT_ZONE_OK;
    }
    if (u >= HINT_NEVER_L && u <= HINT_NEVER_R) {
        return HINT_ZONE_NEVER;
    }
    return HINT_ZONE_NONE;
}

// ---- screen presets, bringing the screen here, lazy follow ----------------------------

// Desk is an arm's length or so with a monitor's worth of width, Couch a
// living room screen, Cinema the far wall of a theatre
LayoutPreset layoutPresetOf(int preset) {
    static const LayoutPreset PRESETS[LAYOUT_PRESETS] = {
        { 120, 160 }, { 250, 260 }, { 400, 500 }
    };
    if (preset < 0 || preset >= LAYOUT_PRESETS) {
        preset = LAYOUT_PRESET_COUCH;
    }
    return PRESETS[preset];
}

int layoutPresetAt(int distanceCm, int widthCm) {
    for (int i = 0; i < LAYOUT_PRESETS; i++) {
        LayoutPreset p = layoutPresetOf(i);
        if (abs(distanceCm - p.distanceCm) <= 2 && abs(widthCm - p.widthCm) <= 2) {
            return i;
        }
    }
    return -1;
}

Vec3 layoutGazeDir(XrQuaternionf head) {
    Vec3 forward = { 0.0f, 0.0f, -1.0f };
    Vec3 g = quatRotate(head, forward);
    float len = sqrtf(vecDot(g, g));
    if (len < 1e-4f) {
        return forward;
    }
    g.x /= len;
    g.y /= len;
    g.z /= len;
    float limit = sinf(LAYOUT_GAZE_PITCH_MAX_DEG * (float)M_PI / 180.0f);
    if (fabsf(g.y) > limit) {
        // Keep the heading, flatten the pitch to the limit
        float flat = sqrtf(g.x * g.x + g.z * g.z);
        float want = sqrtf(1.0f - limit * limit);
        float y = g.y > 0.0f ? limit : -limit;
        if (flat < 1e-4f) {
            g.x = 0.0f;
            g.z = -want;
        }
        else {
            g.x *= want / flat;
            g.z *= want / flat;
        }
        g.y = y;
    }
    return g;
}

XrQuaternionf layoutFaceHead(XrQuaternionf orientation, Vec3 screenPos, Vec3 headPos) {
    Vec3 back = { 0.0f, 0.0f, 1.0f };
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    float hx = headPos.x - screenPos.x;
    float hz = headPos.z - screenPos.z;
    // Directly over or under the head there is no way to face it that is not
    // noise
    if (hx * hx + hz * hz <= 0.0025f) {
        return orientation;
    }
    Vec3 fwd = quatRotate(orientation, back);
    float yaw = atan2f(fwd.x, fwd.z);
    float want = atan2f(hx, hz);
    return quatNorm(quatMul(axisAngleQuat(up, want - yaw), orientation));
}

XrPosef layoutScreenAlong(XrPosef screen, Vec3 headPos, Vec3 dir) {
    Vec3 d = { screen.position.x - headPos.x, screen.position.y - headPos.y,
               screen.position.z - headPos.z };
    float dist = sqrtf(vecDot(d, d));
    // No nearer than a screen can be pushed
    if (dist < 0.3f) {
        dist = 0.3f;
    }
    XrPosef out = screen;
    out.position.x = headPos.x + dir.x * dist;
    out.position.y = headPos.y + dir.y * dist;
    out.position.z = headPos.z + dir.z * dist;
    Vec3 at = { out.position.x, out.position.y, out.position.z };
    out.orientation = layoutFaceHead(screen.orientation, at, headPos);
    return out;
}

XrPosef layoutBringHere(XrPosef screen, XrPosef head) {
    Vec3 hp = { head.position.x, head.position.y, head.position.z };
    return layoutScreenAlong(screen, hp, layoutGazeDir(head.orientation));
}

void followReset(FollowState* st) {
    memset(st, 0, sizeof(*st));
}

static float dirPitch(Vec3 d) {
    float y = d.y < -1.0f ? -1.0f : (d.y > 1.0f ? 1.0f : d.y);
    return asinf(y);
}

static float wrapPi(float a) {
    while (a > (float)M_PI) {
        a -= 2.0f * (float)M_PI;
    }
    while (a <= -(float)M_PI) {
        a += 2.0f * (float)M_PI;
    }
    return a;
}

int followOutsideLeash(Vec3 screenDir, Vec3 gazeDir, float leashRad) {
    float pa = dirPitch(screenDir), pb = dirPitch(gazeDir);
    if (fabsf(pa - pb) > leashRad) {
        return 1;
    }
    // Near straight up or down the heading means nothing
    if (fabsf(pa) < 1.4f && fabsf(pb) < 1.4f) {
        float ya = atan2f(-screenDir.x, -screenDir.z);
        float yb = atan2f(-gazeDir.x, -gazeDir.z);
        if (fabsf(wrapPi(ya - yb)) > leashRad) {
            return 1;
        }
    }
    return 0;
}

// Along the great circle from a to b, t of the way
static Vec3 dirSlerp(Vec3 a, Vec3 b, float t) {
    float dot = vecDot(a, b);
    dot = dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot);
    float omega = acosf(dot);
    Vec3 out;
    if (omega < 1e-4f) {
        out = b;
    }
    else if (omega > (float)M_PI - 1e-3f) {
        // Straight behind: turn about the vertical, the way a head would
        Vec3 up = { 0.0f, 1.0f, 0.0f };
        out = quatRotate(axisAngleQuat(up, t * (float)M_PI), a);
    }
    else {
        float s = sinf(omega);
        float ka = sinf((1.0f - t) * omega) / s;
        float kb = sinf(t * omega) / s;
        out.x = a.x * ka + b.x * kb;
        out.y = a.y * ka + b.y * kb;
        out.z = a.z * ka + b.z * kb;
    }
    return vecNorm(out);
}

Vec3 followStep(FollowState* st, Vec3 screenDir, Vec3 gazeDir, float dt, float leashRad) {
    if (!st->easing) {
        if (!followOutsideLeash(screenDir, gazeDir, leashRad)) {
            return screenDir;
        }
        st->easing = 1;
        st->t = 0.0f;
        st->from = screenDir;
        st->to = gazeDir;
    }
    else {
        // The gaze has moved on since the ease began: chase it from here
        float dot = vecDot(st->to, gazeDir);
        dot = dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot);
        if (acosf(dot) > leashRad * 0.5f) {
            st->from = screenDir;
            st->to = gazeDir;
            st->t = 0.0f;
        }
    }
    if (dt > 0.0f) {
        st->t += dt / FOLLOW_EASE_SEC;
    }
    if (st->t >= 1.0f) {
        st->easing = 0;
        st->t = 0.0f;
        return st->to;
    }
    // Slow out of the start and into the end, so it does not lurch
    float e = st->t * st->t * (3.0f - 2.0f * st->t);
    return dirSlerp(st->from, st->to, e);
}

float layoutAngularWidth(float width, float distFrom, float distNow, float minWidth, float maxWidth) {
    if (distFrom < 0.01f || distNow < 0.01f) {
        return width;
    }
    float w = width * distNow / distFrom;
    return w < minWidth ? minWidth : (w > maxWidth ? maxWidth : w);
}
