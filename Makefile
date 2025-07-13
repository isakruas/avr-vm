# Nome do executável
TARGET = main

# Diretórios
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

# Compilador e flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -I$(INCLUDE_DIR)

# Lista de arquivos fonte e objetos
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Regra principal
all: $(BIN_DIR)/$(TARGET)

# Linkagem final
$(BIN_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) -o $@

# Compilação de cada .c para .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Limpar arquivos compilados
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Executar o programa
run: all
	./$(BIN_DIR)/$(TARGET)
