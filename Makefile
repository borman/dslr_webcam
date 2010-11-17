OBJECTS := dslr_webcam.o
TARGET := dslr_webcam

CC = gcc
LINK = gcc
OFLAGS ?= -Os -fwhole-program
CFLAGS = $(shell pkg-config --cflags libgphoto2) -ljpeg
CFLAGS += -pipe -g -Wall -Wextra -ansi -pedantic $(OFLAGS)
LDFLAGS = $(shell pkg-config --libs libgphoto2)
LDFLAGS += -Wl,-O1 -Wl,--as-needed

.PHONY: all clean

all: $(TARGET)

clean:
	@echo -e "\tRM\t$(TARGET) $(OBJECTS)"
	@rm -f $(TARGET) $(OBJECTS)

%.o: %.c
	@echo -e "\tCC\t$^"
	@$(CC) $(CFLAGS) -c $^ -o $@

$(TARGET): $(OBJECTS)
	@echo -e "\tLD\t$^"
	@$(LINK) $(CFLAGS) $(LDFLAGS) $^ -o $@
