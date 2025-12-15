#include "elevation_view.h"
#include <math.h>
#include <stdio.h>

static Location *current_loc;
static DateTime *current_dt;
static GtkWidget *drawing_area;
static GtkLabel *status_label = NULL;

static int has_selection = 0;
static double selected_ra = 0;
static double selected_dec = 0;

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data) {
    // Background
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_paint(cr);

    // Axes
    double margin = 40;
    double graph_w = width - 2 * margin;
    double graph_h = height - 2 * margin;

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1);

    // Y Axis (Elevation -90 to 90)
    cairo_move_to(cr, margin, margin);
    cairo_line_to(cr, margin, height - margin);
    cairo_stroke(cr);

    // X Axis (Time 18:00 to 07:00)
    cairo_move_to(cr, margin, height / 2.0); // 0 elevation line
    cairo_line_to(cr, width - margin, height / 2.0);
    cairo_stroke(cr);

    // Plot Function
    DateTime start_time = *current_dt;
    start_time.hour = 17;
    start_time.minute = 0;
    start_time.second = 0;

    int duration_hours = 14;

    for (int obj = 0; obj < 3; obj++) {
        // 0: Sun, 1: Moon, 2: Selected
        if (obj == 2 && !has_selection) continue;

        if (obj == 0) cairo_set_source_rgb(cr, 1, 0.8, 0); // Sun Yellow
        else if (obj == 1) cairo_set_source_rgb(cr, 0.5, 0.5, 0.5); // Moon Grey
        else cairo_set_source_rgb(cr, 1, 0, 0); // Selected Red

        int first = 1;
        for (int m = 0; m <= duration_hours * 60; m += 10) {
            DateTime t = start_time;
            // Add minutes
            int total_min = t.minute + m;
            t.hour += total_min / 60;
            t.minute = total_min % 60;
            while (t.hour >= 24) {
                t.hour -= 24;
                t.day += 1;
            }

            double alt = 0, az = 0;
            if (obj == 0) get_sun_position(*current_loc, t, &alt, &az);
            else if (obj == 1) get_moon_position(*current_loc, t, &alt, &az);
            else get_horizontal_coordinates(selected_ra, selected_dec, *current_loc, t, &alt, &az);

            double x = margin + (double)m / (duration_hours * 60) * graph_w;
            // 90 deg is top (margin), -90 is bottom (height - margin)
            // y = center - (alt / 90) * (height/2 - margin)
            // height/2 is 0 deg.
            double y = height/2.0 - (alt / 90.0) * (graph_h / 2.0);

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
    double margin = 40;
    double graph_w = width - 2 * margin;
    double graph_h = height - 2 * margin;

    if (x < margin || x > width - margin || y < margin || y > height - margin) {
        gtk_label_set_text(status_label, "Hover over graph");
        return;
    }

    // Map X to Time
    double time_ratio = (x - margin) / graph_w;
    int duration_hours = 14;
    int total_minutes = (int)(time_ratio * duration_hours * 60);

    int start_hour = 17;
    int hour = start_hour + total_minutes / 60;
    int minute = total_minutes % 60;
    if (hour >= 24) hour -= 24;

    // Map Y to Elevation
    // y = height/2.0 - (alt / 90.0) * (graph_h / 2.0)
    // y - height/2.0 = - (alt / 90.0) * (graph_h / 2.0)
    // (y - height/2.0) / (graph_h / 2.0) = - alt / 90.0
    // alt = -90.0 * (y - height/2.0) / (graph_h / 2.0)

    double alt = -90.0 * (y - height/2.0) / (graph_h / 2.0);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Time: %02d:%02d, Elevation: %.1f deg", hour, minute, alt);
    gtk_label_set_text(status_label, buffer);
}

GtkWidget *create_elevation_view(Location *loc, DateTime *dt, GtkLabel *label) {
    current_loc = loc;
    current_dt = dt;
    status_label = label;
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 400, 200);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), on_draw, NULL, NULL);

    GtkEventController *controller = gtk_event_controller_motion_new();
    g_signal_connect(controller, "motion", G_CALLBACK(on_motion), NULL);
    gtk_widget_add_controller(drawing_area, controller);

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
