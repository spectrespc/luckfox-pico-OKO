#pragma once

/* Pure ROI geometry for the focus assistant.
 *
 * The API and the configuration use basis points (0..10000, 10000 = the full
 * frame).  The AF measurement window and the preview use pixels.  Integer
 * arithmetic only: divinus links without -lm and the host tests build with a
 * plain gcc. */

#define FOCUS_BP_FULL     10000
#define FOCUS_BP_MIN_ROI  1000

/* Clamps a basis-point rectangle to 0..10000, enforces the minimum size and
 * keeps x+w / y+h inside the frame. */
void focus_geom_clamp(int *x, int *y, int *w, int *h);

/* Basis points -> pixels.  x/y use floor, w/h are at least 1 pixel. */
void focus_geom_bp_to_px(int bx, int by, int bw, int bh,
                         int imgw, int imgh,
                         int *x, int *y, int *w, int *h);

/* Pixels -> basis points (rounded to nearest, then clamped). */
void focus_geom_px_to_bp(int x, int y, int w, int h,
                         int imgw, int imgh,
                         int *bx, int *by, int *bw, int *bh);

/* Maps a rectangle selected on the preview (/image.jpg, which already applies
 * mirror/flip) back to unmirrored sensor coordinates. */
void focus_geom_preview_to_sensor(int mirror, int flip, int imgw, int imgh,
                                  int *x, int *y, int *w, int *h);