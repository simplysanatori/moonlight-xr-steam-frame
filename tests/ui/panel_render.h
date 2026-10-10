// Renders every panel and tab to PNG files. Shared by mlxr-panel-test and the ui_render ctest.
#ifndef PANEL_RENDER_H
#define PANEL_RENDER_H

// Writes the PNGs into dir (created if missing). Returns the number written, or -1 on error.
int render_all_panels(const char* dir);

// A fixed set of states for tests: draws panel id with a view and fills hits; the buffer
// has guard bytes around it that the caller checks. Returns 0 if intact.
#endif
