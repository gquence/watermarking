all: all_get all_set

all_set:
	g++ -std=c++17 main.cpp  -O3 -pthread -lm -I /usr/local/include  -lavformat -lavcodec -lswscale -lavutil -lavfilter -lswresample -lavdevice -lz -lx264 -lva -o set_mark.out
all_get: 
	g++ -std=c++17 find_watermark.cpp -O3 -pthread -lm -I /usr/local/include  -lavformat -lavcodec -lswscale -lavutil -lavfilter -lswresample -lavdevice -lz -lx264 -lva -o get_mark.out


clean: 
	rm -rf set_mark.out get_mark.out

re: clean all