#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cstdio>     // Para printf
#include <cstring>    // Para strcmp, strstr
#include <cstdlib>    // Para atoi
#include <map>
#include <functional>


class TelegramBot {
    private:
    
    // Credenciales
    std::string api_id_;
    std::string api_hash_;
    std::string bot_token_;
    std::string download_pàth_;
    
    // Cliente TDLib - PUNTERO NORMAL ESTILO C
    td::ClientManager* client_manager_;  // En lugar de unique_ptr
    std::int32_t client_id_;
    
    // Estado del bot
    std::atomic<bool> running_;
    std::atomic<bool> are_authorized_;
    std::atomic<bool> need_restart_;
    
    // Hilo de trabajo
    std::thread worker_thread_;
    
    // Sistema de queries
    std::uint64_t current_query_id_ = 1;
    std::map<std::uint64_t, std::function<void(td::td_api::object_ptr<td::td_api::Object>)>> handlers_;
    
    // Estado de autorización
    td::td_api::object_ptr<td::td_api::AuthorizationState> authorization_state_;


public:
    TelegramBot();
    ~TelegramBot();
    
    // Funciones principales
    bool initialize(const std::string& api_id, const std::string& bot_token, const std::string& api_hash, const std::string& download_path);
    void run();
    void stop();
    
private:
    // Bucle principal
    void main_loop();
    
    // Procesar respuestas
    void process_response(td::td_api::object_ptr<td::td_api::Object> response);
    
    // Manejo de autorización
    void handle_authorization_update();
    void send_tdlib_parameters();
    void send_bot_token();
    
    void start_file_download(int32_t file_id);

    // Manejo de mensajes
    void handle_new_message(td::td_api::object_ptr<td::td_api::message> message);
    std::string extract_message_text(int64_t chat_id, td::td_api::MessageContent* content);
    std::string generate_response(const std::string& text);
    
    // Envío de mensajes
    void send_text_message(int64_t chat_id, const std::string& text);
    void send_typing_action(int64_t chat_id);
    
    void handle_video(int32_t chat_id, td::td_api::messageVideo* video);
    
    void handle_file_update(td::td_api::object_ptr<td::td_api::file> file);
    void handle_download_response(int32_t file_id, td::td_api::object_ptr<td::td_api::Object> response);
    
    // Manejo de errores
    void handle_error(td::td_api::error* error);
    
    // Sistema de queries
    void send_query(
        td::td_api::object_ptr<td::td_api::Function> query,
        std::function<void(td::td_api::object_ptr<td::td_api::Object>)> handler);
};

#endif // TELEGRAM_BOT_H