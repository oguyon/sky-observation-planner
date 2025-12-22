#include "sky_model.h"
#include <libnova/mercury.h>
#include <libnova/venus.h>
#include <libnova/mars.h>
#include <libnova/jupiter.h>
#include <libnova/saturn.h>
#include <libnova/uranus.h>
#include <libnova/neptune.h>
#include <libnova/sidereal_time.h>
#include <libnova/angular_separation.h>
#include <libnova/lunar.h>

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

void get_equatorial_coordinates(double alt, double az, Location loc, DateTime dt, double *ra, double *dec) {
    struct ln_lnlat_posn observer;
    observer.lat = loc.lat;
    observer.lng = loc.lon;

    struct ln_hrz_posn hrz;
    hrz.az = az;
    hrz.alt = alt;

    double JD = get_julian_day(dt);

    struct ln_equ_posn equ;
    ln_get_equ_from_hrz(&hrz, &observer, JD, &equ);

    if (ra) *ra = equ.ra;
    if (dec) *dec = equ.dec;
}

void get_sun_position(Location loc, DateTime dt, double *alt, double *az) {
    double JD = get_julian_day(dt);
    struct ln_lnlat_posn observer;
    observer.lat = loc.lat;
    observer.lng = loc.lon;

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

void get_planet_position(PlanetID planet, Location loc, DateTime dt, double *alt, double *az, double *ra, double *dec) {
    double JD = get_julian_day(dt);
    struct ln_lnlat_posn observer;
    observer.lat = loc.lat;
    observer.lng = loc.lon;
    struct ln_equ_posn equ = {0, 0};

    switch(planet) {
        case PLANET_MERCURY: ln_get_mercury_equ_coords(JD, &equ); break;
        case PLANET_VENUS:   ln_get_venus_equ_coords(JD, &equ); break;
        case PLANET_MARS:    ln_get_mars_equ_coords(JD, &equ); break;
        case PLANET_JUPITER: ln_get_jupiter_equ_coords(JD, &equ); break;
        case PLANET_SATURN:  ln_get_saturn_equ_coords(JD, &equ); break;
        case PLANET_URANUS:  ln_get_uranus_equ_coords(JD, &equ); break;
        case PLANET_NEPTUNE: ln_get_neptune_equ_coords(JD, &equ); break;
    }

    if (ra) *ra = equ.ra;
    if (dec) *dec = equ.dec;

    struct ln_hrz_posn hrz;
    ln_get_hrz_from_equ(&equ, &observer, JD, &hrz);

    *alt = hrz.alt;
    *az = hrz.az;
}

double get_lst(DateTime dt, Location loc) {
    double JD = get_julian_day(dt);
    return ln_get_apparent_sidereal_time(JD) + loc.lon / 15.0; // Approximation, libnova might handle lon in sidereal func?
    // ln_get_apparent_sidereal_time returns Mean Sidereal Time at Greenwich in hours.
    // LST = GST + lon_hours
}

double get_angular_separation(double ra1, double dec1, double ra2, double dec2) {
    struct ln_equ_posn pos1 = {ra1, dec1};
    struct ln_equ_posn pos2 = {ra2, dec2};
    return ln_get_angular_separation(&pos1, &pos2);
}

void get_moon_equ_coords(DateTime dt, double *ra, double *dec) {
    double JD = get_julian_day(dt);
    struct ln_equ_posn equ;
    ln_get_lunar_equ_coords(JD, &equ);
    *ra = equ.ra;
    *dec = equ.dec;
}
