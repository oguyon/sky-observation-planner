#include <stdio.h>
#include <time.h>
#include <libnova/julian_day.h>
#include <libnova/rise_set.h>
#include <libnova/solar.h>

int main() {
    // Maunakea ? Try 155.4681 East (Positive) to see if it fixes it.
    // Real Maunakea is 155 W. If libnova uses East+, then -155 is correct.
    // But maybe libnova uses West+?

    // Case 1: -155
    struct ln_lnlat_posn obs1 = {19.8207, -155.4681};
    // Case 2: +155
    struct ln_lnlat_posn obs2 = {19.8207, 155.4681};

    double timezone = -10.0;

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    struct ln_date date;
    date.years = tm->tm_year + 1900;
    date.months = tm->tm_mon + 1;
    date.days = tm->tm_mday;
    date.hours = 0; date.minutes = 0; date.seconds = 0;

    double jd_local = ln_get_julian_day(&date);
    double jd_ut = jd_local - (timezone / 24.0);

    printf("JD Start UT: %f\n", jd_ut);

    struct ln_rst_time rst;

    printf("--- Case -155 ---\n");
    ln_get_solar_rst_horizon(jd_ut, &obs1, -0.833, &rst);
    {
        struct ln_date d; ln_get_date(rst.rise, &d);
        printf("Rise UT: %02d:%02d\n", d.hours, d.minutes);
        ln_get_date(rst.set, &d);
        printf("Set UT: %02d:%02d\n", d.hours, d.minutes);
    }

    printf("--- Case +155 ---\n");
    ln_get_solar_rst_horizon(jd_ut, &obs2, -0.833, &rst);
    {
        struct ln_date d; ln_get_date(rst.rise, &d);
        printf("Rise UT: %02d:%02d\n", d.hours, d.minutes);
        ln_get_date(rst.set, &d);
        printf("Set UT: %02d:%02d\n", d.hours, d.minutes);
    }

    return 0;
}
