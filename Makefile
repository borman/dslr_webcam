OBJECTS := dslr_webcam.o
TARGET := dslr_webcam

CC = gcc
LINK = gcc
CFLAGS = $(shell pkg-config --cflags libgphoto2) -ljpeg
CFLAGS += -pipe -O2 -march=athlon64 -mtune=athlon64 -g
LDFLAGS = $(shell pkg-config --libs libgphoto2)
LDFLAGS += -Wl,-O1


%.o: %.c
	@echo -e "\tCC\t$^"
	@$(CC) $(CFLAGS) -c $^ -o $@

$(TARGET): $(OBJECTS)
	@echo -e "\tLD\t$^"
	@$(LINK) $(CFLAGS) $(LDFLAGS) $^ -o $@
