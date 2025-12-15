#include "elevation_view.h"
#include <math.h>
#include <stdio.h>
#include <time.h> // For mktime

static Location *current_loc;
static DateTime *current_dt;
static GtkWidget *drawing_area;
static GtkLabel *status_label = NULL;
static TimeSelectedCallback time_callback = NULL;

static int has_selection = 0;
static double selected_ra = 0;
static double selected_dec = 0;

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

    tm2.tm_year = t2.year - 1900; tm2.tm_mon = t2.month - 1; tm2.tm_mday = t2.day;
    tm2.tm_hour = t2.hour; tm2.tm_min = t2.minute; tm2.tm_sec = (int)t2.second;

    double diff = difftime(mktime(&tm1), mktime(&tm2));
    return diff / 3600.0;
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    // Background
    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
    cairo_paint(cr);

    double margin_left = 50;
    double margin_bottom = 30;
    double margin_top = 10;
    double margin_right = 10;

    double graph_w = width - margin_left - margin_right;
    double graph_h = height - margin_top - margin_bottom;

    // Y Scale: -10 to 90
    // y_px = margin_top + (90 - deg) / 100 * graph_h
    #define DEG_TO_Y(deg) (margin_top + (90.0 - (deg)) / 100.0 * graph_h)

    // Axes
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);

    // Y Axis
    cairo_move_to(cr, margin_left, margin_top);
    cairo_line_to(cr, margin_left, height - margin_bottom);
    cairo_stroke(cr);

    // Y Ticks and Labels
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

    // X Axis (Time)
    // Range: Midnight - 8h to Midnight + 8h
    // Midnight of the CURRENT date selected in current_dt (which implies current_dt represents "the night")
    // Wait, main.c sets current_dt to 00:00 of selected date.
    // So "Midnight" is exactly current_dt.

    DateTime center_time = *current_dt;
    center_time.hour = 0; center_time.minute = 0; center_time.second = 0;

    // X Axis Line
    double y_zero = height - margin_bottom;
    cairo_move_to(cr, margin_left, y_zero);
    cairo_line_to(cr, width - margin_right, y_zero);
    cairo_stroke(cr);

    // X Ticks
    // Range -8 to +8
    for (int h = -8; h <= 8; h++) {
        double x = margin_left + (h + 8) / 16.0 * graph_w;
        cairo_move_to(cr, x, y_zero);
        cairo_line_to(cr, x, y_zero + 5);
        cairo_stroke(cr);

        if (h % 2 == 0) {
            // Label
            DateTime t = add_hours(center_time, h);
            char buf[10];
            sprintf(buf, "%02d:00", t.hour);
            cairo_text_extents_t extents;
            cairo_text_extents(cr, buf, &extents);
            cairo_move_to(cr, x - extents.width/2, y_zero + 15);
            cairo_show_text(cr, buf);
        }
    }

    // Current Time Marker (if visible)
    double diff_now = get_hour_diff(*current_dt, center_time);
    if (diff_now >= -8 && diff_now <= 8) {
        double x_now = margin_left + (diff_now + 8) / 16.0 * graph_w;
        cairo_set_source_rgb(cr, 0, 0, 1); // Blue
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, x_now, margin_top);
        cairo_line_to(cr, x_now, height - margin_bottom);
        cairo_stroke(cr);
    }

    cairo_set_line_width(cr, 1.5);

    // Plot Objects
    for (int obj = 0; obj < 3; obj++) {
        // 0: Sun, 1: Moon, 2: Selected
        if (obj == 2 && !has_selection) continue;

        if (obj == 0) cairo_set_source_rgb(cr, 1, 0.8, 0); // Sun Yellow
        else if (obj == 1) cairo_set_source_rgb(cr, 0.5, 0.5, 0.5); // Moon Grey
        else cairo_set_source_rgb(cr, 1, 0, 0); // Selected Red

        int first = 1;
        // Step every 10 mins = 1/6 hour
        for (double h = -8.0; h <= 8.0; h += 0.166666) {
            DateTime t = add_hours(center_time, h);

            double alt = 0, az = 0;
            if (obj == 0) get_sun_position(*current_loc, t, &alt, &az);
            else if (obj == 1) get_moon_position(*current_loc, t, &alt, &az);
            else get_horizontal_coordinates(selected_ra, selected_dec, *current_loc, t, &alt, &az);

            // Clamp or skip? Plotting -10 to 90.

            double x = margin_left + (h + 8.0) / 16.0 * graph_w;
            double y = DEG_TO_Y(alt);

            // Clip visually if out of bounds? Cairo clip rect handles drawing area bounds.
            // But we should break line if jumpy or handle clamping y if needed.
            // Just draw.

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

static void on_motion(GtkEventControllerMotion *controller, double x, double y, gpointer user_data) {
    if (!status_label || !drawing_area) return;

    int width = gtk_widget_get_width(drawing_area);
    int height = gtk_widget_get_height(drawing_area);
    double margin_left = 50;
    double margin_bottom = 30;
    double margin_top = 10;
    double margin_right = 10;
    double graph_w = width - margin_left - margin_right;
    double graph_h = height - margin_top - margin_bottom;

    if (x < margin_left || x > width - margin_right || y < margin_top || y > height - margin_bottom) {
        gtk_label_set_text(status_label, "");
        return;
    }

    // Map X to Hours from center
    double ratio = (x - margin_left) / graph_w;
    double offset_hours = ratio * 16.0 - 8.0;

    DateTime center_time = *current_dt;
    center_time.hour = 0; center_time.minute = 0; center_time.second = 0;
    DateTime t = add_hours(center_time, offset_hours);

    // Map Y to Elevation
    // y = margin_top + (90 - alt)/100 * graph_h
    // y - margin_top = (90 - alt)/100 * graph_h
    // (y - margin_top)/graph_h * 100 = 90 - alt
    // alt = 90 - (y - margin_top)/graph_h * 100

    double alt = 90.0 - (y - margin_top) / graph_h * 100.0;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Time: %02d:%02d, Elevation: %.1f deg", t.hour, t.minute, alt);
    gtk_label_set_text(status_label, buffer);
}

static void on_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    if (!time_callback) return;

    int width = gtk_widget_get_width(drawing_area);
    // int height = gtk_widget_get_height(drawing_area);
    double margin_left = 50;
    double margin_right = 10;
    double graph_w = width - margin_left - margin_right;

    if (x < margin_left || x > width - margin_right) return;

    double ratio = (x - margin_left) / graph_w;
    double offset_hours = ratio * 16.0 - 8.0;

    DateTime center_time = *current_dt;
    center_time.hour = 0; center_time.minute = 0; center_time.second = 0;
    DateTime new_dt = add_hours(center_time, offset_hours);

    time_callback(new_dt);
}

GtkWidget *create_elevation_view(Location *loc, DateTime *dt, GtkLabel *label, TimeSelectedCallback on_time_selected) {
    current_loc = loc;
    current_dt = dt;
    status_label = label;
    time_callback = on_time_selected;

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 400, 200);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), on_draw, NULL, NULL);

    GtkEventController *motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(motion_controller, "motion", G_CALLBACK(on_motion), NULL);
    gtk_widget_add_controller(drawing_area, motion_controller);

    GtkGesture *click_controller = gtk_gesture_click_new();
    g_signal_connect(click_controller, "pressed", G_CALLBACK(on_pressed), NULL);
    gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(click_controller));

    return drawing_area;
}

void elevation_view_set_selected(double ra, double dec) {
    selected_ra = ra;
    selected_dec = dec;
    has_selection = 1;
    elevation_view_redraw();
}

void elevation_view_redraw() {
    if (drawing_area) {
        gtk_widget_queue_draw(drawing_area);
    }
}
