#ifndef NN_GEOM_H
#define NN_GEOM_H

typedef struct {
    int src_w, src_h;                /* frame before crop and rotation      */
    int crop_enable, cx, cy, cw, ch; /* ePTZ crop of this channel           */
    int angle;                       /* 0, 90, 180 or 270                   */
} nn_geom;

/* nx,ny,nw,nh are normalized 0..10000 detection coordinates.
 * Returns 1 on success, 0 when the box is entirely outside the canvas. */
int nn_map_rect(const nn_geom *g, int nx, int ny, int nw, int nh,
                int *ox, int *oy, int *ow, int *oh);

/* Re-anchors an overlay region of size w x h so that it keeps the same corner
 * and margin after a 90/270 rotation.  x/y are in/out. */
void nn_map_anchor(const nn_geom *g, int w, int h, int *x, int *y);

#endif
