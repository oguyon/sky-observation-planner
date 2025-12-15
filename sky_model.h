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

void get_horizontal_coordinates(double ra, double dec, Location loc, DateTime dt, double *alt, double *az);
void get_sun_position(Location loc, DateTime dt, double *alt, double *az);
void get_moon_position(Location loc, DateTime dt, double *alt, double *az);
double get_julian_day(DateTime dt);

#endif
