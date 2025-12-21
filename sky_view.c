#include "sky_view.h"
#include "catalog.h"
#include "target_list.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libnova/julian_day.h>
#include <libnova/rise_set.h>
#include <libnova/solar.h>
#include <libnova/lunar.h>
#include <libnova/angular_separation.h>

static Location *current_loc;
static DateTime *current_dt;
static SkyViewOptions *current_options;
static GtkWidget *drawing_area;
static void (*click_callback)(double, double) = NULL;
static double cursor_alt = -1;
static double cursor_az = -1;

// View State
static double view_zoom = 1.0;
static double view_pan_x = 0.0; // Normalized units
static double view_pan_y = 0.0; // Normalized units
static double view_rotation = 0.0; // Radians
static Target *highlighted_target = NULL;

static int use_horizon_projection = 0;
static double horizon_center_az = 180.0; // Start facing South(0) or North(180)? Standard skymap N up -> S down. 180 is North.

void sky_view_toggle_projection() {
    use_horizon_projection = !use_horizon_projection;
    if (use_horizon_projection) {
        view_rotation = 0.0; // Force upright
        // Reset pan Y?
        view_pan_y = 0.0;
    }
    sky_view_redraw();
}

void sky_view_set_highlighted_target(Target *target) {
    highlighted_target = target;
    sky_view_redraw();
}

void sky_view_reset_view() {
    view_zoom = 1.0;
    view_pan_x = 0.0;
    view_pan_y = 0.0;
    view_rotation = 0.0;
    horizon_center_az = 180.0;
    sky_view_redraw();
}

double sky_view_get_zoom() {
    return view_zoom;
}

// Helper to project Alt/Az to X/Y (0-1 range from center)
// North Up, South Down. West Left, East Right.
// Az 0=South, 180=North.
static int project(double alt, double az, double *x, double *y) {
    if (alt < 0) return 0;

    if (use_horizon_projection) {
        // Stereographic projection centered at (Az=horizon_center_az, Alt=0)
        double alt_rad = alt * M_PI / 180.0;
        double az_rad = az * M_PI / 180.0;
        double center_az_rad = horizon_center_az * M_PI / 180.0;
        double d_az = az_rad - center_az_rad;

        // Cartesian on sphere (X towards view center)
        double X = cos(alt_rad) * cos(d_az);
        double Y = cos(alt_rad) * sin(d_az);
        double Z = sin(alt_rad);

        // Project from X=-1 plane to X=0 plane (or standard Stereographic from pole)
        // Standard formula from center (1,0,0) to plane x=0:
        // y' = Y / (1+X), z' = Z / (1+X)
        // Check for point behind viewer
        if (X <= -0.99) return 0; // Singularity at antipode

        double k = 1.0 / (1.0 + X);
        double x_proj = k * Y; // Horizontal
        double y_proj = k * Z; // Vertical (Up)

        // Map to screen coordinates
        // Zenith mode: x is Right, y is Up (actually down in screen coords, handled by -r)
        // Here x_proj is Right (if West is Right?).
        // In Zenith mode: Az 90 (W) -> x=-r (Left). Az 270 (E) -> x=r (Right).
        // Let's match that.
        // If center=180 (N). Az=90(W) -> d_az = -90. sin(-90)=-1. Y=0. X=0. Wait.
        // d_az = 90 - 180 = -90. cos(-90)=0. X=0. Y=-1. x_proj = -1.
        // So W -> Left. Matches.

        *x = x_proj;
        *y = -y_proj; // y_proj is Up, Screen Y is Down.
        return 1;
    } else {
        double r = 1.0 - alt / 90.0;
        if (r < 0) r = 0;
        double az_rad = az * M_PI / 180.0;
        *x = -r * sin(az_rad);
        *y = -r * cos(az_rad);
        return 1;
    }
}

// Apply View Transformation (Rotate -> Scale -> Pan)
static void transform_point(double u, double v, double *tx, double *ty) {
    double u_rot = u * cos(view_rotation) - v * sin(view_rotation);
    double v_rot = u * sin(view_rotation) + v * cos(view_rotation);

    double s_u = u_rot * view_zoom;
    double s_v = v_rot * view_zoom;
    *tx = s_u + view_pan_x;
    *ty = s_v + view_pan_y;
}

static void untransform_point(double tx, double ty, double *u, double *v) {
    double s_u = tx - view_pan_x;
    double s_v = ty - view_pan_y;
    double u_rot = s_u / view_zoom;
    double v_rot = s_v / view_zoom;

    *u = u_rot * cos(-view_rotation) - v_rot * sin(-view_rotation);
    *v = u_rot * sin(-view_rotation) + v_rot * cos(-view_rotation);
}

static void unproject(double x, double y, double *alt, double *az) {
    if (use_horizon_projection) {
        // Inverse Stereographic
        // x = k Y, y = -k Z
        // Y = x/k, Z = -y/k
        // k = 1/(1+X)
        // x = Y/(1+X), -y = Z/(1+X)
        // Let u = x, v = -y.
        // rho^2 = u^2 + v^2.
        // X = (1 - rho^2)/(1 + rho^2)
        // Y = 2u / (1 + rho^2)
        // Z = 2v / (1 + rho^2)

        double u = x;
        double v = -y;
        double rho2 = u*u + v*v;
        double X = (1.0 - rho2) / (1.0 + rho2);
        double Y = 2.0 * u / (1.0 + rho2);
        double Z = 2.0 * v / (1.0 + rho2);

        *alt = asin(Z) * 180.0 / M_PI;
        double d_az = atan2(Y, X) * 180.0 / M_PI;
        *az = horizon_center_az + d_az;

        while (*az < 0) *az += 360.0;
        while (*az >= 360.0) *az -= 360.0;
    } else {
        double r = sqrt(x*x + y*y);
        if (r > 1.0) {
            *alt = -1; // Invalid
            return;
        }
        *alt = 90.0 * (1.0 - r);
        double angle = atan2(-x, -y);
        *az = angle * 180.0 / M_PI;
        if (*az < 0) *az += 360.0;
        *az += 180.0;
        if (*az >= 360.0) *az -= 360.0;
    }
}

