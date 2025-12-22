#include "elevation_view.h"
#include "target_list.h"
#include <math.h>
#include <stdio.h>
#include <time.h> // For mktime

static Location *current_loc;
static DateTime *current_dt;
static GtkWidget *drawing_area;
static GtkLabel *status_label = NULL;
static TimeSelectedCallback time_callback = NULL;
static ElevationHoverCallback hover_callback = NULL;
static Target *highlighted_target = NULL;

// Helper state to store last motion coordinates for drawing the crosshair/line
static int last_motion_valid = 0;
static double last_motion_x = 0;
static double last_motion_y = 0;
static double last_motion_alt = 0;

void elevation_view_set_highlighted_target(Target *target) {
    highlighted_target = target;
    elevation_view_redraw();
}

// Helper to add hours to a DateTime
static DateTime add_hours(DateTime dt, double hours) {
    struct tm t = {0};
    t.tm_year = dt.year - 1900;
    t.tm_mon = dt.month - 1;
    t.tm_mday = dt.day;
    t.tm_hour = dt.hour;
    t.tm_min = dt.minute;
    t.tm_sec = (int)dt.second;
    t.tm_isdst = -1;

    time_t time_val = mktime(&t);
    time_val += (time_t)(hours * 3600);

    struct tm *new_t = localtime(&time_val);
    DateTime res = dt;
    res.year = new_t->tm_year + 1900;
    res.month = new_t->tm_mon + 1;
    res.day = new_t->tm_mday;
    res.hour = new_t->tm_hour;
    res.minute = new_t->tm_min;
    res.second = new_t->tm_sec;
    return res;
}

static double get_hour_diff(DateTime t1, DateTime t2) {
    struct tm tm1 = {0}, tm2 = {0};
    tm1.tm_year = t1.year - 1900; tm1.tm_mon = t1.month - 1; tm1.tm_mday = t1.day;
    tm1.tm_hour = t1.hour; tm1.tm_min = t1.minute; tm1.tm_sec = (int)t1.second;
    tm1.tm_isdst = -1;

    tm2.tm_year = t2.year - 1900; tm2.tm_mon = t2.month - 1; tm2.tm_mday = t2.day;
    tm2.tm_hour = t2.hour; tm2.tm_min = t2.minute; tm2.tm_sec = (int)t2.second;
    tm2.tm_isdst = -1;

    double diff = difftime(mktime(&tm1), mktime(&tm2));
    return diff / 3600.0;
}

