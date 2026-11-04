CXX ?= g++
CXXFLAGS ?= -O2 -g
CPPFLAGS += $(shell pkg-config --cflags $(PKG_DEPS))

PKG_DEPS := pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon
PKG_LIBS = $(shell pkg-config --libs $(PKG_DEPS))

SRCS := main.cpp overview.cpp ExpoGesture.cpp OverviewPassElement.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)
TARGET := hyprexpo.so
TEST_TARGETS := tests/grid-test tests/preview-test

.PHONY: all clean test
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -shared -o $@ $^ $(PKG_LIBS) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++23 -fPIC -MMD -MP -c $< -o $@

tests/grid-test: Grid.hpp
tests/preview-test: WorkspacePreview.hpp

tests/%-test: tests/%.cpp
	$(CXX) $(CXXFLAGS) -std=c++23 -Wall -Wextra -Werror $< -o $@

test: $(TEST_TARGETS)
	./tests/grid-test
	./tests/preview-test

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS) $(TEST_TARGETS)

-include $(DEPS)
