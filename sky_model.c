#include "sky_model.h"

double get_julian_day(DateTime dt) {
    struct ln_date date;
    date.years = dt.year;
    date.months = dt.month;
    date.days = dt.day;
    date.hours = dt.hour;
    date.minutes = dt.minute;
    date.seconds = dt.second;
    return ln_get_julian_day(&date) - dt.timezone_offset/24.0;
}

void get_horizontal_coordinates(double ra, double dec, Location loc, DateTime dt, double *alt, double *az) {
    struct ln_lnlat_posn observer;
    observer.lat = loc.lat;
    observer.lng = loc.lon;

    struct ln_equ_posn object;
    object.ra = ra; // degrees
    object.dec = dec; // degrees

    double JD = get_julian_day(dt);

    struct ln_hrz_posn hrz;
    ln_get_hrz_from_equ(&object, &observer, JD, &hrz);

    *alt = hrz.alt;
    *az = hrz.az;
}

void get_sun_position(Location loc, DateTime dt, double *alt, double *az) {
    double JD = get_julian_day(dt);
    struct ln_lnlat_posn observer;
    observer.lat = loc.lat;
    observer.lng = loc.lon;

    struct ln_helio_posn pos;
    ln_get_solar_geom_coords(JD, &pos);
    struct ln_equ_posn equ;
    ln_get_solar_equ_coords(JD, &equ);

    struct ln_hrz_posn hrz;
    ln_get_hrz_from_equ(&equ, &observer, JD, &hrz);

    *alt = hrz.alt;
    *az = hrz.az;
}

void get_moon_position(Location loc, DateTime dt, double *alt, double *az) {
    double JD = get_julian_day(dt);
    struct ln_lnlat_posn observer;
    observer.lat = loc.lat;
    observer.lng = loc.lon;

    struct ln_equ_posn equ;
    ln_get_lunar_equ_coords(JD, &equ);

    struct ln_hrz_posn hrz;
    ln_get_hrz_from_equ(&equ, &observer, JD, &hrz);

    *alt = hrz.alt;
    *az = hrz.az;
}