static DateTime get_nearest_midnight(DateTime dt) {
    DateTime midnight = dt;
    midnight.hour = 0;
    midnight.minute = 0;
    midnight.second = 0;

    if (dt.hour >= 12) {
        // Move to next day
        return add_hours(midnight, 24.0);
    }
    return midnight;
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    double margin_left = 50;
    double margin_bottom = 30;
    double margin_top = 20; // Increased for Sunrise/Sunset Labels
    double margin_right = 10;

    double graph_w = width - margin_left - margin_right;
    double graph_h = height - margin_top - margin_bottom;

    // Y Scale: -10 to 90
    #define DEG_TO_Y(deg) (margin_top + (90.0 - (deg)) / 100.0 * graph_h)

    // Determine Center Time (Nearest Midnight)
    DateTime center_time = get_nearest_midnight(*current_dt);

    // Background Gradient (Twilight/Day/Night)
    // Draw vertical stripes for each pixel column
    double x_start = margin_left;
    double x_end = width - margin_right;

    // Scan for Sunrise/Sunset
    double sunrise_x = -1, sunset_x = -1;

    for (double x = x_start; x < x_end; x += 1.0) {
        double ratio = (x - margin_left) / graph_w;
        double offset_hours = ratio * 16.0 - 8.0;
        DateTime t = add_hours(center_time, offset_hours);

        double sun_alt, sun_az;
        get_sun_position(*current_loc, t, &sun_alt, &sun_az);

        double brightness = 0.0;
        if (sun_alt <= -18.0) {
            brightness = 0.1; // Dark
        } else if (sun_alt >= 0.0) {
            brightness = 0.9; // Bright
        } else {
            // Interpolate -18 to 0
            double f = (sun_alt + 18.0) / 18.0;
            brightness = 0.1 + f * 0.8;
        }

        cairo_set_source_rgb(cr, brightness, brightness, brightness);
        cairo_rectangle(cr, x, margin_top, 1.0, graph_h);
        cairo_fill(cr);

        // Detect 0 crossing
        if (x > x_start) {
            double prev_ratio = (x - 1.0 - margin_left) / graph_w;
            double prev_offset = prev_ratio * 16.0 - 8.0;
            DateTime prev_t = add_hours(center_time, prev_offset);
            double prev_sun_alt, temp_az;
            get_sun_position(*current_loc, prev_t, &prev_sun_alt, &temp_az);

            if (prev_sun_alt < 0 && sun_alt >= 0) sunrise_x = x;
            if (prev_sun_alt > 0 && sun_alt <= 0) sunset_x = x;
        }
    }

    // Draw Sunrise/Sunset Lines and Labels
    cairo_set_source_rgb(cr, 1.0, 0.5, 0.0); // Orange
    cairo_set_line_width(cr, 1);
    if (sunrise_x > 0) {
        cairo_move_to(cr, sunrise_x, margin_top);
        cairo_line_to(cr, sunrise_x, height - margin_bottom);
        cairo_stroke(cr);
        cairo_move_to(cr, sunrise_x + 2, margin_top + 10);
        cairo_show_text(cr, "Sunrise");
    }
    if (sunset_x > 0) {
        cairo_move_to(cr, sunset_x, margin_top);
        cairo_line_to(cr, sunset_x, height - margin_bottom);
        cairo_stroke(cr);
        cairo_move_to(cr, sunset_x - 40, margin_top + 10);
        cairo_show_text(cr, "Sunset");
    }

    // Shading for elevation limits
    // Below 0: Transparent Red
    double y_0 = DEG_TO_Y(0);
    double y_min10 = DEG_TO_Y(-10);
    cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 0.3); // Red, 0.3 alpha
    cairo_rectangle(cr, margin_left, y_0, graph_w, y_min10 - y_0);
    cairo_fill(cr);

    // 0 to 20: Gradient Red -> Yellow -> Black
    double y_20 = DEG_TO_Y(20);
    cairo_pattern_t *pat = cairo_pattern_create_linear(0, y_20, 0, y_0);

    cairo_pattern_add_color_stop_rgba(pat, 0.0, 0, 0, 0, 0.0); // Black Transparent
    cairo_pattern_add_color_stop_rgba(pat, 0.25, 1, 1, 0, 0.3); // Transparent Yellow
    cairo_pattern_add_color_stop_rgba(pat, 1.0, 1, 0, 0, 0.3); // Transparent Red at 0

    cairo_set_source(cr, pat);
    cairo_rectangle(cr, margin_left, y_20, graph_w, y_0 - y_20);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    // Axes and Labels
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);

    cairo_move_to(cr, margin_left, margin_top);
    cairo_line_to(cr, margin_left, height - margin_bottom);
    cairo_stroke(cr);

    // Y Ticks
    for (int d = -10; d <= 90; d += 10) {
        double y = DEG_TO_Y(d);
        cairo_move_to(cr, margin_left, y);
        cairo_line_to(cr, margin_left - 5, y);
        cairo_stroke(cr);

        char buf[10];
        sprintf(buf, "%d", d);
        cairo_text_extents_t extents;
        cairo_text_extents(cr, buf, &extents);
        cairo_move_to(cr, margin_left - 8 - extents.width, y + extents.height/2);
        cairo_show_text(cr, buf);
    }

    double y_zero = height - margin_bottom;
    cairo_move_to(cr, margin_left, y_zero);
    cairo_line_to(cr, width - margin_right, y_zero);
    cairo_stroke(cr);

    // X Ticks
    for (int h = -8; h <= 8; h++) {
        double x = margin_left + (h + 8) / 16.0 * graph_w;
        cairo_move_to(cr, x, y_zero);
        cairo_line_to(cr, x, y_zero + 5);
        cairo_stroke(cr);

        if (h % 2 == 0) {
            DateTime t = add_hours(center_time, h);
            char buf[10];
            sprintf(buf, "%02d:00", t.hour);
            cairo_text_extents_t extents;
            cairo_text_extents(cr, buf, &extents);
            cairo_move_to(cr, x - extents.width/2, y_zero + 15);
            cairo_show_text(cr, buf);
        }
    }

    // "Now" Line (Selected Time)
    double diff_now = get_hour_diff(*current_dt, center_time);
    if (diff_now >= -8 && diff_now <= 8) {
        double x_now = margin_left + (diff_now + 8) / 16.0 * graph_w;
        cairo_set_source_rgb(cr, 0, 0, 1); // Blue
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, x_now, margin_top);
        cairo_line_to(cr, x_now, height - margin_bottom);
        cairo_stroke(cr);
    }

    // Cursor Horizontal Line & Label
    if (last_motion_valid) {
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5); // Grey
        cairo_set_line_width(cr, 1.0);
        cairo_set_dash(cr, (double[]){4.0, 4.0}, 2, 0); // Dashed

        cairo_move_to(cr, margin_left, last_motion_y);
        cairo_line_to(cr, width - margin_right, last_motion_y);
        cairo_stroke(cr);

        cairo_set_dash(cr, NULL, 0, 0); // Reset dash

        char elev_buf[16];
        snprintf(elev_buf, 16, "%.1f", last_motion_alt);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // White text
        cairo_move_to(cr, width - margin_right - 30, last_motion_y - 5);
        cairo_show_text(cr, elev_buf);
    }

    cairo_set_line_width(cr, 1.5);

    // Plot Objects (Sun, Moon)
    for (int obj = 0; obj < 2; obj++) {
        if (obj == 0) cairo_set_source_rgb(cr, 1, 0.8, 0); // Sun Yellow
        else cairo_set_source_rgb(cr, 0.8, 0.8, 0.8); // Moon White/Grey

        int first = 1;
        for (double h = -8.0; h <= 8.0; h += 0.166666) {
            DateTime t = add_hours(center_time, h);

            double alt = 0, az = 0;
            if (obj == 0) get_sun_position(*current_loc, t, &alt, &az);
            else get_moon_position(*current_loc, t, &alt, &az);

            double x = margin_left + (h + 8.0) / 16.0 * graph_w;
            double y = DEG_TO_Y(alt);

            if (first) {
                cairo_move_to(cr, x, y);
                first = 0;
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        cairo_stroke(cr);
    }

    // Plot Targets
    int num_lists = target_list_get_list_count();
    for (int l = 0; l < num_lists; l++) {
        TargetList *tl = target_list_get_list_by_index(l);
        if (!target_list_is_visible(tl)) continue;

        int cnt = target_list_get_count(tl);
        for (int i=0; i<cnt; i++) {
            Target *tgt = target_list_get_target(tl, i);

            if (tgt == highlighted_target) {
                cairo_set_source_rgb(cr, 0.0, 1.0, 1.0); // Cyan
                cairo_set_line_width(cr, 3.0);
            } else {
                cairo_set_source_rgb(cr, 1, 0.3, 0.3); // Light Red
                cairo_set_line_width(cr, 1.5);
            }

            int first = 1;
            for (double h = -8.0; h <= 8.0; h += 0.166666) {
                DateTime t = add_hours(center_time, h);
                double alt, az;
                get_horizontal_coordinates(tgt->ra, tgt->dec, *current_loc, t, &alt, &az);

                double x = margin_left + (h + 8.0) / 16.0 * graph_w;
                double y = DEG_TO_Y(alt);

                if (first) {
                    cairo_move_to(cr, x, y);
                    first = 0;
                } else {
                    cairo_line_to(cr, x, y);
                }
            }
            cairo_stroke(cr);
        }
    }
}

static void on_leave(GtkEventControllerMotion *controller, gpointer user_data) {
    if (status_label) gtk_label_set_text(status_label, "");
    last_motion_valid = 0;
    if (hover_callback) {
        DateTime dummy = {0};
        hover_callback(0, dummy, 0);
    }
    elevation_view_redraw();
}

static void on_motion(GtkEventControllerMotion *controller, double x, double y, gpointer user_data) {
    if (!status_label || !drawing_area) return;

    int width = gtk_widget_get_width(drawing_area);
    int height = gtk_widget_get_height(drawing_area);
    double margin_left = 50;
    double margin_bottom = 30;
    double margin_top = 20;
    double margin_right = 10;
    double graph_w = width - margin_left - margin_right;
    double graph_h = height - margin_top - margin_bottom;

    if (x < margin_left || x > width - margin_right || y < margin_top || y > height - margin_bottom) {
        gtk_label_set_text(status_label, "");
        last_motion_valid = 0;
        if (hover_callback) {
            DateTime dummy = {0};
            hover_callback(0, dummy, 0);
        }
        elevation_view_redraw();
        return;
    }

    double ratio = (x - margin_left) / graph_w;
    double offset_hours = ratio * 16.0 - 8.0;

    DateTime center_time = get_nearest_midnight(*current_dt);
    DateTime t = add_hours(center_time, offset_hours);

    double alt = 90.0 - (y - margin_top) / graph_h * 100.0;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Time: %02d:%02d, Elevation: %.1f deg", t.hour, t.minute, alt);
    gtk_label_set_text(status_label, buffer);

    last_motion_valid = 1;
    last_motion_x = x;
    last_motion_y = y;
    last_motion_alt = alt;

    if (hover_callback) {
        hover_callback(1, t, alt);
    }

    elevation_view_redraw();
}

static void update_time_from_x(double x, int width) {
    if (!time_callback) return;
    double margin_left = 50;
    double margin_right = 10;
    double graph_w = width - margin_left - margin_right;

    if (x < margin_left || x > width - margin_right) return;

    double ratio = (x - margin_left) / graph_w;
    double offset_hours = ratio * 16.0 - 8.0;

    DateTime center_time = get_nearest_midnight(*current_dt);
    DateTime new_dt = add_hours(center_time, offset_hours);

    time_callback(new_dt);
}

static void on_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    update_time_from_x(x, gtk_widget_get_width(widget));
}

