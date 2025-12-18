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

void sky_view_set_highlighted_target(Target *target) {
    highlighted_target = target;
    sky_view_redraw();
}

void sky_view_reset_view() {
    view_zoom = 1.0;
    view_pan_x = 0.0;
    view_pan_y = 0.0;
    view_rotation = 0.0;
    sky_view_redraw();
}

double sky_view_get_zoom() {
    return view_zoom;
}

// Helper to project Alt/Az to X/Y (0-1 range from center)
static int project(double alt, double az, double *x, double *y) {
    if (alt < 0) return 0;
    double r = 1.0 - alt / 90.0;
    if (r < 0) r = 0;
    double az_rad = az * M_PI / 180.0;
    *x = -r * sin(az_rad);
    *y = -r * cos(az_rad);
    return 1;
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

static void draw_text_centered(cairo_t *cr, double x, double y, const char *text) {
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);
    cairo_move_to(cr, x - extents.width/2 - extents.x_bearing, y - extents.height/2 - extents.y_bearing);
    cairo_show_text(cr, text);
}

// Helper to draw a styled text box (opaque black, white outline)
// Anchored at top-left (x, y) unless right_align is true (then x is right edge)
static void draw_styled_text_box(cairo_t *cr, double x, double y, const char **lines, int count, int right_align) {
    if (count <= 0) return;

    double font_size = 12.0 * (current_options->font_scale > 0 ? current_options->font_scale : 1.0);
    cairo_set_font_size(cr, font_size);

    double max_w = 0;
    double total_h = 0;
    double line_h = font_size * 1.2;
    double padding = 5.0;

    for(int i=0; i<count; i++) {
        cairo_text_extents_t ext;
        cairo_text_extents(cr, lines[i], &ext);
        if (ext.width > max_w) max_w = ext.width;
    }
    total_h = count * line_h;

    double box_w = max_w + 2 * padding;
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
        cairo_move_to(cr, draw_x + padding, draw_y + padding + (i + 1) * line_h - (line_h - font_size)/2); // Approx baseline
        cairo_show_text(cr, lines[i]);
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

// Helpers for Ephemeris formatting
static void format_rst_time(double jd, double timezone, char *buf, size_t len, const char *label) {
    if (jd < 0) {
        snprintf(buf, len, "%s: --:--", label);
    } else {
        // Convert JD to Date
        struct ln_date date;
        ln_get_date(jd, &date);
        // Add Timezone
        double h = date.hours + date.minutes/60.0 + date.seconds/3600.0 + timezone;
        while (h < 0) h += 24.0;
        while (h >= 24.0) h -= 24.0;
        int hh = (int)h;
        int mm = (int)((h - hh) * 60.0);
        snprintf(buf, len, "%s: %02d:%02d", label, hh, mm);
    }
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    double radius = (width < height ? width : height) / 2.0 - 10;
    double cx = width / 2.0;
    double cy = height / 2.0;

    // Calculate effective star settings
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

    // Directions
    struct { char *label; double az; } dirs[] = {{"N", 180}, {"S", 0}, {"E", 270}, {"W", 90}};
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    for (int i=0; i<4; i++) {
        double u, v;
        if (project(0, dirs[i].az + 180, &u, &v)) {
            double tx, ty;
            transform_point(u, v, &tx, &ty);
            draw_text_centered(cr, cx + tx * radius, cy + ty * radius, dirs[i].label);
        }
    }

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
                // If smaller than 1.0, clamp to 1.0 and adjust color/alpha
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

                    // Apply Saturation
                    double sat = current_options->star_saturation;
                    r = 1.0 + (r - 1.0) * sat;
                    g = 1.0 + (g - 1.0) * sat;
                    b = 1.0 + (b - 1.0) * sat;

                    // Clamp
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

        // Visibility Check
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

        char buf_loc[64], buf_ut[64], buf_lst[64];
        snprintf(buf_loc, 64, "Local: %02d:%02d", current_dt->hour, current_dt->minute);
        snprintf(buf_ut, 64, "UT: %02d:%02d", ut_date.hours, ut_date.minutes);
        snprintf(buf_lst, 64, "LST: %02d:%02d", (int)lst, (int)((lst - (int)lst)*60));

        const char *lines[] = {buf_loc, buf_ut, buf_lst};
        draw_styled_text_box(cr, 10, 10, lines, 3, 0);
    }

    // Ephemeris Box
    {
        // Use Noon JD to ensure we get events for the "current local day" (Morning Rise, Evening Set)
        DateTime noon_dt = *current_dt;
        noon_dt.hour = 12; noon_dt.minute = 0; noon_dt.second = 0;
        double jd_noon = get_julian_day(noon_dt);

        // Original JD for phase calculation (current time)
        double jd_now = get_julian_day(*current_dt);

        struct ln_lnlat_posn observer = {current_loc->lat, current_loc->lon};
        struct ln_rst_time rst;

        char buf_sunset[64], buf_tw_start[64], buf_tw_end[64], buf_sunrise[64];
        char buf_mr[64], buf_ms[64], buf_mill[64];

        // Timezone: 0 for UT, current_dt->timezone_offset for Local
        double tz = current_options->ephemeris_use_ut ? 0.0 : current_dt->timezone_offset;

        // Solar
        ln_get_solar_rst_horizon(jd_noon, &observer, -0.833, &rst);
        format_rst_time(rst.set, tz, buf_sunset, 64, "Sunset");
        format_rst_time(rst.rise, tz, buf_sunrise, 64, "Sunrise");

        ln_get_solar_rst_horizon(jd_noon, &observer, -18.0, &rst);
        format_rst_time(rst.set, tz, buf_tw_start, 64, "Astro Tw. Start");
        format_rst_time(rst.rise, tz, buf_tw_end, 64, "Astro Tw. End");

        // Lunar
        ln_get_lunar_rst(jd_noon, &observer, &rst);
        format_rst_time(rst.rise, tz, buf_mr, 64, "Moon Rise");
        format_rst_time(rst.set, tz, buf_ms, 64, "Moon Set");

        double phase = ln_get_lunar_disk(jd_now); // 0..1
        snprintf(buf_mill, 64, "Moon Illum: %.1f%%", phase * 100.0);

        const char *lines[] = {buf_sunset, buf_tw_start, buf_tw_end, buf_sunrise, buf_mr, buf_ms, buf_mill};

        // Draw below Time box. Estimate Time box height.
        // Base size 12 * scale * 1.2 * 3 lines + 10 padding ~ 55 * scale.
        double scale = (current_options->font_scale > 0 ? current_options->font_scale : 1.0);
        double y_offset = 10 + (12.0 * scale * 1.2 * 3 + 10) + 10;

        draw_styled_text_box(cr, 10, y_offset, lines, 7, 0);
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
static double drag_start_rotation = 0;

static void on_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, gpointer user_data) {
    drag_start_pan_y = view_pan_y;
    drag_start_rotation = view_rotation;
}

static void on_drag_update_handler(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
    double radius = (width < height ? width : height) / 2.0 - 10;

    // Drag X -> Rotation (around zenith)
    // Sensitivity: 1 full width = 180 degrees?
    view_rotation = drag_start_rotation + (offset_x / width) * M_PI;

    // Drag Y -> Pan Y (Vertical shift of zenith)
    view_pan_y = drag_start_pan_y + (offset_y / radius);

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
