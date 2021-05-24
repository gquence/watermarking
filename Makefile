CC = g++

SRC_DIR = ./srcs/
INC_DIR = ./includes/
SRCS = main.cpp format_ctx.cpp stream.cpp
OBJ_DIR = ./obj/

NAME = test

COMMON_FLAGS = -Wall -Werror -Wextra -march=native
COMMON_LIBS = -pthread -lm
LIB_INC = /usr/local/include
LIB_FLAGS = -lavformat -lavcodec -lswscale -lavutil -lavfilter -lswresample -lavdevice -lz -lx264 -lva 

SRC = $(addprefix $(SRC_DIR), $(SRCS))
OBJ = $(addprefix $(OBJ_DIR), $(SRCS:.cpp=.o))

all: all_get all_set

all_set:
	g++ -std=c++17 main.cpp  -O3 -pthread -lm -I /usr/local/include  -lavformat -lavcodec -lswscale -lavutil -lavfilter -lswresample -lavdevice -lz -lx264 -lva -o set_mark.out
all_get: 
	g++ -std=c++17 find_watermark.cpp -O3 -pthread -lm -I /usr/local/include  -lavformat -lavcodec -lswscale -lavutil -lavfilter -lswresample -lavdevice -lz -lx264 -lva -o get_mark.out


fclean: clean
	rm -rf set_mark.out get_mark.out

re: fclean all

#g++ -Wall  -Wextra -march=native srcs/*.cpp  -pthread -lm -I /usr/local/include  -lavformat -lavcodec -lswscale -lavutil -lavfilter -lswresample -lavdevice -lz -lx264 -lva