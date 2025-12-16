#include "sky_view.h"
#include "catalog.h"
#include "target_list.h"
#include <math.h>
#include <stdio.h>
#include <libnova/julian_day.h>

static Location *current_loc;
static DateTime *current_dt;
static SkyViewOptions *current_options;
static GtkWidget *drawing_area;
static void (*click_callback)(double, double) = NULL;
static double cursor_alt = -1;
static double cursor_az = -1;

// View State
static double view_zoom = 1.0;
static double view_rotation = 0.0; // Radians
static double view_pan_y = 0.0;    // Normalized units

void sky_view_reset_view() {
    view_zoom = 1.0;
    view_rotation = 0.0;
    view_pan_y = 0.0;
    sky_view_redraw();
}

// Helper to project Alt/Az to X/Y (0-1 range from center)
// Returns 1 if valid (above horizon), 0 otherwise
static int project(double alt, double az, double *x, double *y) {
    if (alt < 0) return 0;
    double r = 1.0 - alt / 90.0;
    if (r < 0) r = 0;
    double az_rad = az * M_PI / 180.0;
    *x = -r * sin(az_rad);
    *y = -r * cos(az_rad);
    return 1;
}

// Apply View Transformation (Rotation -> Scale -> Pan)
static void transform_point(double u, double v, double *tx, double *ty) {
    // Rotate
    double r_u = u * cos(view_rotation) - v * sin(view_rotation);
    double r_v = u * sin(view_rotation) + v * cos(view_rotation);

    // Scale
    double s_u = r_u * view_zoom;
    double s_v = r_v * view_zoom;

    // Pan
    *tx = s_u;
    *ty = s_v + view_pan_y;
}

// Inverse transform for click
static void untransform_point(double tx, double ty, double *u, double *v) {
    // Un-Pan
    double s_u = tx;
    double s_v = ty - view_pan_y;

    // Un-Scale
    double r_u = s_u / view_zoom;
    double r_v = s_v / view_zoom;

    // Un-Rotate
    *u = r_u * cos(-view_rotation) - r_v * sin(-view_rotation);
    *v = r_u * sin(-view_rotation) + r_v * cos(-view_rotation);
}

// Inverse projection for click
static void unproject(double x, double y, double *alt, double *az) {
    double r = sqrt(x*x + y*y);
    if (r > 1.0) {
        *alt = -1; // Invalid
        return;
    }
    *alt = 90.0 * (1.0 - r);

    double angle = atan2(-x, -y);
    *az = angle * 180.0 / M_PI;
    if (*az < 0) *az += 360.0;

    // Libnova uses South=0. We rotate projection by 180.
    // So screen Az needs to be rotated back by 180 to match Libnova.
    *az += 180.0;
    if (*az >= 360.0) *az -= 360.0;
}