static void draw_text_centered(cairo_t *cr, double x, double y, const char *text) {
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);
    cairo_move_to(cr, x - extents.width/2 - extents.x_bearing, y - extents.height/2 - extents.y_bearing);
    cairo_show_text(cr, text);
}

// Helper to draw a styled text box (opaque black, white outline)
static void draw_styled_text_box(cairo_t *cr, double x, double y, const char **lines, int count, int right_align) {
    if (count <= 0) return;

    double font_size = 12.0 * (current_options->font_scale > 0 ? current_options->font_scale : 1.0);
    cairo_set_font_size(cr, font_size);

    double max_w_left = 0;
    double max_w_right = 0;
    double total_h = 0;
    double line_h = font_size * 1.2;
    double padding = 5.0;
    double gap = 10.0;

    // Measure pass
    for(int i=0; i<count; i++) {
        const char *sep = strchr(lines[i], '|');
        if (sep) {
            char left[64], right[64];
            int len_l = sep - lines[i];
            strncpy(left, lines[i], len_l); left[len_l] = '\0';
            strcpy(right, sep + 1);

            cairo_text_extents_t ext_l, ext_r;
            cairo_text_extents(cr, left, &ext_l);
            cairo_text_extents(cr, right, &ext_r);
            if (ext_l.width > max_w_left) max_w_left = ext_l.width;
            if (ext_r.width > max_w_right) max_w_right = ext_r.width;
        } else {
            cairo_text_extents_t ext;
            cairo_text_extents(cr, lines[i], &ext);
            // Treat single lines as covering the full width (contributing to left for simplicity of max_w calc)
            double w = ext.width;
            if (w > max_w_left) max_w_left = w; // Provisional, will adjust total max_w later
        }
    }

    // If we have split lines, the box width needs to accommodate both columns + gap
    double box_w = max_w_left + 2 * padding;
    if (max_w_right > 0) {
        box_w = max_w_left + gap + max_w_right + 2 * padding;
    }

    // Re-check single lines against total box width
    for(int i=0; i<count; i++) {
        if (!strchr(lines[i], '|')) {
            cairo_text_extents_t ext;
            cairo_text_extents(cr, lines[i], &ext);
            if (ext.width + 2*padding > box_w) box_w = ext.width + 2*padding;
        }
    }

    total_h = count * line_h;
    double box_h = total_h + 2 * padding;

    double draw_x = right_align ? (x - box_w) : x;
    double draw_y = y;

    // Fill Black
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_rectangle(cr, draw_x, draw_y, box_w, box_h);
    cairo_fill_preserve(cr);

    // Stroke White
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // Draw Text
    for(int i=0; i<count; i++) {
        double y_pos = draw_y + padding + (i + 1) * line_h - (line_h - font_size)/2;
        const char *sep = strchr(lines[i], '|');

        if (sep) {
            char left[64], right[64];
            int len_l = sep - lines[i];
            strncpy(left, lines[i], len_l); left[len_l] = '\0';
            strcpy(right, sep + 1);

            // Left aligned
            cairo_move_to(cr, draw_x + padding, y_pos);
            cairo_show_text(cr, left);

            // Right aligned (at right edge of box - padding)
            cairo_text_extents_t ext_r;
            cairo_text_extents(cr, right, &ext_r);
            cairo_move_to(cr, draw_x + box_w - padding - ext_r.width, y_pos);
            cairo_show_text(cr, right);
        } else {
            cairo_move_to(cr, draw_x + padding, y_pos);
            cairo_show_text(cr, lines[i]);
        }
    }
}

// Simple B-V to RGB mapping
static void bv_to_rgb(double bv, double *r, double *g, double *b) {
    if (bv < 0.0) { *r = 0.6; *g = 0.6; *b = 1.0; } // Blue
    else if (bv < 0.5) {
        double t = bv / 0.5;
        *r = 0.6 + 0.4*t; *g = 0.6 + 0.4*t; *b = 1.0;
    } else if (bv < 1.0) {
        double t = (bv - 0.5) / 0.5;
        *r = 1.0; *g = 1.0; *b = 1.0 - 0.5*t;
    } else if (bv < 1.5) {
        double t = (bv - 1.0) / 0.5;
        *r = 1.0; *g = 1.0 - 0.4*t; *b = 0.5 - 0.5*t;
    } else {
        *r = 1.0; *g = 0.6; *b = 0.0; // Red
    }
}

static void format_time_only(double jd, double timezone, char *buf, size_t len) {
    if (jd < 0) {
        snprintf(buf, len, "--:--");
    } else {
        struct ln_date date;
        ln_get_date(jd, &date);
        double h = date.hours + date.minutes/60.0 + date.seconds/3600.0 + timezone;
        while (h < 0) h += 24.0;
        while (h >= 24.0) h -= 24.0;
        int hh = (int)h;
        int mm = (int)((h - hh) * 60.0);
        snprintf(buf, len, "%02d:%02d", hh, mm);
    }
}

typedef struct {
    double jd;
    char label[32];
    char time_str[16];
} EphemEvent;

