all: hyprexpo.so
hyprexpo.so: main.cpp overview.cpp ExpoGesture.cpp OverviewPassElement.cpp
	$(CXX) -shared -fPIC --no-gnu-unique main.cpp overview.cpp ExpoGesture.cpp OverviewPassElement.cpp -o hyprexpo.so -g `pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon` -std=c++2b -Wno-narrowing
install: hyprexpo.so
	hyprctl plugin unload /usr/lib/libhyprexpo.so
	cp ./hyprexpo.so /usr/lib/libhyprexpo.so
	hyprctl plugin load /usr/lib/libhyprexpo.so
uninstall:
	rm /usr/lib/libhyprexpo.so
clean:
	rm ./hyprexpo.so
