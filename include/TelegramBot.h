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
#include <set>
#include <functional>

/**
 * @class TelegramBot
 * @brief Clase principal que gestiona la comunicación entre el bot y la biblioteca TDLib.
 * 
 * Encapsula las operaciones de inicialización, envío y recepción de mensajes,
 * descargas de archivos, manejo de errores y respuestas automáticas.
 */
class TelegramBot {
    private:
    
    struct FileType {
        std::string fileName;
        std::string extension;
        std::string mimeType;
        int64_t fileSize;
    };
    
    struct DownloadInfo {
        int64_t chat_id;
        int64_t message_id;
        std::string original_text;
        time_t start_time;
        time_t finish_time;
        FileType file;
    };

    using DownloadMap = std::unordered_map<int32_t, DownloadInfo>;

    std::set<int64_t> user_ids_allowed;

    // Mapa para callbacks pendientes esperando ID real
    std::map<int64_t, std::function<void(int64_t)>> pending_message_callbacks_;

    // Credenciales
    std::string api_id_;
    std::string api_hash_;
    std::string bot_token_;
    std::string path_bot_db_;
    std::string download_path_;

    
    td::ClientManager* client_manager_; 
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

    bool is_message_allowed(int64_t user_id);
    int64_t get_sender_user_id(td::td_api::MessageSender* sender);

public:

    TelegramBot();
    ~TelegramBot();
    
    // Funciones principales
    bool initialize(const std::string& api_id, const std::string& bot_token, const std::string& api_hash, const std::string& bot_db_path, const std::string& download_path, const std::string& user_id_credentials);
    void run();
    void stop();

    DownloadMap downloads_;

    bool usingCredentials = 0;

    // Bucle principal
    void main_loop();
    
    // Procesar respuestas
    void process_response(uint64_t query_id, td::td_api::object_ptr<td::td_api::Object> response);
    
    // Manejo de autorización
    void handle_authorization_update();
    void send_tdlib_parameters();
    void send_bot_token();
    
    void start_file_download(int32_t file_id);

    // Manejo de mensajes
    void handle_new_updateNewMessage(td::td_api::object_ptr<td::td_api::message> message);
    std::string extract_updateNewMessage_data(int64_t chat_id, td::td_api::MessageContent* content);
    std::string generate_response(const std::string& text);
    
    // Envío de mensajes
    void send_text_message(int64_t chat_id, const std::string& text, std::function<void(int64_t message_id)> callback);
    //void send_text_message(int64_t chat_id, const std::string& text);
    void send_edited_message(int64_t chat_id, int64_t message_id, const std::string& text);

    void send_typing_action(int64_t chat_id);
    
    void handle_video(int32_t chat_id, td::td_api::messageVideo* video);
    void handle_document(int32_t chat_id, td::td_api::messageDocument* document);
    
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