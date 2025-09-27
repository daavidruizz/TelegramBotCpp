#include "TelegramBot.h"
#include <iostream>
#include <cstdlib>
#include <signal.h>
#include <thread>
#include <chrono>
#include "rzLogger.h"

TelegramBot* bot = nullptr;

void signal_handler(int signal) {
    std::cout << "\nRecibida señal de interrupción. Cerrando bot..." << std::endl;
    if (bot) {
        bot->stop();
        delete bot;
        bot = nullptr;
    }
    exit(0);
}

int main() {
    // Manejar señales de interrupción
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    rzLog_init();
    rzLog_set_level(RZ_LOG_DEBUG);

    rzLog(RZ_LOG_INFO, "Iniciando bot...");

    // Obtener credenciales de variables de entorno
    const char* api_id_str = std::getenv("TELEGRAM_API_ID");
    const char* api_hash = std::getenv("TELEGRAM_API_HASH");
    const char* bot_token = std::getenv("TELEGRAM_BOT_TOKEN");

    if (!api_id_str || !api_hash || !bot_token) {
        std::cerr << "Error: Debes establecer las variables de entorno:" << std::endl;
        std::cerr << "TELEGRAM_API_ID, TELEGRAM_API_HASH, TELEGRAM_BOT_TOKEN" << std::endl;
        return 1;
    }

    std::string api_id(api_id_str);

    try {
        
        bot = new TelegramBot();
        
        if (!bot->initialize(api_id, bot_token, api_hash)) {
            rzLog(RZ_LOG_ERROR, "Error al inicializar el bot");
            delete bot;
            return 1;
        }

        rzLog(RZ_LOG_INFO, "Bot inicializado correctamente");
        rzLog(RZ_LOG_INFO, "Esperando respuestas de TDLib...");

        bot->run();  // Tu función run() original

        // Mantener el programa corriendo Y mostrar estado cada 5 segundos
        int counter = 0;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            counter++;
            rzLog(RZ_LOG_INFO, "[MAIN] Bot ejecutándose...");
        }

    } catch (const std::exception& e) {

        rzLog(RZ_LOG_ERROR,"Excepción: ");

        rzLog_stop();
        if (bot) {
            delete bot;
        }
        return 1;
    }

    return 0;
}