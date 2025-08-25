all: hyprswish.so
hyprswish.so: main.cpp overview.cpp OverviewPassElement.cpp
	$(CXX) -shared -fPIC --no-gnu-unique main.cpp overview.cpp OverviewPassElement.cpp -o hyprswish.so -g `pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon` -std=c++2b -Wno-narrowing
install: hyprswish.so
	hyprctl plugin unload /usr/lib/libhyprswish.so
	cp ./hyprswish.so /usr/lib/libhyprswish.so
	hyprctl plugin load /usr/lib/libhyprswish.so
uninstall:
	rm /usr/lib/libhyprswish.so
clean:
	rm ./hyprswish.so
