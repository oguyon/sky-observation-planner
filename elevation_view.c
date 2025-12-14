#include "elevation_view.h"
#include <math.h>
#include <stdio.h>

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    AppState *app = (AppState *)data;
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    double w = allocation.width;
    double h = allocation.height;

    // Margins
    double left_m = 50;
    double bottom_m = 30;
    double right_m = 20;
    double top_m = 20;

    double plot_w = w - left_m - right_m;
    double plot_h = h - top_m - bottom_m;

    // Background
    cairo_set_source_rgb(cr, 0.95, 0.95, 0.95);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    // Axes
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);
    // Y Axis
    cairo_move_to(cr, left_m, top_m);
    cairo_line_to(cr, left_m, h - bottom_m);
    // X Axis
    cairo_move_to(cr, left_m, h - bottom_m);
    cairo_line_to(cr, w - right_m, h - bottom_m);
    cairo_stroke(cr);

    // Y Axis Labels (Elevation 0 to 90)
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    for (int alt = 0; alt <= 90; alt += 15) {
        double y = h - bottom_m - (alt / 90.0) * plot_h;
        cairo_move_to(cr, left_m - 5, y);
        cairo_line_to(cr, left_m, y);
        cairo_stroke(cr);

        char buf[8];
        sprintf(buf, "%d", alt);
        cairo_move_to(cr, left_m - 25, y + 3);
        cairo_show_text(cr, buf);
    }

    // X Axis Time Range
    // Center at current time, +/- 12 hours? Or Sunset to Sunrise?
    // Requirement: "full night".
    // Let's take the JD of the current date at 12:00 (Noon) and go to next Noon.
    // Or center around midnight of current date.
    // Let's determine "Midnight" for the current date.

    struct ln_date mid_date = app->date;
    mid_date.hours = 0; mid_date.minutes = 0; mid_date.seconds = 0;
    double JD_midnight = ln_get_julian_day(&mid_date);

    // If current time is > 12:00, the relevant night is the coming one (so midnight is next day).
    // If current time is < 12:00, the relevant night was the previous one.
    // Let's assume the user sets a time and we want to show the night *containing* that time?
    // Or the night starting on that day.
    // Let's just range from 18:00 of Day to 06:00 of Day+1.
    // 12 hours window centered on Midnight.

    // Adjust JD to closest midnight
    // If hours > 12, add 1 day to date, set h=0.
    // Actually simpler: range JD_current - 0.5 to JD_current + 0.5? No that's 24h.
    // Let's do 18:00 previous day to 06:00 next day if currently near midnight.

    double JD_start, JD_end;
    double JD_current = ln_get_julian_day(&app->date);

    // Align JD_current to nearest midnight
    double JD_nearest_midnight = floor(JD_current - 0.5) + 0.5 + 1.0;
    // Wait. JD starts at noon (.0). So .5 is midnight.
    // Example: JD 10.0 (noon). JD 10.5 (midnight). JD 11.0 (noon).
    // If JD is 10.2 (16:48). Nearest midnight is 10.5.
    // If JD is 10.8 (07:12). Nearest midnight is 10.5.

    if (JD_current - (floor(JD_current) + 0.5) > 0.5) {
       // Should not happen as diff is < 0.5?
    }

    // Calculate nearest midnight
    double JD_noon = floor(JD_current);
    if (JD_current - JD_noon >= 0.5) {
        JD_nearest_midnight = JD_noon + 0.5; // Tonight
    } else {
        JD_nearest_midnight = JD_noon - 0.5; // Last night
    }
    // No wait.
    // JD = X.2 (16:48). Nearest midnight is X.5. (Tonight)
    // JD = X.8 (07:12). Nearest midnight is X.5. (Last night? No, that's this morning's night).
    // Let's stick to "Night of this Date".
    // Start: 18:00 Local. End: 06:00 Local.
    // Assuming UTC for now as app->date is struct ln_date (which is UTC in libnova).
    // 18:00 UTC to 06:00 UTC.

    // Let's create a range of +/- 6 hours around 00:00 UTC of the day?
    // Let's assume the user wants to see the night associated with the date.
    // We will span from 18:00 on (day) to 06:00 on (day+1).

    struct ln_date start_date = app->date;
    start_date.hours = 18; start_date.minutes = 0; start_date.seconds = 0;
    JD_start = ln_get_julian_day(&start_date);
    if (app->date.hours < 12) JD_start -= 1.0; // Go back to previous evening if we are in morning

    JD_end = JD_start + 0.5; // +12 hours (0.5 days)

    // Draw X labels (every 2 hours)
    for (double jd = JD_start; jd <= JD_end; jd += 2.0/24.0) {
        double x_fraction = (jd - JD_start) / (JD_end - JD_start);
        double x = left_m + x_fraction * plot_w;

        cairo_move_to(cr, x, h - bottom_m);
        cairo_line_to(cr, x, h - bottom_m + 5);
        cairo_stroke(cr);

        struct ln_date d;
        ln_get_date(jd, &d);
        char buf[8];
        sprintf(buf, "%02d:%02d", d.hours, d.minutes);
        cairo_move_to(cr, x - 15, h - bottom_m + 15);
        cairo_show_text(cr, buf);
    }

    // Plot Functions
    // We step every 10 minutes
    double step = 10.0 / (24.0 * 60.0);

    // 1. Sun (Yellow)
    cairo_set_source_rgb(cr, 1.0, 0.8, 0.0);
    cairo_set_line_width(cr, 2);
    int first = 1;
    for (double jd = JD_start; jd <= JD_end; jd += step) {
        struct ln_hrz_posn sun_pos;
        struct ln_equ_posn sun_equ;
        ln_get_solar_equ_coords(jd, &sun_equ);
        ln_get_hrz_from_equ(&sun_equ, &app->observer_location, jd, &sun_pos);

        double x = left_m + ((jd - JD_start) / (JD_end - JD_start)) * plot_w;
        double y = h - bottom_m - (sun_pos.alt / 90.0) * plot_h;

        // Clamp Y visually? No, just let it clip or go off chart if alt < 0?
        // We usually want to see negative elevation too to see rise/set.
        // But the chart is 0-90. Let's clamp at bottom line.
        if (sun_pos.alt < 0) y = h - bottom_m;

        if (first) {
            cairo_move_to(cr, x, y);
            first = 0;
        } else {
            cairo_line_to(cr, x, y);
        }
    }
    cairo_stroke(cr);

    // 2. Moon (White/Grey)
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    first = 1;
    for (double jd = JD_start; jd <= JD_end; jd += step) {
        struct ln_hrz_posn moon_pos;
        struct ln_equ_posn moon_equ;
        ln_get_lunar_equ_coords(jd, &moon_equ); // Accurate enough
        ln_get_hrz_from_equ(&moon_equ, &app->observer_location, jd, &moon_pos);

        double x = left_m + ((jd - JD_start) / (JD_end - JD_start)) * plot_w;
        double y = h - bottom_m - (moon_pos.alt / 90.0) * plot_h;
        if (moon_pos.alt < 0) y = h - bottom_m;

        if (first) {
            cairo_move_to(cr, x, y);
            first = 0;
        } else {
            cairo_line_to(cr, x, y);
        }
    }
    cairo_stroke(cr);

    // 3. Selection (Red)
    if (app->has_selection) {
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        first = 1;
        for (double jd = JD_start; jd <= JD_end; jd += step) {
            struct ln_hrz_posn sel_pos;
            // Assuming star fixed RA/Dec (good approx for one night)
            ln_get_hrz_from_equ(&app->selection_equ, &app->observer_location, jd, &sel_pos);

            double x = left_m + ((jd - JD_start) / (JD_end - JD_start)) * plot_w;
            double y = h - bottom_m - (sel_pos.alt / 90.0) * plot_h;
            if (sel_pos.alt < 0) y = h - bottom_m;

            if (first) {
                cairo_move_to(cr, x, y);
                first = 0;
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        cairo_stroke(cr);

        // Draw Legend for Selection
        cairo_move_to(cr, left_m + 10, top_m + 15);
        cairo_show_text(cr, app->selection_name);
    }

    // Legend
    cairo_set_font_size(cr, 10);
    cairo_set_source_rgb(cr, 1.0, 0.8, 0.0);
    cairo_move_to(cr, w - right_m - 40, top_m + 10);
    cairo_show_text(cr, "Sun");

    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_move_to(cr, w - right_m - 40, top_m + 25);
    cairo_show_text(cr, "Moon");

    return FALSE;
}

GtkWidget *elevation_view_new(AppState *app) {
    GtkWidget *drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 400, 200);
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(on_draw), app);
    return drawing_area;
}

void elevation_view_redraw(GtkWidget *widget) {
    gtk_widget_queue_draw(widget);
}