static void on_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data) {
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    double start_x, start_y;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
    update_time_from_x(start_x + offset_x, gtk_widget_get_width(widget));
}

GtkWidget *create_elevation_view(Location *loc, DateTime *dt, GtkLabel *label, TimeSelectedCallback on_time_selected, ElevationHoverCallback on_hover) {
    current_loc = loc;
    current_dt = dt;
    status_label = label;
    time_callback = on_time_selected;
    hover_callback = on_hover;

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 400, 200);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), on_draw, NULL, NULL);

    GtkEventController *motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(motion_controller, "motion", G_CALLBACK(on_motion), NULL);
    g_signal_connect(motion_controller, "leave", G_CALLBACK(on_leave), NULL);
    gtk_widget_add_controller(drawing_area, motion_controller);

    GtkGesture *click_controller = gtk_gesture_click_new();
    g_signal_connect(click_controller, "pressed", G_CALLBACK(on_pressed), NULL);
    gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(click_controller));

    // Right mouse drag
    GtkGesture *drag_controller = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_controller), 3); // Right button
    g_signal_connect(drag_controller, "drag-update", G_CALLBACK(on_drag_update), NULL);
    gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(drag_controller));

    return drawing_area;
}

void elevation_view_set_selected(double ra, double dec) {
    // Deprecated functionality if we don't have active list context here easily.
    // Or we could pass it in.
    // For now, let's disable adding targets from elevation view, or just redraw.
    elevation_view_redraw();
}

void elevation_view_redraw() {
    if (drawing_area) {
        gtk_widget_queue_draw(drawing_area);
    }
}
