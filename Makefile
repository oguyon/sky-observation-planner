CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0` -I/usr/include/libnova -Wall -g
LIBS = `pkg-config --libs gtk+-3.0` -lnova -lm

TARGET = night_sky

SRCS = main.c sky_model.c sky_view.c elevation_view.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