static int compare_ephem_events(const void *a, const void *b) {
    EphemEvent *ea = (EphemEvent *)a;
    EphemEvent *eb = (EphemEvent *)b;
    if (ea->jd < eb->jd) return -1;
    if (ea->jd > eb->jd) return 1;
    return 0;
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    double radius = (width < height ? width : height) / 2.0 - 10;
    double cx = width / 2.0;
    double cy = height / 2.0;

    double effective_limit = current_options->star_mag_limit;
    double effective_m0 = current_options->star_size_m0;
    double effective_ma = current_options->star_size_ma;

    if (current_options->auto_star_settings) {
        effective_limit = 8.0 + view_zoom;
        effective_m0 = 5.5 + 0.3 * sqrt(view_zoom);
        effective_ma = 0.35 + 0.05 * sqrt(view_zoom);
    }

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);

    double h_cx = cx + view_pan_x * radius;
    double h_cy = cy + view_pan_y * radius;
    double h_r = radius * view_zoom;

    cairo_set_source_rgb(cr, 0, 0, 0.1);
    cairo_arc(cr, h_cx, h_cy, h_r, 0, 2 * M_PI);
    cairo_fill_preserve(cr);

    cairo_save(cr);
    cairo_clip(cr);

    // Directions (N=180, S=0)
    struct { char *label; double az; } dirs[] = {
        {"N", 180}, {"NE", 225}, {"E", 270}, {"SE", 315},
        {"S", 0}, {"SW", 45}, {"W", 90}, {"NW", 135}
    };

    // Brighter, Bolder, Bigger for directions
    cairo_save(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    double dir_font_size = 15.0 * (current_options->font_scale > 0 ? current_options->font_scale : 1.0);
    cairo_set_font_size(cr, dir_font_size);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

    for (int i=0; i<8; i++) {
        double u, v;

        // Correct Azimuth for projection
        // Zenith projection is South-Up (Az=0 at Top). Az increases CW?
        // Let's check 'project':
        // x = -r sin(az), y = -r cos(az).
        // Az=0 (S) -> x=0, y=-r (Top). Correct.
        // Az=180 (N) -> x=0, y=r (Bottom).
        // Az=90 (W) -> x=-r, y=0 (Left).
        // Az=270 (E) -> x=r, y=0 (Right).

        // So for Zenith view: S is Top, N is Bottom.
        // If we want "N" label at the North position, we draw at Az=180.
        // If user sees "N" at South position (Top), it means we drew N at Az=0.
        // 'dirs' has N=180.
        // draw_az = 180.
        // project(0, 180) -> x=0, y=r (Bottom).
        // So "N" should appear at Bottom.

        // Wait, if N appears in South direction (Top?), then maybe my previous analysis of 'project' was wrong?
        // Or maybe the coordinate system is flipped?
        // Standard Map: N Up.
        // Astrolabe/Planisphere: often N Up or S Up.
        // If my project puts S at Top.
        // And N label is at Bottom.
        // Then N label is in North direction relative to map center?

        // User says: "'N' appears in the south direction".
        // South direction usually means the direction of the meridian passage (for N hemisphere).
        // If screen Top is South. Then "N" appearing at Top would be wrong.

        // Let's assume user wants Standard Map (N Up).
        // But my projection is S Up.
        // If I want N Up:
        // project should map 180 to Top (y=-r).
        // Current: 180 maps to Bottom (y=r).
        // So Map is S Up.

        // If User sees "N" at "South Direction".
        // Does he mean "N" label is at the S pole of the map?
        // If Map is S Up. S is Top.
        // If "N" is at Top. That is wrong.
        // "N" label (180) -> Bottom.
        // "S" label (0) -> Top.

        // Maybe I should offset by 180 ONLY if NOT horizon view?
        // In previous step I removed offset.
        // Before that, I had `dirs[i].az + 180`.
        // 180 + 180 = 0 (S). So N label was drawn at S (Top).
        // That was the "N in South direction" bug I presumably fixed?

        // Let's try to rotate the labels by 180 degrees relative to current logic?
        // If I use `dirs[i].az + 180`.
        // N(180) -> 360(0) -> Top (South). Wrong.
        // S(0) -> 180 -> Bottom (North). Wrong.

        // If I use `dirs[i].az` (Current).
        // N(180) -> Bottom.
        // S(0) -> Top.
        // This seems consistent with S-Up map.

        // Is it possible "South Direction" means "Bottom of screen"?
        // If user thinks Bottom is South.
        // Then N is at South.
        // But in S-Up map, Bottom is North.

        // Let's assume user wants N to match the actual sky orientation presented.
        // If `project` is correct.
        // Let's check `project` again.
        // `az_rad = az * PI / 180`.
        // `x = -r sin(az)`, `y = -r cos(az)`.
        // cos(0)=1 -> y=-r. (Top).
        // sin(90)=1 -> x=-r. (Left).
        // So Az=0 is Top. Az=90 is Left.
        // West is Left?
        // If looking Up (Zenith), N is behind (Bottom), S is forward (Top).
        // West is to the Right?
        // If I lie down head North, feet South. Looking up.
        // East is Left. West is Right.
        // My map: Az=90 (W) is Left.
        // So W is Left.
        // This is "looking down from outside" or "looking up with mirror"?
        // Sky maps usually: E is Left, W is Right (when N is Up).
        // If S is Up.
        // Invert Left/Right?
        // E (270) -> x=r (Right). W (90) -> x=-r (Left).
        // Standard Sky Map (N Up): E Left, W Right.
        // Rotated 180 (S Up): E Right, W Left.
        // My map: E Right, W Left.
        // So orientation is consistent with S Up.

        // So labels: S should be Top. N Bottom. E Right. W Left.
        // `dirs` has: S=0, N=180, E=270, W=90.
        // With `draw_az = dirs[i].az`:
        // S(0) -> Top.
        // N(180) -> Bottom.
        // E(270) -> Right.
        // W(90) -> Left.

        // This seems correct for S-Up.

        // BUT user says "N appears in South Direction".
        // If user considers "Top" to be North (standard map assumption).
        // Then S(0) is at "North".
        // But user says "N appears in South".
        // So N label is at Top?
        // That happens if `draw_az = 0`.
        // `dirs[N].az = 180`.
        // So I must have `180 + 180`.

        // WAIT. My previous commit *removed* `+180`.
        // "Removed the `+ 180` azimuth offset... fixing the 'N in South direction' issue."
        // User says "The cardinal points *still* appear to be wrong".
        // Maybe I misunderstood which direction was which?

        // Let's stick to standard N-Up map for Zenith?
        // If I change `view_rotation` to `PI` by default?
        // `sky_view_reset_view`: `view_rotation = 0`.

        // Let's allow rotating the labels to match whatever the user sees.
        // If "N" is at Top. And Top is South.
        // I need N to be at Bottom.
        // If "N" is currently at Top.
        // Then `project` mapped 180 to Top?
        // No, `project` maps 0 to Top.
        // So if N is at Top, I was passing 0 for N.
        // `dirs[N].az = 180`.
        // So I was passing `180 + 180 = 360 = 0`.
        // BUT I removed the offset!
        // So I am passing 180.
        // So N should be at Bottom.

        // Is `project` taking `az` or `az+180`?
        // `project` takes `az`.

        // Maybe the grid code adds 180?
        // Grid: `project(alt, az + 180 + 180, ...)`? No.
        // `project(alt, az + 180.0, &u, &v)` in `show_ra_dec_grid` loop?
        // Grid: `project(alt, az + 180.0, ...)`
        // So Grids are rotated by 180?
        // If Grid N (180) is drawn at 180+180=0 (Top).
        // Then the Map is N-Up?
        // If Grid is N-Up.
        // And I draw Labels S-Up.
        // Then N label (Bottom) is on S Grid (Bottom).
        // S label (Top) is on N Grid (Top).
        // So S label is on North Pole?
        // User says "N appears in South".
        // Maybe user means "N label is on the South part of the Sky".

        // Use `draw_az = dirs[i].az + (use_horizon_projection ? 0 : 180);`
        // Restore the 180 offset for Zenith!
        // Because the STAR/GRID projection seems to use `az + 180` internally in the loops?
        // Let's check `sky_view.c` `on_draw` again.
        // `get_horizontal_coordinates... &alt, &az`.
        // `project(alt, az + 180.0, &u, &v)`.
        // YES! The stars and grids are projected with `az + 180`.
        // So I MUST add 180 to the labels too!

        double draw_az = dirs[i].az + (use_horizon_projection ? 0 : 180);

        if (project(0, draw_az, &u, &v)) {
            double tx, ty;
            transform_point(u, v, &tx, &ty);
            draw_text_centered(cr, cx + tx * radius, cy + ty * radius, dirs[i].label);
        }
    }
    cairo_restore(cr);

    // Grids
    if (current_options->show_alt_az_grid) {
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.8);
        cairo_set_line_width(cr, 1.0);
        for (int alt = 30; alt < 90; alt += 30) {
            double r_alt = 1.0 - alt / 90.0;
            double t_r = r_alt * radius * view_zoom;
            cairo_new_path(cr);
            cairo_arc(cr, h_cx, h_cy, t_r, 0, 2 * M_PI);
            cairo_stroke(cr);
            char buf[10]; sprintf(buf, "%d", alt);
            double u, v; project(alt, 180 + 180, &u, &v);
            double tx, ty; transform_point(u, v, &tx, &ty);
            cairo_move_to(cr, cx + tx * radius, cy + ty * radius); cairo_show_text(cr, buf);
        }
        for (int az = 0; az < 360; az += 45) {
            double u, v, tx1, ty1, tx2, ty2;
            project(90, az, &u, &v); transform_point(u, v, &tx1, &ty1);
            project(0, az + 180, &u, &v); transform_point(u, v, &tx2, &ty2);
            cairo_new_path(cr);
            cairo_move_to(cr, cx + tx1 * radius, cy + ty1 * radius);
            cairo_line_to(cr, cx + tx2 * radius, cy + ty2 * radius);
            cairo_stroke(cr);
        }
    }

    if (current_options->show_ra_dec_grid) {
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.8, 0.8);
        cairo_set_line_width(cr, 1.0);
        for (int dec = -60; dec <= 80; dec += 20) {
            int first = 1;
            for (int ra = 0; ra <= 360; ra += 5) {
                double alt, az, u, v, tx, ty;
                get_horizontal_coordinates(ra, dec, *current_loc, *current_dt, &alt, &az);
                if (project(alt, az + 180.0, &u, &v)) {
                    transform_point(u, v, &tx, &ty);
                    if (first) { cairo_move_to(cr, cx + tx * radius, cy + ty * radius); first = 0; }
                    else { cairo_line_to(cr, cx + tx * radius, cy + ty * radius); }
                } else first = 1;
            }
            cairo_stroke(cr);
        }
        for (int ra_h = 0; ra_h < 24; ra_h += 2) {
            int first = 1;
            for (int dec = -90; dec <= 90; dec += 5) {
                double alt, az, u, v, tx, ty;
                get_horizontal_coordinates(ra_h * 15.0, dec, *current_loc, *current_dt, &alt, &az);
                if (project(alt, az + 180.0, &u, &v)) {
                    transform_point(u, v, &tx, &ty);
                    if (first) { cairo_move_to(cr, cx + tx * radius, cy + ty * radius); first = 0; }
                    else { cairo_line_to(cr, cx + tx * radius, cy + ty * radius); }
                } else first = 1;
            }
            cairo_stroke(cr);
        }
    }

    if (current_options->show_ecliptic) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.8);
        cairo_set_line_width(cr, 2.0);
        int first = 1;
        double jd = get_julian_day(*current_dt);
        for (int lon = 0; lon <= 360; lon += 2) {
            struct ln_lnlat_posn ecl = {lon, 0};
            struct ln_equ_posn equ;
            ln_get_equ_from_ecl(&ecl, jd, &equ);
            double alt, az, u, v, tx, ty;
            get_horizontal_coordinates(equ.ra, equ.dec, *current_loc, *current_dt, &alt, &az);
            if (project(alt, az + 180.0, &u, &v)) {
                transform_point(u, v, &tx, &ty);
                if (first) { cairo_move_to(cr, cx + tx * radius, cy + ty * radius); first = 0; }
                else { cairo_line_to(cr, cx + tx * radius, cy + ty * radius); }
            } else first = 1;
        }
        cairo_stroke(cr);
    }

    if (current_options->show_constellation_lines) {
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.8, 0.5);
        cairo_set_line_width(cr, 1.0);
        for (int i = 0; i < num_constellations; i++) {
            double center_x = 0, center_y = 0;
            int count_pts = 0;
            for(int j=0; j<constellations[i].num_lines; j++) {
                int len = constellations[i].line_lengths[j];
                int first = 1;
                for (int k=0; k<len; k++) {
                    double ra = constellations[i].lines[j][k*2];
                    double dec = constellations[i].lines[j][k*2+1];
                    double alt, az, u, v, tx, ty;
                    get_horizontal_coordinates(ra, dec, *current_loc, *current_dt, &alt, &az);
                    if (project(alt, az + 180.0, &u, &v)) {
                        transform_point(u, v, &tx, &ty);
                        center_x += tx; center_y += ty; count_pts++;
                        if (first) { cairo_move_to(cr, cx + tx * radius, cy + ty * radius); first = 0; }
                        else { cairo_line_to(cr, cx + tx * radius, cy + ty * radius); }
                    } else first = 1;
                }
                cairo_stroke(cr);
            }
            if (current_options->show_constellation_names && count_pts > 0) {
                center_x /= count_pts; center_y /= count_pts;
                cairo_set_source_rgba(cr, 0.8, 0.8, 1.0, 0.7);
                draw_text_centered(cr, cx + center_x * radius, cy + center_y * radius, constellations[i].id);
            }
        }
    }

    int stars_total_brighter = 0;
    int stars_visible_in_view = 0;

    if (stars) {
        for (int i = 0; i < num_stars; i++) {
            if (stars[i].mag > effective_limit) continue;
            stars_total_brighter++;

            double alt, az;
            get_horizontal_coordinates(stars[i].ra, stars[i].dec, *current_loc, *current_dt, &alt, &az);
            double u, v;
            if (project(alt, az + 180.0, &u, &v)) {
                double tx, ty;
                transform_point(u, v, &tx, &ty);

                double px = cx + tx * radius;
                double py = cy + ty * radius;

                if (px >= 0 && px <= width && py >= 0 && py <= height) {
                    stars_visible_in_view++;
                }

                double calc_size = (effective_m0 - stars[i].mag) * effective_ma;
                double draw_size = calc_size;
                double brightness = 1.0;

                if (draw_size < 1.0) {
                    brightness = draw_size;
                    if (brightness < 0.1) brightness = 0.1;
                    draw_size = 1.0;
                }

                if (current_options->show_star_colors) {
                    double r, g, b;
                    bv_to_rgb(stars[i].bv, &r, &g, &b);

                    double sat = current_options->star_saturation;
                    r = 1.0 + (r - 1.0) * sat;
                    g = 1.0 + (g - 1.0) * sat;
                    b = 1.0 + (b - 1.0) * sat;

                    if (r < 0) r = 0;
                    if (r > 1) r = 1;
                    if (g < 0) g = 0;
                    if (g > 1) g = 1;
                    if (b < 0) b = 0;
                    if (b > 1) b = 1;

                    cairo_set_source_rgba(cr, r * brightness, g * brightness, b * brightness, 1.0);
                } else {
                    cairo_set_source_rgba(cr, brightness, brightness, brightness, 1.0);
                }

                cairo_new_path(cr);
                cairo_arc(cr, px, py, draw_size, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        }
    }

    if (current_options->show_planets) {
        PlanetID p_ids[] = {PLANET_MERCURY, PLANET_VENUS, PLANET_MARS, PLANET_JUPITER, PLANET_SATURN, PLANET_URANUS, PLANET_NEPTUNE};
        const char *p_names[] = {"Mercury", "Venus", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"};
        for (int p=0; p<7; p++) {
            double alt, az, ra, dec, u, v, tx, ty;
            get_planet_position(p_ids[p], *current_loc, *current_dt, &alt, &az, &ra, &dec);
            if (project(alt, az + 180.0, &u, &v)) {
                transform_point(u, v, &tx, &ty);
                cairo_set_source_rgb(cr, 1.0, 0.5, 0.5);
                cairo_arc(cr, cx + tx * radius, cy + ty * radius, 3, 0, 2 * M_PI);
                cairo_fill(cr);
                cairo_move_to(cr, cx + tx * radius + 4, cy + ty * radius);
                cairo_show_text(cr, p_names[p]);
            }
        }
    }

    int num_lists = target_list_get_list_count();
    for (int l = 0; l < num_lists; l++) {
        TargetList *tl = target_list_get_list_by_index(l);
        if (!target_list_is_visible(tl)) continue;

        int cnt = target_list_get_count(tl);
        for (int i=0; i<cnt; i++) {
            Target *tgt = target_list_get_target(tl, i);
            double alt, az, u, v, tx, ty;
            get_horizontal_coordinates(tgt->ra, tgt->dec, *current_loc, *current_dt, &alt, &az);
            if (project(alt, az + 180.0, &u, &v)) {
                transform_point(u, v, &tx, &ty);

                if (tgt == highlighted_target) {
                    cairo_set_source_rgb(cr, 0.0, 1.0, 1.0); cairo_set_line_width(cr, 3.0);
                } else {
                    cairo_set_source_rgb(cr, 1.0, 0.3, 0.3); cairo_set_line_width(cr, 1.5);
                }

                cairo_new_path(cr);
                cairo_arc(cr, cx + tx * radius, cy + ty * radius, 6, 0, 2 * M_PI);
                cairo_stroke(cr);
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, cx + tx * radius + 8, cy + ty * radius);
                cairo_show_text(cr, tgt->name);
            }
        }
    }

    double s_alt, s_az, u, v, tx, ty;
    get_sun_position(*current_loc, *current_dt, &s_alt, &s_az);
    if (project(s_alt, s_az + 180.0, &u, &v)) {
        transform_point(u, v, &tx, &ty);
        cairo_set_source_rgb(cr, 1, 1, 0);
        cairo_arc(cr, cx + tx * radius, cy + ty * radius, 5, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_move_to(cr, cx + tx * radius + 6, cy + ty * radius);
        cairo_show_text(cr, "Sun");
    }

    double m_alt, m_az, m_ra, m_dec;
    get_moon_position(*current_loc, *current_dt, &m_alt, &m_az);
    get_moon_equ_coords(*current_dt, &m_ra, &m_dec);
    if (project(m_alt, m_az + 180.0, &u, &v)) {
        transform_point(u, v, &tx, &ty);
        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_arc(cr, cx + tx * radius, cy + ty * radius, 4, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_move_to(cr, cx + tx * radius + 6, cy + ty * radius);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_show_text(cr, "Moon");
        if (current_options->show_moon_circles) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.3);
            cairo_set_line_width(cr, 1.0);
            for (int r_deg = 5; r_deg <= 20; r_deg += 5) {
                int first_pt = 1;
                for (int ang = 0; ang <= 360; ang += 10) {
                    double theta = ang * M_PI / 180.0; double delta = r_deg * M_PI / 180.0;
                    double alt0 = m_alt * M_PI / 180.0; double az0 = m_az * M_PI / 180.0;
                    double sin_alt1 = sin(alt0)*cos(delta) + cos(alt0)*sin(delta)*cos(theta);
                    double alt1 = asin(sin_alt1);
                    double y_val = sin(delta)*sin(theta);
                    double x_val = cos(delta)*cos(alt0) - sin(alt0)*sin(delta)*cos(theta);
                    double az1 = az0 + atan2(y_val, x_val);
                    double alt_deg = alt1 * 180.0 / M_PI; double az_deg = az1 * 180.0 / M_PI;
                    double u2, v2, tx2, ty2;
                    if (project(alt_deg, az_deg + 180.0, &u2, &v2)) {
                        transform_point(u2, v2, &tx2, &ty2);
                        if (first_pt) { cairo_move_to(cr, cx + tx2 * radius, cy + ty2 * radius); first_pt = 0; }
                        else { cairo_line_to(cr, cx + tx2 * radius, cy + ty2 * radius); }
                    } else first_pt = 1;
                }
                cairo_stroke(cr);
            }
        }
    }

    {
        double u, v; project(90, 0, &u, &v); double tx, ty; transform_point(u, v, &tx, &ty);
        cairo_set_source_rgb(cr, 1, 1, 0); cairo_arc(cr, cx + tx * radius, cy + ty * radius, 3, 0, 2 * M_PI); cairo_fill(cr);
    }

    cairo_restore(cr);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2); cairo_arc(cr, h_cx, h_cy, h_r, 0, 2 * M_PI); cairo_stroke(cr);

    // Info Boxes
    {
        double lst = get_lst(*current_dt, *current_loc);
        while (lst < 0.0) lst += 24.0;
        while (lst > 24.0) lst -= 24.0;
        double jd_ut = get_julian_day(*current_dt); struct ln_date ut_date; ln_get_date(jd_ut, &ut_date);
        double mjd = jd_ut - 2400000.5;

        char buf_loc[64], buf_ut[64], buf_lst[64], buf_mjd[64];
        char buf_lat[64], buf_lon[64], buf_elev[64];

        snprintf(buf_loc, 64, "Local|%04d-%02d-%02d %02d:%02d:%02d", current_dt->year, current_dt->month, current_dt->day, current_dt->hour, current_dt->minute, (int)current_dt->second);
        snprintf(buf_ut, 64, "UT|%04d-%02d-%02d %02d:%02d:%02d", ut_date.years, ut_date.months, ut_date.days, ut_date.hours, ut_date.minutes, (int)ut_date.seconds);
        snprintf(buf_lst, 64, "LST|%02d:%02d", (int)lst, (int)((lst - (int)lst)*60));
        snprintf(buf_mjd, 64, "MJD|%.5f", mjd);

        snprintf(buf_lat, 64, "Lat|%.4f", current_loc->lat);
        snprintf(buf_lon, 64, "Lon|%.4f", current_loc->lon);
        snprintf(buf_elev, 64, "Elev|%.0fm", current_loc->elevation);

        const char *lines[] = {buf_loc, buf_ut, buf_lst, buf_mjd, buf_lat, buf_lon, buf_elev};
        draw_styled_text_box(cr, 10, 10, lines, 7, 0);
    }

    // Ephemeris Box
    {
        // Use Noon JD to ensure we get events for the "current local day" (Morning Rise, Evening Set)
        DateTime noon_dt = *current_dt;
        noon_dt.hour = 12; noon_dt.minute = 0; noon_dt.second = 0;
        double jd_noon = get_julian_day(noon_dt);

        // Original JD for phase calculation (current time)
        double jd_now = get_julian_day(*current_dt);

        struct ln_lnlat_posn observer = {current_loc->lon, current_loc->lat};
        struct ln_rst_time rst;

        // Elevation correction for horizon (Dip + Refraction adjustment)
        double R = 6378140.0; // Earth Radius in meters
        double h = current_loc->elevation;

        // 1. Horizon Dip
        double dip = 0.0;
        if (h > 0) {
            dip = acos(R / (R + h)) * (180.0 / M_PI);
        }

        // 2. Atmospheric Refraction scaling with Altitude
        // Standard Atmosphere Model
        double T_std = 15.0; // Sea level temp (C)
        double P_std = 1013.25; // Sea level pressure (mbar)

        // Temperature at altitude (Lapse rate 6.5 K/km)
        double T_alt = T_std - 0.0065 * h;
        if (T_alt < -273.15) T_alt = -273.15; // Limit absolute zero

        // Pressure at altitude (Troposphere formula)
        double P_alt = P_std * pow(1.0 - 2.25577e-5 * h, 5.25588);
        if (P_alt < 0) P_alt = 0;

        // Scale standard refraction (0.5667 deg ~ 34 arcmin)
        double ref_scale = (P_alt / P_std) * (288.15 / (273.15 + T_alt));
        double refraction = 0.5667 * ref_scale;

        double semidiameter = 0.2666; // ~16 arcmin

        double horizon = -(semidiameter + refraction + dip);

        char buf_header[64];

        // Timezone: 0 for UT, current_dt->timezone_offset for Local
        double tz = current_options->ephemeris_use_ut ? 0.0 : current_dt->timezone_offset;

        if (current_options->ephemeris_use_ut) {
            snprintf(buf_header, 64, "Ephemeris (UT)");
        } else {
            snprintf(buf_header, 64, "Ephemeris (Local UTC%+.1f)", tz);
        }

        EphemEvent events[6];
        int ev_count = 0;

        // Solar
        ln_get_solar_rst_horizon(jd_noon, &observer, horizon, &rst);

        // Sunset (rst.set)
        events[ev_count].jd = (rst.set > 0) ? rst.set : 999999999.0;
        strcpy(events[ev_count].label, "Sunset");
        format_time_only((rst.set > 0) ? rst.set : -1, tz, events[ev_count].time_str, 16);
        ev_count++;

        // Sunrise (rst.rise)
        events[ev_count].jd = (rst.rise > 0) ? rst.rise : 999999999.0;
        strcpy(events[ev_count].label, "Sunrise");
        format_time_only((rst.rise > 0) ? rst.rise : -1, tz, events[ev_count].time_str, 16);
        ev_count++;

        ln_get_solar_rst_horizon(jd_noon, &observer, -18.0, &rst);

        // Astro Tw. Start (rst.rise)
        events[ev_count].jd = (rst.rise > 0) ? rst.rise : 999999999.0;
        strcpy(events[ev_count].label, "Astro Tw. Start");
        format_time_only((rst.rise > 0) ? rst.rise : -1, tz, events[ev_count].time_str, 16);
        ev_count++;

        // Astro Tw. End (rst.set)
        events[ev_count].jd = (rst.set > 0) ? rst.set : 999999999.0;
        strcpy(events[ev_count].label, "Astro Tw. End");
        format_time_only((rst.set > 0) ? rst.set : -1, tz, events[ev_count].time_str, 16);
        ev_count++;

        // Lunar
        ln_get_lunar_rst(jd_noon, &observer, &rst);

        // Moon Rise (rst.rise)
        events[ev_count].jd = (rst.rise > 0) ? rst.rise : 999999999.0;
        strcpy(events[ev_count].label, "Moon Rise");
        format_time_only((rst.rise > 0) ? rst.rise : -1, tz, events[ev_count].time_str, 16);
        ev_count++;

        // Moon Set (rst.set)
        events[ev_count].jd = (rst.set > 0) ? rst.set : 999999999.0;
        strcpy(events[ev_count].label, "Moon Set");
        format_time_only((rst.set > 0) ? rst.set : -1, tz, events[ev_count].time_str, 16);
        ev_count++;

        qsort(events, ev_count, sizeof(EphemEvent), compare_ephem_events);

        char lines_buf[8][64];
        const char *lines_ptr[8];

        lines_ptr[0] = buf_header;

        for(int i=0; i<ev_count; i++) {
            snprintf(lines_buf[i], 64, "%s|%s", events[i].label, events[i].time_str);
            lines_ptr[i+1] = lines_buf[i];
        }

        double phase = ln_get_lunar_disk(jd_now); // 0..1
        char buf_mill[64];
        snprintf(buf_mill, 64, "Moon Illum|%.1f%%", phase * 100.0);
        lines_ptr[ev_count+1] = buf_mill;

        double scale = (current_options->font_scale > 0 ? current_options->font_scale : 1.0);
        double y_offset = 10 + (12.0 * scale * 1.2 * 6 + 10) + 10;

        draw_styled_text_box(cr, 10, y_offset, lines_ptr, ev_count + 2, 0);
    }

    if (cursor_alt >= 0) {
        struct ln_lnlat_posn observer; observer.lat = current_loc->lat; observer.lng = current_loc->lon;
        struct ln_hrz_posn hrz; hrz.az = cursor_az; hrz.alt = cursor_alt;
        struct ln_equ_posn equ; ln_get_equ_from_hrz(&hrz, &observer, get_julian_day(*current_dt), &equ);
        struct ln_equ_posn sun_equ, moon_equ; double jd = get_julian_day(*current_dt);
        ln_get_solar_equ_coords(jd, &sun_equ); ln_get_lunar_equ_coords(jd, &moon_equ);
        double dist_sun = ln_get_angular_separation(&equ, &sun_equ);
        double dist_moon = ln_get_angular_separation(&equ, &moon_equ);

        char buf_alt[64], buf_az[64], buf_sun[64], buf_moon[64], buf_coords[64];
        snprintf(buf_alt, 64, "Alt: %.1f", cursor_alt);
        snprintf(buf_az, 64, "Az: %.1f", cursor_az);
        snprintf(buf_sun, 64, "Sun Dist: %.1f", dist_sun);
        snprintf(buf_moon, 64, "Moon Dist: %.1f", dist_moon);
        snprintf(buf_coords, 64, "RA:%.2f Dec:%.2f", equ.ra, equ.dec);

        const char *lines[] = {buf_alt, buf_az, buf_sun, buf_moon, buf_coords};
        draw_styled_text_box(cr, width - 10, 10, lines, 5, 1); // Right aligned
    }

    // Star Count Box (After Restore)
    {
        char count_buf[64];
        snprintf(count_buf, 64, "Stars: %d / %d", stars_visible_in_view, stars_total_brighter);
        const char *lines[] = {count_buf};
        draw_styled_text_box(cr, 10, height - 10 - 30, lines, 1, 0); // Simplified position logic
    }

    // Zoom and FOV Info (Bottom Right)
    {
        double fov = 180.0 / view_zoom;
        char info_buf[64];
        snprintf(info_buf, 64, "Zoom: %.2f | FOV: %.1f%s", view_zoom, fov, "\u00B0"); // degree symbol
        const char *lines[] = {info_buf};
        draw_styled_text_box(cr, width - 10, height - 10 - 30, lines, 1, 1);
    }
}

