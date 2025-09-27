# Compilador y flags
CXX = g++
CC = gcc

# Flags UNIFICADOS
CXXFLAGS = -std=c++17 -O2 -Wall -I./include -I./rzLogger/include -I/home/rzzz/Documents/telegram_dev/td/tdlib/include -pthread
CFLAGS = -O2 -Wall -I./include -I./rzLogger/include -pthread

# Archivos fuente - con rutas completas
SRCS = main.cpp TelegramBot.cpp
C_SRCS = rzLogger/rzLogger.c

# Objetos - manteniendo la estructura de directorios
OBJS = $(SRCS:.cpp=.o) rzLogger.o

# Librerías TDLib
LIBS = \
  /home/rzzz/Documents/telegram_dev/td/build/libtdclient.a \
  /home/rzzz/Documents/telegram_dev/td/build/libtdapi.a \
  /home/rzzz/Documents/telegram_dev/td/build/libtdcore.a \
  /home/rzzz/Documents/telegram_dev/td/build/libtdmtproto.a \
  /home/rzzz/Documents/telegram_dev/td/build/tddb/libtddb.a \
  /home/rzzz/Documents/telegram_dev/td/build/tdnet/libtdnet.a \
  /home/rzzz/Documents/telegram_dev/td/build/tde2e/libtde2e.a \
  /home/rzzz/Documents/telegram_dev/td/build/tdactor/libtdactor.a \
  /home/rzzz/Documents/telegram_dev/td/build/tdtl/libtdtl.a \
  /home/rzzz/Documents/telegram_dev/td/build/sqlite/libtdsqlite.a \
  /home/rzzz/Documents/telegram_dev/td/build/tdutils/libtdutils.a \
  /home/rzzz/Documents/telegram_dev/td/build/libtdjson_static.a \
  /home/rzzz/Documents/telegram_dev/td/build/libmemprof.a \
  -lssl -lcrypto -lz -ldl -pthread

# Ejecutable final
TARGET = telegram_bot

# Reglas EXPLÍCITAS para cada archivo objeto
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -Wl,--start-group $(LIBS) -Wl,--end-group -o $(TARGET)

rzLogger.o: rzLogger/rzLogger.c
	$(CC) $(CFLAGS) -c $< -o $@

# Reglas para archivos .cpp
main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

TelegramBot.o: TelegramBot.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@


clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean