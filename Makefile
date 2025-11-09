# Compilador y flags
CXX = g++
CC = gcc

# Sysroot de la Raspberry Pi
RPI_SYSROOT = $(HOME)/Documents/rzzzvpn_sysroot

# Compiladores viejos compatibles
CXX_RPI = aarch64-linux-gnu-g++-10 -g -O1 -fno-omit-frame-pointer #-fsanitize=address
CC_RPI = aarch64-linux-gnu-gcc-10

# Flags optimizados para RPi 3B+
BASE_ARM_FLAGS = -march=armv8-a -mtune=cortex-a53 -O2
SYSROOT_FLAGS = --sysroot=$(RPI_SYSROOT)

CXXFLAGS_RPI = -std=c++17 $(BASE_ARM_FLAGS) $(SYSROOT_FLAGS) \
               -I./include -I./rzLogger/include \
               -I$(TDLIB_PATH_RPI)/tdlib/include \
               -pthread

CFLAGS_RPI = $(BASE_ARM_FLAGS) $(SYSROOT_FLAGS) \
             -I./include -I./rzLogger/include \
             -pthread

# Enlace 100% estático
LDFLAGS_RPI = $(SYSROOT_FLAGS) \
              -static -static-libgcc -static-libstdc++ \
              -Wl,--whole-archive -lpthread -Wl,--no-whole-archive

# Rutas base
TDLIB_PATH = /home/rzzz/Documents/telegram_dev/td
TDLIB_PATH_RPI = /home/rzzz/Documents/telegram_dev/td_arm64/td
TDLIB_INCLUDE = $(TDLIB_PATH)/tdlib/include
TDLIB_BUILD = $(TDLIB_PATH)/build
TDLIB_BUILD_RPI = $(TDLIB_PATH_RPI)/build
ARM64_LIBS_DIR = /home/rzzz/Documents/telegram_dev/TelegramBotCpp/cross_libs_arm64

# Flags base comunes
BASE_INCLUDES = -I./include -I./rzLogger/include -I$(TDLIB_INCLUDE)
BASE_FLAGS = -O2 -Wall -pthread

# Flags NORMALES (host)
CXXFLAGS = -std=c++17 $(BASE_FLAGS) $(BASE_INCLUDES)
CFLAGS = $(BASE_FLAGS) $(BASE_INCLUDES)

# Flags DEBUG (host)
CXXFLAGS_DEBUG = -std=c++17 -g -O0 -Wall $(BASE_INCLUDES) -pthread
CFLAGS_DEBUG = -g -O0 -Wall $(BASE_INCLUDES) -pthread

# Archivos fuente
SRCS = main.cpp TelegramBot.cpp
C_SRCS = rzLogger/rzLogger.c

# Objetos
OBJS = main.o TelegramBot.o rzLogger.o
OBJS_RPI = main_rpi.o TelegramBot_rpi.o rzLogger_rpi.o

# Librerías TDLib (HOST)
LIBS = \
  $(TDLIB_BUILD)/libtdclient.a \
  $(TDLIB_BUILD)/libtdapi.a \
  $(TDLIB_BUILD)/libtdcore.a \
  $(TDLIB_BUILD)/libtdmtproto.a \
  $(TDLIB_BUILD)/tddb/libtddb.a \
  $(TDLIB_BUILD)/tdnet/libtdnet.a \
  $(TDLIB_BUILD)/tde2e/libtde2e.a \
  $(TDLIB_BUILD)/tdactor/libtdactor.a \
  $(TDLIB_BUILD)/tdtl/libtdtl.a \
  $(TDLIB_BUILD)/sqlite/libtdsqlite.a \
  $(TDLIB_BUILD)/tdutils/libtdutils.a \
  $(TDLIB_BUILD)/libtdjson_static.a \
  $(TDLIB_BUILD)/libmemprof.a \
  -lssl -lcrypto -lz -ldl -pthread