static void on_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    if (click_callback) {
        GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
        int width = gtk_widget_get_width(widget);
        int height = gtk_widget_get_height(widget);

        double radius = (width < height ? width : height) / 2.0 - 10;
        double cx = width / 2.0;
        double cy = height / 2.0;

        double nx = (x - cx) / radius;
        double ny = (y - cy) / radius;

        double u, v;
        untransform_point(nx, ny, &u, &v);

        double alt, az;
        unproject(u, v, &alt, &az);
        if (alt >= 0) {
            click_callback(alt, az);
        }
    }
}

static void on_motion(GtkEventControllerMotion *controller, double x, double y, gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);

    double radius = (width < height ? width : height) / 2.0 - 10;
    double cx = width / 2.0;
    double cy = height / 2.0;

    double nx = (x - cx) / radius;
    double ny = (y - cy) / radius;

    double u, v;
    untransform_point(nx, ny, &u, &v);

    unproject(u, v, &cursor_alt, &cursor_az);

    // Trigger redraw to update cursor info
    gtk_widget_queue_draw(widget);
}

static void on_scroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer user_data) {
    double factor = 1.1;
    if (dy > 0) factor = 1.0 / 1.1;

    view_zoom *= factor;
    // Scale pan_y to keep center fixed relative to sky (since pan_x=0)
    view_pan_y *= factor;

    sky_view_redraw();
}

