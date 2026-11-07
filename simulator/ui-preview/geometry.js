// Native pixels, matched to include/ui_layout.h by the preview contract test.
export const SCREEN = Object.freeze({ width: 480, height: 854 });
export const GRID = Object.freeze({
  margin: 16, headerHeight: 40, headerGap: 10, columns: 4, rows: 6, gap: 8,
});
export const PAGE_RAIL = Object.freeze({ height: 12, activeOpacity: 90, idleOpacity: 20 });
export const GRID_TOP = GRID.margin + GRID.headerHeight + GRID.headerGap;
export const GRID_WIDTH = SCREEN.width - GRID.margin * 2;
export const GRID_HEIGHT = SCREEN.height - GRID_TOP - GRID.margin;
export const COLUMN_PITCH = (GRID_WIDTH + GRID.gap) / GRID.columns;
export const ROW_PITCH = (GRID_HEIGHT + GRID.gap) / GRID.rows;
export const STRIP_PADDING = 10;
export const STRIP_META_WIDTH = COLUMN_PITCH - GRID.gap - STRIP_PADDING;

export function widgetRect({ x = 0, y = 0, w, h }) {
  return { x: GRID.margin + x * COLUMN_PITCH, y: GRID_TOP + y * ROW_PITCH,
    width: w * COLUMN_PITCH - GRID.gap, height: h * ROW_PITCH - GRID.gap };
}

export const geometryStyle = Object.freeze({
  '--screen-margin': `${GRID.margin}px`, '--header-height': `${GRID.headerHeight}px`,
  '--grid-top': `${GRID_TOP}px`, '--grid-width': `${GRID_WIDTH}px`, '--grid-height': `${GRID_HEIGHT}px`,
  '--grid-gap': `${GRID.gap}px`, '--column-pitch': `${COLUMN_PITCH}px`, '--row-pitch': `${ROW_PITCH}px`,
  '--page-rail-height': `${PAGE_RAIL.height}px`, '--page-rail-active-opacity': PAGE_RAIL.activeOpacity / 100,
  '--page-rail-idle-opacity': PAGE_RAIL.idleOpacity / 100,
  '--strip-padding': `${STRIP_PADDING}px`, '--strip-meta-width': `${STRIP_META_WIDTH}px`,
});
