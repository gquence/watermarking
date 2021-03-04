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

all: 
	g++ -Wall  -Wextra srcs/*.cpp  -pthread -lm -I /usr/local/include  -lavformat -lavcodec -lswscale -lavutil -lavfilter -lswresample -lavdevice -lz -lx264 -lva

$(NAME): $(OBJ)
	@echo "Linking..."
	$(CC) $(COMMON_FLAGS)  $(COMMON_LIBS) $(OBJ) -o $@  $(LIB_FLAGS)

$(OBJ): $(OBJ_DIR)%.o: $(SRC_DIR)%.cpp | $(OBJ_DIR)
	$(CC) $(COMMON_FLAGS) -c $< -o $@ -I $(LIB_INC)

$(OBJ_DIR):
	@mkdir $(OBJ_DIR) 2< /dev/null || true
	@echo "Compiling..."

clean:
	rm -rf $(OBJ) $(OBJ_DIR)
	
fclean: clean
	rm -rf $(NAME)

re: fclean all

#g++ -Wall  -Wextra -march=native srcs/*.cpp  -pthread -lm -I /usr/local/include  -lavformat -lavcodec -lswscale -lavutil -lavfilter -lswresample -lavdevice -lz -lx264 -lva