# Librerías TDLib (RASPBERRY PI) - debes compilar TDLib para ARM
LIBS_RPI = \
  $(TDLIB_BUILD_RPI)/libtdclient.a \
  $(TDLIB_BUILD_RPI)/libtdapi.a \
  $(TDLIB_BUILD_RPI)/libtdcore.a \
  $(TDLIB_BUILD_RPI)/libtdmtproto.a \
  $(TDLIB_BUILD_RPI)/tddb/libtddb.a \
  $(TDLIB_BUILD_RPI)/tdnet/libtdnet.a \
  $(TDLIB_BUILD_RPI)/tde2e/libtde2e.a \
  $(TDLIB_BUILD_RPI)/tdactor/libtdactor.a \
  $(TDLIB_BUILD_RPI)/tdtl/libtdtl.a \
  $(TDLIB_BUILD_RPI)/sqlite/libtdsqlite.a \
  $(TDLIB_BUILD_RPI)/tdutils/libtdutils.a \
  $(TDLIB_BUILD_RPI)/libtdjson_static.a \
  $(TDLIB_BUILD_RPI)/libmemprof.a \
  $(RPI_SYSROOT)/usr/lib/aarch64-linux-gnu/libssl.a \
  $(RPI_SYSROOT)/usr/lib/aarch64-linux-gnu/libcrypto.a \
  $(RPI_SYSROOT)/usr/lib/aarch64-linux-gnu/libz.a \
  -ldl -pthread
#  $(ARM64_LIBS_DIR)/lib/libssl.a \
#  $(ARM64_LIBS_DIR)/lib/libcrypto.a \
#  $(ARM64_LIBS_DIR)/lib/libz.a \

# Ejecutables
TARGET = telegram_bot
TARGET_RPI = telegram_bot_rpi

# ============ REGLAS PRINCIPALES ============

all: $(TARGET)

debug: CXXFLAGS=$(CXXFLAGS_DEBUG)
debug: CFLAGS=$(CFLAGS_DEBUG)
debug: clean $(TARGET)

rpi: $(TARGET_RPI)

# ============ COMPILACIÓN HOST ============

$(TARGET): $(OBJS)
	@echo "Enlazando para HOST..."
	$(CXX) $(OBJS) -Wl,--start-group $(LIBS) -Wl,--end-group -o $(TARGET)
	@echo "Compilación HOST completada: $(TARGET)"

main.o: main.cpp
	@echo "Compilando main.cpp (HOST)..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

TelegramBot.o: TelegramBot.cpp
	@echo "Compilando TelegramBot.cpp (HOST)..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

rzLogger.o: rzLogger/rzLogger.c
	@echo "Compilando rzLogger.c (HOST)..."
	$(CC) $(CFLAGS) -c $< -o $@

# ============ COMPILACIÓN RASPBERRY PI (CROSS-COMPILE) ============

# Flags para RPI
CXX_RPI = aarch64-linux-gnu-g++
CC_RPI = aarch64-linux-gnu-gcc
CXXFLAGS_RPI = $(CXXFLAGS) -I$(ARM64_LIBS_DIR)/include -static -static-libstdc++ -static-libgcc
CFLAGS_RPI = $(CFLAGS) -I$(ARM64_LIBS_DIR)/include -static -static-libstdc++ -static-libgcc
LDFLAGS_RPI = -L$(ARM64_LIBS_DIR)/lib -static -static-libstdc++ -static-libgcc -pthread

# Regla con verificación
$(TARGET_RPI): $(OBJS_RPI)
	@echo "Enlazando con sysroot y flags compatibles..."
	$(CXX_RPI) $(OBJS_RPI) \
		$(LDFLAGS_RPI) \
		-Wl,--start-group $(LIBS_RPI) -Wl,--end-group \
		-o $(TARGET_RPI)
	@./check_binary.sh
	@echo "Binario listo y verificado"
	@echo "Compilación RASPBERRY PI completada: $(TARGET_RPI)"
	@echo "Transfiere el ejecutable a tu RPi con:"
	@echo "    scp $(TARGET_RPI)"

