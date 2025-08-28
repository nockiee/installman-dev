CC=g++
CFLAGS=-std=c++11 `pkg-config --cflags gtk+-3.0` -I/usr/include/libarchive
LIBS=`pkg-config --libs gtk+-3.0` -larchive
TARGET=installman
PREFIX=/usr/local

all: $(TARGET)

$(TARGET): installman.cpp
	$(CC) $(CFLAGS) -o $(TARGET) installman.cpp $(LIBS)

install: $(TARGET)
	install -D -m 755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	install -D -m 755 uninstaller.sh $(PREFIX)/bin/installman-uninstall
	install -D -m 644 installman.desktop /usr/share/applications/installman.desktop
	update-desktop-database /usr/share/applications/

clean:
	rm -f $(TARGET)

.PHONY: all install clean