#include "sky_view.h"
#include "catalog.h"
#include <math.h>
#include <stdio.h>

static Location *current_loc;
static DateTime *current_dt;
static SkyViewOptions *current_options;
static GtkWidget *drawing_area;
static void (*click_callback)(double, double) = NULL;
static double cursor_alt = -1;
static double cursor_az = -1;

// Helper to project Alt/Az to X/Y (0-1 range from center)
static void project(double alt, double az, double *x, double *y) {
    double r = 1.0 - alt / 90.0;
    if (r < 0) r = 0;
    double az_rad = az * M_PI / 180.0;
    *x = -r * sin(az_rad);
    *y = -r * cos(az_rad);
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

    // Background
    cairo_set_source_rgb(cr, 0, 0, 0.1); // Dark blue/black
    cairo_paint(cr);

    // Horizon
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Directions
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_move_to(cr, cx, cy - radius); cairo_show_text(cr, "N");
    cairo_move_to(cr, cx, cy + radius); cairo_show_text(cr, "S");
    cairo_move_to(cr, cx - radius, cy); cairo_show_text(cr, "E");
    cairo_move_to(cr, cx + radius, cy); cairo_show_text(cr, "W");

    // Alt/Az Grid
    if (current_options->show_alt_az_grid) {
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.5);
        cairo_set_line_width(cr, 0.5);

        // Alt circles
        for (int alt = 30; alt < 90; alt += 30) {
            double r_alt = 1.0 - alt / 90.0;
            cairo_arc(cr, cx, cy, r_alt * radius, 0, 2 * M_PI);
            cairo_stroke(cr);

            // Label
            char buf[10]; sprintf(buf, "%d", alt);
            cairo_move_to(cr, cx + 2, cy - r_alt * radius + 10);
            cairo_show_text(cr, buf);
        }

        // Az lines
        for (int az = 0; az < 360; az += 45) {
            double x, y;
            project(0, az + 180.0, &x, &y);
            cairo_move_to(cr, cx, cy);
            cairo_line_to(cr, cx + x * radius, cy + y * radius);
            cairo_stroke(cr);
        }
    }

    // RA/Dec Grid (Projected)
    if (current_options->show_ra_dec_grid) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.5, 0.5); // Blue-ish
        cairo_set_line_width(cr, 0.5);

        // Dec circles (approximate by drawing lines between points)
        for (int dec = -60; dec <= 80; dec += 20) {
            int first = 1;
            for (int ra = 0; ra <= 360; ra += 5) {
                double alt, az;
                get_horizontal_coordinates(ra, dec, *current_loc, *current_dt, &alt, &az);
                if (alt >= 0) {
                    double x, y;
                    project(alt, az + 180.0, &x, &y);
                    if (first) { cairo_move_to(cr, cx + x * radius, cy + y * radius); first = 0; }
                    else { cairo_line_to(cr, cx + x * radius, cy + y * radius); }
                } else {
                    first = 1;
                }
            }
            cairo_stroke(cr);

            // Draw label at some visible point?
            // Simple approach: calculate at RA=0 or current sidereal time?
        }

        // RA lines
        for (int ra_h = 0; ra_h < 24; ra_h += 2) {
            int first = 1;
            for (int dec = -90; dec <= 90; dec += 5) {
                double alt, az;
                get_horizontal_coordinates(ra_h * 15.0, dec, *current_loc, *current_dt, &alt, &az);
                if (alt >= 0) {
                    double x, y;
                    project(alt, az + 180.0, &x, &y);
                    if (first) { cairo_move_to(cr, cx + x * radius, cy + y * radius); first = 0; }
                    else { cairo_line_to(cr, cx + x * radius, cy + y * radius); }
                } else {
                    first = 1;
                }
            }
            cairo_stroke(cr);
        }
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

                    if (alt >= 0) {
                        double x, y;
                        project(alt, az + 180.0, &x, &y);

                        center_x += x;
                        center_y += y;
                        count_pts++;

                        if (first) {
                            cairo_move_to(cr, cx + x * radius, cy + y * radius);
                            first = 0;
                        } else {
                            cairo_line_to(cr, cx + x * radius, cy + y * radius);
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
        if (alt >= 0) {
            double x, y;
            project(alt, az + 180.0, &x, &y);
            double size = 2.0 - (stars[i].mag / 3.0);
            if (size < 0.5) size = 0.5;

            cairo_arc(cr, cx + x * radius, cy + y * radius, size, 0, 2 * M_PI);
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
            if (alt >= 0) {
                double x, y;
                project(alt, az + 180.0, &x, &y);
                cairo_set_source_rgb(cr, 1.0, 0.5, 0.5);
                cairo_arc(cr, cx + x * radius, cy + y * radius, 3, 0, 2 * M_PI);
                cairo_fill(cr);
                cairo_move_to(cr, cx + x * radius + 4, cy + y * radius);
                cairo_show_text(cr, p_names[p]);
            }
        }
    }

    // Draw Sun
    double s_alt, s_az;
    get_sun_position(*current_loc, *current_dt, &s_alt, &s_az);
    if (s_alt >= 0) {
        double x, y;
        project(s_alt, s_az + 180.0, &x, &y);
        cairo_set_source_rgb(cr, 1, 1, 0);
        cairo_arc(cr, cx + x * radius, cy + y * radius, 5, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_move_to(cr, cx + x * radius + 6, cy + y * radius);
        cairo_show_text(cr, "Sun");
    }

    // Draw Moon
    double m_alt, m_az;
    double m_ra, m_dec;
    get_moon_position(*current_loc, *current_dt, &m_alt, &m_az);
    get_moon_equ_coords(*current_dt, &m_ra, &m_dec);

    if (m_alt >= 0) {
        double x, y;
        project(m_alt, m_az + 180.0, &x, &y);
        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_arc(cr, cx + x * radius, cy + y * radius, 4, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_move_to(cr, cx + x * radius + 6, cy + y * radius);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_show_text(cr, "Moon");

        // Draw Concentric Circles around Moon
        if (current_options->show_moon_circles) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.3);
            cairo_set_line_width(cr, 1.0);

            // Draw 5, 10, 15, 20 deg circles
            // To be accurate in projection, we should project points.
            // Simple approx: circle in projection? No, heavily distorted.
            // Better: Scan circle around Moon RA/Dec and project.

            for (int r_deg = 5; r_deg <= 20; r_deg += 5) {
                int first_pt = 1;
                for (int ang = 0; ang <= 360; ang += 10) {
                    // This is spherical calc.
                    // Approximate by converting to Alt/Az locally?
                    // Let's use simple angular separation in RA/Dec space if projection allows,
                    // but Alt/Az projection is easier.
                    // Let's compute points at distance R from Moon(Alt, Az).
                    // This requires spherical trig: Given (Alt0, Az0), distance R, bearing Theta -> (Alt1, Az1).

                    double theta = ang * M_PI / 180.0;
                    double delta = r_deg * M_PI / 180.0;
                    double alt0 = m_alt * M_PI / 180.0;
                    double az0 = m_az * M_PI / 180.0; // Libnova Az

                    double sin_alt1 = sin(alt0)*cos(delta) + cos(alt0)*sin(delta)*cos(theta);
                    double alt1 = asin(sin_alt1);
                    double y = sin(delta)*sin(theta);
                    double x_val = cos(delta)*cos(alt0) - sin(alt0)*sin(delta)*cos(theta); // x name conflict
                    double az1 = az0 + atan2(y, x_val);

                    double alt_deg = alt1 * 180.0 / M_PI;
                    double az_deg = az1 * 180.0 / M_PI;

                    if (alt_deg >= 0) {
                        double px, py;
                        project(alt_deg, az_deg + 180.0, &px, &py);
                        if (first_pt) { cairo_move_to(cr, cx + px * radius, cy + py * radius); first_pt = 0; }
                        else { cairo_line_to(cr, cx + px * radius, cy + py * radius); }
                    } else {
                        first_pt = 1;
                    }
                }
                cairo_stroke(cr);
            }
        }
    }

    // Time Info Text Box
    {
        double lst = get_lst(*current_dt, *current_loc);
        while (lst > 24.0) lst -= 24.0;
        char buf[256];
        snprintf(buf, sizeof(buf),
            "UT: %02d:%02d\nLST: %02d:%02d",
            current_dt->hour, current_dt->minute,
            (int)lst, (int)((lst - (int)lst)*60)
        );

        cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
        cairo_rectangle(cr, 10, 10, 100, 40);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, 15, 25);
        cairo_show_text(cr, buf); // Multiline not supported directly by show_text easily without loop, sticking to one line or separate calls

        snprintf(buf, sizeof(buf), "UT: %02d:%02d", current_dt->hour, current_dt->minute);
        cairo_move_to(cr, 15, 25);
        cairo_show_text(cr, buf);

        snprintf(buf, sizeof(buf), "LST: %02d:%02d", (int)lst, (int)((lst - (int)lst)*60));
        cairo_move_to(cr, 15, 40);
        cairo_show_text(cr, buf);
    }

    // Cursor Info (Top Right)
    if (cursor_alt >= 0) {
        struct ln_lnlat_posn observer;
        observer.lat = current_loc->lat;
        observer.lng = current_loc->lon;
        struct ln_hrz_posn hrz = {cursor_alt, cursor_az};
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

        double alt, az;
        unproject(nx, ny, &alt, &az);
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

    unproject(nx, ny, &cursor_alt, &cursor_az);

    // Trigger redraw to update cursor info
    gtk_widget_queue_draw(widget);
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

    return drawing_area;
}

void sky_view_redraw() {
    if (drawing_area) {
        gtk_widget_queue_draw(drawing_area);
    }
}