main_rpi.o: main.cpp
	@echo "Compilando main.cpp (RASPBERRY PI ARM64)..."
	$(CXX_RPI) $(CXXFLAGS_RPI) -c $< -o $@

TelegramBot_rpi.o: TelegramBot.cpp
	@echo "Compilando TelegramBot.cpp (RASPBERRY PI ARM64)..."
	$(CXX_RPI) $(CXXFLAGS_RPI) -c $< -o $@

rzLogger_rpi.o: rzLogger/rzLogger.c
	@echo "Compilando rzLogger.c (RASPBERRY PI ARM64)..."
	$(CC_RPI) $(CFLAGS_RPI) -c $< -o $@

# ============ VERIFICACIONES ============

check-cross-compiler:
	@echo "Verificando cross-compiler para ARM64..."
	@which $(CXX_RPI) > /dev/null 2>&1 || \
		(echo "ERROR: Cross-compiler no encontrado." && \
		 echo "Instala con: sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu" && \
		 exit 1)
	@echo "Cross-compiler encontrado: $(CXX_RPI)"

check-tdlib-rpi:
	@echo "Verificando TDLib compilada para ARM64..."
	@test -d $(TDLIB_BUILD_RPI) || \
		(echo "ERROR: No existe $(TDLIB_BUILD_RPI)" && \
		 echo "Necesitas compilar TDLib para ARM64 primero." && \
		 echo "Ejecuta: make build-tdlib-rpi" && \
		 exit 1)
	@echo "TDLib ARM64 encontrada"

# ============ HELPERS ============

install-cross-compiler:
	@echo "Instalando cross-compiler para ARM64..."
	sudo apt update
	sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Añadir al final del Makefile

build-libs-arm64:
	@echo "Compilando librerías ARM64..."
	./build_arm64_libs.sh

check-libs-arm64:
	@echo "🔍 Verificando librerías ARM64..."
	@test -f $(ARM64_LIBS_DIR)/lib/libssl.a || \
		(echo "Librerías no encontradas. Ejecuta: make build-libs-arm64" && exit 1)
	@echo "Librerías ARM64 OK"


deploy:
	@echo "Subiendo $(TARGET) a Raspberry Pi..."
	scp $(TARGET) rzzzvpn:/home/rzzz/

clean:
	@echo "Limpiando archivos objeto y ejecutables..."
	rm -f $(OBJS) $(OBJS_RPI) $(TARGET) $(TARGET_RPI)
	@echo "Limpieza completada"

clean-all: clean
	@echo "Limpiando también build de TDLib ARM64..."
	rm -rf $(TDLIB_BUILD_RPI)

help:
	@echo "════════════════════════════════════════════════════════════════"
	@echo "  Makefile para Telegram Bot (HOST y Raspberry Pi Cross-Compile)"
	@echo "════════════════════════════════════════════════════════════════"
	@echo ""
	@echo "Compilación LOCAL (tu PC):"
	@echo "  make              - Compilar versión normal"
	@echo "  make debug        - Compilar versión debug"
	@echo "  make clean        - Limpiar archivos compilados"
	@echo ""
	@echo "Cross-Compilación para RASPBERRY PI:"
	@echo "  make install-cross-compiler  - Instalar herramientas ARM64"
	@echo "  make create-toolchain        - Crear toolchain CMake"
	@echo "  make build-tdlib-rpi         - Compilar TDLib para ARM64"
	@echo "  make rpi                     - Compilar bot para Raspberry Pi"
	@echo "  make deploy                  - Subir bot a Raspberry Pi via SCP"
	@echo ""
	@echo "Limpieza:"
	@echo "  make clean        - Limpiar objetos locales"
	@echo "  make clean-all    - Limpiar todo (incluye TDLib ARM64)"
	@echo ""
	@echo "════════════════════════════════════════════════════════════════"

.PHONY: all debug rpi clean clean-all check-cross-compiler check-tdlib-rpi \
        build-tdlib-rpi create-toolchain install-cross-compiler deploy help \
		build-libs-arm64 check-libs-arm64