static void draw_text_centered(cairo_t *cr, double x, double y, const char *text) {
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);
    cairo_move_to(cr, x - extents.width/2 - extents.x_bearing, y - extents.height/2 - extents.y_bearing);
    cairo_show_text(cr, text);
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    double radius = (width < height ? width : height) / 2.0 - 10;
    double cx = width / 2.0;
    double cy = height / 2.0;

    // Background (Fill Widget)
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);

    // Horizon Circle (Transformed)
    // Horizon is unit circle in projection space.
    // Transform center (0,0) -> (0, view_pan_y) scaled by zoom.
    // But rotation doesn't change circle shape.
    // Radius becomes radius * zoom.

    double h_cx = cx + 0;
    double h_cy = cy + view_pan_y * radius;
    double h_r = radius * view_zoom;

    // Sky Background (Dark Blue) inside Horizon
    cairo_set_source_rgb(cr, 0, 0, 0.1);
    cairo_arc(cr, h_cx, h_cy, h_r, 0, 2 * M_PI);
    cairo_fill_preserve(cr); // Fill and keep path for clipping/stroking

    // Clip to Horizon
    cairo_save(cr);
    cairo_clip(cr);

    // Directions (N, S, E, W)
    // Project them (Alt=0, Az=0, 90, 180, 270)
    struct { char *label; double az; } dirs[] = {
        {"N", 180}, {"S", 0}, {"E", 270}, {"W", 90} // Libnova conventions: S=0, W=90, N=180, E=270
    };

    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    for (int i=0; i<4; i++) {
        double u, v;
        if (project(0, dirs[i].az + 180, &u, &v)) { // +180 to correct Libnova->Screen
            double tx, ty;
            transform_point(u, v, &tx, &ty);
            draw_text_centered(cr, cx + tx * radius, cy + ty * radius, dirs[i].label);
        }
    }

    // Alt/Az Grid
    if (current_options->show_alt_az_grid) {
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.8);
        cairo_set_line_width(cr, 1.0);

        // Alt circles
        for (int alt = 30; alt < 90; alt += 30) {
            double r_alt = 1.0 - alt / 90.0;
            double t_r = r_alt * view_zoom;

            cairo_new_path(cr);
            cairo_arc(cr, h_cx, h_cy, t_r, 0, 2 * M_PI);
            cairo_stroke(cr);

            // Label
            char buf[10]; sprintf(buf, "%d", alt);
            double u, v;
            project(alt, 180 + 180, &u, &v);
            double tx, ty;
            transform_point(u, v, &tx, &ty);
            cairo_move_to(cr, cx + tx * radius, cy + ty * radius);
            cairo_show_text(cr, buf);
        }

        // Az lines
        for (int az = 0; az < 360; az += 45) {
            double u, v;
            // Zenith
            project(90, az, &u, &v);
            double tx1, ty1;
            transform_point(u, v, &tx1, &ty1);

            // Horizon
            project(0, az + 180, &u, &v);
            double tx2, ty2;
            transform_point(u, v, &tx2, &ty2);

            cairo_new_path(cr);
            cairo_move_to(cr, cx + tx1 * radius, cy + ty1 * radius);
            cairo_line_to(cr, cx + tx2 * radius, cy + ty2 * radius);
            cairo_stroke(cr);
        }
    }

    // RA/Dec Grid (Projected)
    if (current_options->show_ra_dec_grid) {
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.8, 0.8); // Blue-ish
        cairo_set_line_width(cr, 1.0);

        // Dec circles
        for (int dec = -60; dec <= 80; dec += 20) {
            int first = 1;
            for (int ra = 0; ra <= 360; ra += 5) {
                double alt, az;
                get_horizontal_coordinates(ra, dec, *current_loc, *current_dt, &alt, &az);

                double u, v;
                if (project(alt, az + 180.0, &u, &v)) {
                    double tx, ty;
                    transform_point(u, v, &tx, &ty);
                    if (first) { cairo_move_to(cr, cx + tx * radius, cy + ty * radius); first = 0; }
                    else { cairo_line_to(cr, cx + tx * radius, cy + ty * radius); }
                } else {
                    first = 1;
                }
            }
            cairo_stroke(cr);
        }

        // RA lines
        for (int ra_h = 0; ra_h < 24; ra_h += 2) {
            int first = 1;
            for (int dec = -90; dec <= 90; dec += 5) {
                double alt, az;
                get_horizontal_coordinates(ra_h * 15.0, dec, *current_loc, *current_dt, &alt, &az);

                double u, v;
                if (project(alt, az + 180.0, &u, &v)) {
                    double tx, ty;
                    transform_point(u, v, &tx, &ty);
                    if (first) { cairo_move_to(cr, cx + tx * radius, cy + ty * radius); first = 0; }
                    else { cairo_line_to(cr, cx + tx * radius, cy + ty * radius); }
                } else {
                    first = 1;
                }
            }
            cairo_stroke(cr);
        }
    }

    // Ecliptic
    if (current_options->show_ecliptic) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.8); // Yellow
        cairo_set_line_width(cr, 2.0);

        int first = 1;
        double jd = get_julian_day(*current_dt);
        for (int lon = 0; lon <= 360; lon += 2) {
            struct ln_lnlat_posn ecl = {lon, 0};
            struct ln_equ_posn equ;
            ln_get_equ_from_ecl(&ecl, jd, &equ);

            double alt, az;
            get_horizontal_coordinates(equ.ra, equ.dec, *current_loc, *current_dt, &alt, &az);

            double u, v;
            if (project(alt, az + 180.0, &u, &v)) {
                double tx, ty;
                transform_point(u, v, &tx, &ty);
                if (first) { cairo_move_to(cr, cx + tx * radius, cy + ty * radius); first = 0; }
                else { cairo_line_to(cr, cx + tx * radius, cy + ty * radius); }
            } else {
                first = 1;
            }
        }
        cairo_stroke(cr);
    }

    // Draw Constellations
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
                    double alt, az;
                    get_horizontal_coordinates(ra, dec, *current_loc, *current_dt, &alt, &az);

                    double u, v;
                    if (project(alt, az + 180.0, &u, &v)) {
                        double tx, ty;
                        transform_point(u, v, &tx, &ty);

                        center_x += tx;
                        center_y += ty;
                        count_pts++;

                        if (first) {
                            cairo_move_to(cr, cx + tx * radius, cy + ty * radius);
                            first = 0;
                        } else {
                            cairo_line_to(cr, cx + tx * radius, cy + ty * radius);
                        }
                    } else {
                        first = 1;
                    }
                }
                cairo_stroke(cr);
            }

            // Constellation Names
            if (current_options->show_constellation_names && count_pts > 0) {
                center_x /= count_pts;
                center_y /= count_pts;
                cairo_set_source_rgba(cr, 0.8, 0.8, 1.0, 0.7);
                draw_text_centered(cr, cx + center_x * radius, cy + center_y * radius, constellations[i].id);
            }
        }
    }

    // Draw Stars
    cairo_set_source_rgb(cr, 1, 1, 1);
    for (int i = 0; i < num_stars; i++) {
        double alt, az;
        get_horizontal_coordinates(stars[i].ra, stars[i].dec, *current_loc, *current_dt, &alt, &az);
        double u, v;
        if (project(alt, az + 180.0, &u, &v)) {
            double tx, ty;
            transform_point(u, v, &tx, &ty);
            double size = 2.0 - (stars[i].mag / 3.0);
            if (size < 0.5) size = 0.5;

            cairo_arc(cr, cx + tx * radius, cy + ty * radius, size, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    // Draw Planets
    if (current_options->show_planets) {
        PlanetID p_ids[] = {PLANET_MERCURY, PLANET_VENUS, PLANET_MARS, PLANET_JUPITER, PLANET_SATURN, PLANET_URANUS, PLANET_NEPTUNE};
        const char *p_names[] = {"Mercury", "Venus", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"};

        for (int p=0; p<7; p++) {
            double alt, az, ra, dec;
            get_planet_position(p_ids[p], *current_loc, *current_dt, &alt, &az, &ra, &dec);
            double u, v;
            if (project(alt, az + 180.0, &u, &v)) {
                double tx, ty;
                transform_point(u, v, &tx, &ty);
                cairo_set_source_rgb(cr, 1.0, 0.5, 0.5);
                cairo_arc(cr, cx + tx * radius, cy + ty * radius, 3, 0, 2 * M_PI);
                cairo_fill(cr);
                cairo_move_to(cr, cx + tx * radius + 4, cy + ty * radius);
                cairo_show_text(cr, p_names[p]);
            }
        }
    }

    // Draw Targets
    int cnt = target_list_get_count();
    for (int i=0; i<cnt; i++) {
        Target *tgt = target_list_get(i);
        double alt, az;
        get_horizontal_coordinates(tgt->ra, tgt->dec, *current_loc, *current_dt, &alt, &az);

        double u, v;
        if (project(alt, az + 180.0, &u, &v)) {
            double tx, ty;
            transform_point(u, v, &tx, &ty);

            // Light Red Circle
            cairo_new_path(cr);
            cairo_set_source_rgb(cr, 1.0, 0.3, 0.3);
            cairo_arc(cr, cx + tx * radius, cy + ty * radius, 6, 0, 2 * M_PI);
            cairo_stroke(cr);

            // Label
            cairo_move_to(cr, cx + tx * radius + 8, cy + ty * radius);
            cairo_show_text(cr, tgt->name);
        }
    }

    // Draw Sun
    double s_alt, s_az;
    get_sun_position(*current_loc, *current_dt, &s_alt, &s_az);
    {
        double u, v;
        if (project(s_alt, s_az + 180.0, &u, &v)) {
            double tx, ty;
            transform_point(u, v, &tx, &ty);
            cairo_set_source_rgb(cr, 1, 1, 0);
            cairo_arc(cr, cx + tx * radius, cy + ty * radius, 5, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_move_to(cr, cx + tx * radius + 6, cy + ty * radius);
            cairo_show_text(cr, "Sun");
        }
    }

    // Draw Moon
    double m_alt, m_az;
    double m_ra, m_dec;
    get_moon_position(*current_loc, *current_dt, &m_alt, &m_az);
    get_moon_equ_coords(*current_dt, &m_ra, &m_dec);

    {
        double u, v;
        if (project(m_alt, m_az + 180.0, &u, &v)) {
            double tx, ty;
            transform_point(u, v, &tx, &ty);
            cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
            cairo_arc(cr, cx + tx * radius, cy + ty * radius, 4, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_move_to(cr, cx + tx * radius + 6, cy + ty * radius);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_show_text(cr, "Moon");

            // Draw Concentric Circles around Moon
            if (current_options->show_moon_circles) {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.3);
                cairo_set_line_width(cr, 1.0);

                for (int r_deg = 5; r_deg <= 20; r_deg += 5) {
                    int first_pt = 1;
                    for (int ang = 0; ang <= 360; ang += 10) {
                        double theta = ang * M_PI / 180.0;
                        double delta = r_deg * M_PI / 180.0;
                        double alt0 = m_alt * M_PI / 180.0;
                        double az0 = m_az * M_PI / 180.0;

                        double sin_alt1 = sin(alt0)*cos(delta) + cos(alt0)*sin(delta)*cos(theta);
                        double alt1 = asin(sin_alt1);
                        double y_val = sin(delta)*sin(theta);
                        double x_val = cos(delta)*cos(alt0) - sin(alt0)*sin(delta)*cos(theta);
                        double az1 = az0 + atan2(y_val, x_val);

                        double alt_deg = alt1 * 180.0 / M_PI;
                        double az_deg = az1 * 180.0 / M_PI;

                        double u2, v2;
                        if (project(alt_deg, az_deg + 180.0, &u2, &v2)) {
                            double tx2, ty2;
                            transform_point(u2, v2, &tx2, &ty2);
                            if (first_pt) { cairo_move_to(cr, cx + tx2 * radius, cy + ty2 * radius); first_pt = 0; }
                            else { cairo_line_to(cr, cx + tx2 * radius, cy + ty2 * radius); }
                        } else {
                            first_pt = 1;
                        }
                    }
                    cairo_stroke(cr);
                }
            }
        }
    }

    // Zenith Spot (Yellow)
    {
        double u, v;
        project(90, 0, &u, &v); // Zenith
        double tx, ty;
        transform_point(u, v, &tx, &ty);
        cairo_set_source_rgb(cr, 1, 1, 0); // Yellow
        cairo_arc(cr, cx + tx * radius, cy + ty * radius, 3, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    // Restore clipping
    cairo_restore(cr);

    // Horizon Stroke (Draw again on top of clip)
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_arc(cr, h_cx, h_cy, h_r, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Time Info Text Box (Fixed position, not transformed)
    {
        double lst = get_lst(*current_dt, *current_loc);
        while (lst < 0.0) lst += 24.0;
        while (lst > 24.0) lst -= 24.0;

        double jd_ut = get_julian_day(*current_dt);
        struct ln_date ut_date;
        ln_get_date(jd_ut, &ut_date);

        char buf[256];

        cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
        cairo_rectangle(cr, 10, 10, 120, 55);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);

        snprintf(buf, sizeof(buf), "Local: %02d:%02d", current_dt->hour, current_dt->minute);
        cairo_move_to(cr, 15, 25);
        cairo_show_text(cr, buf);

        snprintf(buf, sizeof(buf), "UT: %02d:%02d", ut_date.hours, ut_date.minutes);
        cairo_move_to(cr, 15, 40);
        cairo_show_text(cr, buf);

        snprintf(buf, sizeof(buf), "LST: %02d:%02d", (int)lst, (int)((lst - (int)lst)*60));
        cairo_move_to(cr, 15, 55);
        cairo_show_text(cr, buf);
    }

    // Cursor Info (Top Right)
    if (cursor_alt >= 0) {
        struct ln_lnlat_posn observer;
        observer.lat = current_loc->lat;
        observer.lng = current_loc->lon;
        struct ln_hrz_posn hrz;
        hrz.az = cursor_az;
        hrz.alt = cursor_alt;

        struct ln_equ_posn equ;

        ln_get_equ_from_hrz(&hrz, &observer, get_julian_day(*current_dt), &equ);

        // Distance to Sun/Moon
        struct ln_equ_posn sun_equ, moon_equ;
        double jd = get_julian_day(*current_dt);
        ln_get_solar_equ_coords(jd, &sun_equ);
        ln_get_lunar_equ_coords(jd, &moon_equ);

        double dist_sun = ln_get_angular_separation(&equ, &sun_equ);
        double dist_moon = ln_get_angular_separation(&equ, &moon_equ);

        char buf[256];
        // Background
        cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
        cairo_rectangle(cr, width - 160, 10, 150, 80);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 1, 1, 1);
        snprintf(buf, sizeof(buf), "Alt: %.1f Az: %.1f", cursor_alt, cursor_az);
        cairo_move_to(cr, width - 155, 25); cairo_show_text(cr, buf);

        snprintf(buf, sizeof(buf), "RA: %.2f Dec: %.2f", equ.ra, equ.dec);
        cairo_move_to(cr, width - 155, 40); cairo_show_text(cr, buf);

        snprintf(buf, sizeof(buf), "Sun Dist: %.1f", dist_sun);
        cairo_move_to(cr, width - 155, 55); cairo_show_text(cr, buf);

        snprintf(buf, sizeof(buf), "Moon Dist: %.1f", dist_moon);
        cairo_move_to(cr, width - 155, 70); cairo_show_text(cr, buf);

        snprintf(buf, sizeof(buf), "S:%.1f,%.1f C:%.1f,%.1f", sun_equ.ra, sun_equ.dec, equ.ra, equ.dec);
        cairo_move_to(cr, width - 155, 85);
        cairo_set_font_size(cr, 10);
        cairo_show_text(cr, buf);
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
    if (dy < 0) {
        view_zoom *= 1.1;
    } else {
        view_zoom /= 1.1;
    }
    sky_view_redraw();
}

static double drag_start_rotation = 0;
static double drag_start_pan_y = 0;

static void on_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, gpointer user_data) {
    drag_start_rotation = view_rotation;
    drag_start_pan_y = view_pan_y;
}

static void on_drag_update_handler(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
    double radius = (width < height ? width : height) / 2.0 - 10;

    // offset_x -> Rotation
    // Drag Left (negative x) -> Rotate Counter Clockwise?
    // Sensitivity: Full width = 180 deg?
    view_rotation = drag_start_rotation + (offset_x / radius);

    // offset_y -> Pan Y
    // Drag Up (negative y) -> Move Zenith Up (negative Pan Y in Cairo coord? No, +Pan Y moves down.)
    // User said: "Drag up will move the Zenith point up".
    // Cairo Y increases downwards. So Drag Up (negative dy) should move Center Up (negative dy).
    view_pan_y = drag_start_pan_y + (offset_y / radius);

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

    // Drag (Rotate/Pan) - Right Button
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
