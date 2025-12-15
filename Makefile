CC = gcc
CFLAGS = -Wall -g `pkg-config --cflags gtk4 jansson`
LIBS = `pkg-config --libs gtk4 jansson` -lnova -lm

TARGET = night_sky
SRCS = main.c catalog.c sky_model.c sky_view.c elevation_view.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
