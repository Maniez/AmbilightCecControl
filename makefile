prog: AmbilightCecControl.o
	g++ -o prog AmbilightCecControl.o

AmbilightCecControl.o: AmbilightCecControl.cpp
	g++ -c -ldl -Wall -Wextra scr/AmbilightCecControl.cpp