static double drag_start_pan_y = 0;
// Drag State for precise positioning
static double drag_target_u = 0;
static double drag_target_v = 0;
static double drag_target_dist = 0;
static double drag_target_v_rot_sign = 1.0;

static void on_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, gpointer user_data) {
    if (use_horizon_projection) {
        drag_start_pan_y = horizon_center_az; // Reuse variable for az
        return;
    }

    drag_start_pan_y = view_pan_y;

    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
    double radius = (width < height ? width : height) / 2.0 - 10;
    double cx = width / 2.0;
    double cy = height / 2.0;

    double nx = (start_x - cx) / radius;
    double ny = (start_y - cy) / radius;

    double u, v;
    untransform_point(nx, ny, &u, &v);

    drag_target_u = u;
    drag_target_v = v;
    drag_target_dist = sqrt(u*u + v*v);

    // Calculate initial v_rot state to preserve hemisphere (North vs South of Zenith)
    double s_v = ny - view_pan_y;
    double v_rot = s_v / view_zoom;
    drag_target_v_rot_sign = (v_rot >= 0) ? 1.0 : -1.0;
}

static void on_drag_update_handler(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
    double radius = (width < height ? width : height) / 2.0 - 10;
    double cx = width / 2.0;
    double cy = height / 2.0;

    if (use_horizon_projection) {
        // Horizon mode: Drag X changes Azimuth
        // Scale: Full width corresponds to some FOV.
        // Let's approximate: Radius corresponds to 90 degrees in Stereographic center?
        // In Stereo at center: dx/d_angle = 0.5 (Scale factor k=0.5 at X=1?).
        // Simple linear map for intuitiveness:
        // offset_x pixels.
        // 1 pixel = scale factor.
        // Let's say drag width = 90 degrees.
        double angle_scale = 90.0 / radius / view_zoom; // degrees per pixel

        // We use relative drag, so we need to track delta since last update?
        // GtkGestureDrag gives total offset from start.
        // We need previous offset to calculate delta.
        // Or store initial azimuth at drag start.

        // Static variable to store start az? Or use user_data?
        // Let's use a static for simplicity in this context, assuming single drag.
        // static double drag_start_az = -1; (Unused, removed)

        // Hack: Check if this is the first update call?
        // Better: Use drag_begin to store start az.
        // But drag_begin doesn't know about horizon mode specifics in `drag_start_pan_y`.
        // Let's assume `drag_start_pan_y` can be reused or we add a variable.
        // reusing `drag_start_pan_y` as `drag_start_az` for horizon mode.

        if (offset_x == 0 && offset_y == 0) { // Should be caught in drag_begin, but for safety
             // This logic assumes on_drag_begin sets drag_start_pan_y = horizon_center_az;
        }

        double delta_az = offset_x * angle_scale;
        horizon_center_az = drag_start_pan_y - delta_az; // Drag left (neg offset) -> Increase Az (Rotate camera left -> View right?)
        // Standard: Drag sky. Drag left -> Sky moves left -> Center Az increases?
        // If Center Az increases (South -> West), we look West. Sky moves Right.
        // So Drag Left (-x) -> Sky moves Left -> Center Az Decreases.
        // So minus sign is correct?

        while (horizon_center_az < 0) horizon_center_az += 360.0;
        while (horizon_center_az >= 360.0) horizon_center_az -= 360.0;

        sky_view_redraw();
        return;
    }

    double start_x, start_y;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

    double current_x = start_x + offset_x;
    double current_y = start_y + offset_y;

    // Target normalized screen position
    double tx = (current_x - cx) / radius;
    double ty_req = (current_y - cy) / radius;

    // Avoid singularity at Zenith
    if (drag_target_dist < 0.001) {
        // Just Pan Y
        view_pan_y = ty_req; // v_rot approx 0
        if (view_pan_y > 0) view_pan_y = 0;
        sky_view_redraw();
        return;
    }

    // 1. Solve Rotation (view_rotation) to match Mouse X (tx)
    // We want u_rot = tx / Zoom.
    // u_rot^2 + v_rot^2 = dist^2.
    // Constrain u_rot to valid range [-dist, dist]
    double u_rot_target = tx / view_zoom;
    if (u_rot_target > drag_target_dist) u_rot_target = drag_target_dist;
    if (u_rot_target < -drag_target_dist) u_rot_target = -drag_target_dist;

    double v_rot_mag = sqrt(drag_target_dist * drag_target_dist - u_rot_target * u_rot_target);
    double v_rot_target = v_rot_mag * drag_target_v_rot_sign;

    // We want to rotate (drag_target_u, drag_target_v) to (u_rot_target, v_rot_target).
    // (u, v) rotated by theta is (u', v').
    // theta = angle(u', v') - angle(u, v).
    double angle_target = atan2(v_rot_target, u_rot_target);
    double angle_source = atan2(drag_target_v, drag_target_u);
    // Standard rotation matrix is [cos -sin; sin cos] which rotates CCW.
    // My transform_point uses [cos -sin; sin cos].
    // So 'view_rotation' is the CCW angle.
    // Wait, let's verify transform_point again.
    // u_rot = u cos - v sin.
    // v_rot = u sin + v cos.
    // This rotates (u,v) by angle 'view_rotation' CCW.
    // So theta = view_rotation.
    view_rotation = angle_target - angle_source;

    // 2. Solve Pan Y to match Mouse Y (ty_req)
    // ty = v_rot * Zoom + PanY
    view_pan_y = ty_req - v_rot_target * view_zoom;

    // Constraint: Zenith not below center (pan_y <= 0)
    if (view_pan_y > 0) view_pan_y = 0;

    // Constraint: Pan X is always 0
    view_pan_x = 0;

    sky_view_redraw();
}

GtkWidget *create_sky_view(Location *loc, DateTime *dt, SkyViewOptions *options, void (*on_sky_click)(double, double)) {
    current_loc = loc;
    current_dt = dt;
    current_options = options;
    click_callback = on_sky_click;

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 400, 400);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), on_draw, NULL, NULL);

    GtkGesture *gesture = gtk_gesture_click_new();
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_pressed), NULL);
    gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(gesture));

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), NULL);
    gtk_widget_add_controller(drawing_area, motion);

    // Scroll (Zoom)
    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), NULL);
    gtk_widget_add_controller(drawing_area, scroll);

    // Drag (Pan) - Right Button
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 3);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update_handler), NULL);
    gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(drag));

    return drawing_area;
}

void sky_view_redraw() {
    if (drawing_area) {
        gtk_widget_queue_draw(drawing_area);
    }
}
