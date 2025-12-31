CXX := g++
CXXFLAGS := -std=c++2b -fPIC -shared -Wno-narrowing -O3
LDFLAGS := -g

# Get both compiler and linker flags from pkg-config
PKG_DEPS := pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_DEPS))
PKG_LDFLAGS := $(shell pkg-config --libs $(PKG_DEPS))

SRCS := main.cpp overview.cpp ExpoGesture.cpp SwishGesture.cpp OverviewPassElement.cpp
OBJS := $(SRCS:.cpp=.o)
TARGET := hyprexpo.so

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(PKG_LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
