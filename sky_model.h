#ifndef SKY_MODEL_H
#define SKY_MODEL_H

#include <libnova/libnova.h>

typedef struct {
    double lat;
    double lon;
} Location;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    double second;
    double timezone_offset; // hours from UTC
} DateTime;

typedef enum {
    PLANET_MERCURY,
    PLANET_VENUS,
    PLANET_MARS,
    PLANET_JUPITER,
    PLANET_SATURN,
    PLANET_URANUS,
    PLANET_NEPTUNE
} PlanetID;

void get_horizontal_coordinates(double ra, double dec, Location loc, DateTime dt, double *alt, double *az);
void get_sun_position(Location loc, DateTime dt, double *alt, double *az);
void get_moon_position(Location loc, DateTime dt, double *alt, double *az);
void get_planet_position(PlanetID planet, Location loc, DateTime dt, double *alt, double *az, double *ra, double *dec);
double get_julian_day(DateTime dt);
double get_lst(DateTime dt, Location loc);
double get_angular_separation(double ra1, double dec1, double ra2, double dec2);
void get_moon_equ_coords(DateTime dt, double *ra, double *dec);

#endif
