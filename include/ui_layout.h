#ifndef SCREEN_UI_LAYOUT_H
#define SCREEN_UI_LAYOUT_H

#include "screen.h"

/* Native pixels. Keep simulator/ui-preview/geometry.js in sync; the preview
 * contract test checks the shared geometry. Logical scene cells stay 4 x 6. */
#define UI_MARGIN 16
#define UI_HEADER_HEIGHT 40
#define UI_HEADER_GAP 10
#define UI_GRID_COLUMNS 4
#define UI_GRID_ROWS 6
#define UI_GRID_GAP 8
#define UI_GRID_TOP (UI_MARGIN + UI_HEADER_HEIGHT + UI_HEADER_GAP)
#define UI_GRID_WIDTH (SCREEN_WIDTH - 2 * UI_MARGIN)
#define UI_GRID_HEIGHT (SCREEN_HEIGHT - UI_GRID_TOP - UI_MARGIN)
#define UI_COLUMN_PITCH ((UI_GRID_WIDTH + UI_GRID_GAP) / UI_GRID_COLUMNS)
#define UI_ROW_PITCH ((UI_GRID_HEIGHT + UI_GRID_GAP) / UI_GRID_ROWS)
#define UI_PAGE_RAIL_HEIGHT 12
#define UI_PAGE_RAIL_ACTIVE_OPACITY 90
#define UI_PAGE_RAIL_IDLE_OPACITY 20

#endif
