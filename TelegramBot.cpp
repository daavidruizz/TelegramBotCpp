#include "TelegramBot.h"
#include <chrono>
#include <iomanip>
#include <chrono>
#include <string>
#include "rzLogger.h"
#include <td/telegram/Log.h>
#include <cmath>

std::unordered_map<int32_t, int32_t> last_downloaded_;
std::unordered_map<int32_t, std::chrono::steady_clock::time_point> last_time_;
std::unordered_map<int32_t, int> last_progress_reported_;

struct DownloadStatus {
    int64_t chat_id;
    int64_t file_id;
    int64_t message_id; // mensaje que se va a editar
    int last_percent;
};

std::unordered_map<int64_t, DownloadStatus> downloads_; // key = file_id

TelegramBot::TelegramBot() {
    td::td_api::setLogVerbosityLevel(0);
    td::Log::set_verbosity_level(0);
    client_manager_ = new td::ClientManager();
    client_id_ = client_manager_->create_client_id();
    
    authorization_state_ = td::td_api::make_object<td::td_api::authorizationStateClosed>();
    
    running_ = false;
    are_authorized_ = false;
    need_restart_ = false;
    current_query_id_ = 1;
    // 0 = solo errores críticos
    rzLog(RZ_LOG_INFO, "TelegramBot creado");
    fflush(stdout);
}

TelegramBot::~TelegramBot() {
    stop();
    
    if (client_manager_) {
        delete client_manager_;
        client_manager_ = nullptr;
    }
    rzLog(RZ_LOG_INFO, "TelegramBot destruido");
}

bool TelegramBot::initialize(const std::string& api_id, const std::string& bot_token, const std::string& api_hash, const std::string& download_path) {
    api_id_ = api_id;
    api_hash_ = api_hash;
    bot_token_ = bot_token;
    download_pàth_ = download_path;
    
   rzLog(RZ_LOG_INFO, "[BOT] Inicializado con API_ID=%s, API_HASH=%s..., BOT_TOKEN=%s...", 
           api_id.c_str(), 
           api_hash.substr(0, 8).c_str(),
           bot_token.substr(0, 12).c_str());
    return true;
}

void TelegramBot::run() {
    if (running_) {
       rzLog(RZ_LOG_INFO, "[BOT] Run");
        return;
    }
    
    running_ = true;
    worker_thread_ = std::thread(&TelegramBot::main_loop, this);
    
    // FORZAR el inicio enviando parámetros inmediatamente
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    rzLog(RZ_LOG_INFO, "[BOT] Enviando parámetros iniciales...");
    send_tdlib_parameters();
}

void TelegramBot::stop() {
    if (!running_) return;
    
    rzLog(RZ_LOG_INFO, "[BOT] Deteniendo...");
    running_ = false;
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    rzLog(RZ_LOG_INFO, "[BOT] Detenido completamente");
}

void TelegramBot::main_loop() {
    rzLog(RZ_LOG_INFO, "[LOOP] Bucle principal iniciado");
    fflush(stdout);
    
    // Enviar parámetros iniciales inmediatamente
    bool params_sent = false;
    
    while (running_) {
        if (need_restart_) {
            rzLog(RZ_LOG_INFO, "[LOOP] Reiniciando cliente...");
            
            if (client_manager_) {
                delete client_manager_;
            }
            
            client_manager_ = new td::ClientManager();
            client_id_ = client_manager_->create_client_id();
            
            need_restart_ = false;
            are_authorized_ = false;
            params_sent = false; // Reset para reenviar parámetros
            rzLog(RZ_LOG_INFO, "[LOOP] Cliente reiniciado con ID: %d", client_id_);
        }
        
        // Enviar parámetros en la primera iteración
        if (!params_sent) {
            rzLog(RZ_LOG_INFO, "[LOOP] Enviando parámetros TDLib por primera vez...");
            send_tdlib_parameters();
            params_sent = true;
        }
        
        if (!client_manager_) {
            rzLog(RZ_LOG_INFO, "[LOOP] ERROR: client_manager_ es null!");
            continue;
        }
        
        // Recibir respuesta con timeout
        auto response = client_manager_->receive(1.0);
        
        if (response.object) {
            rzLog(RZ_LOG_DEBUG_EXTRA, "[LOOP] Respuesta recibida, tipo: %d", response.object->get_id());
            fflush(stdout);
            process_response(std::move(response.object));
        } else {
            // Mostrar que estamos esperando (cada 10 iteraciones para no spam)
            static int wait_counter = 0;
            if (++wait_counter % 10 == 0) {
                rzLog(RZ_LOG_DEBUG_EXTRA, "[LOOP] Esperando respuestas... (autorizado: %s)", 
                are_authorized_ ? "SÍ" : "NO");
                fflush(stdout);
            }
        }
    }
    
    rzLog(RZ_LOG_INFO, "[LOOP] Bucle principal terminado");
}

void TelegramBot::handle_file_update(td::td_api::object_ptr<td::td_api::file> file) {
    int32_t file_id = file->id_;
    int64_t downloaded = file->local_->downloaded_size_;
    int64_t total = file->size_;
    //bool is_downloading = file->local_->is_downloading_active_;
    bool is_complete = file->local_->is_downloading_completed_;

    auto it = downloads_.find(file_id);
    if (it == downloads_.end()) return; // No tenemos mensaje de progreso

    if (total <= 0) return; // Evitar división por cero

    float progress = (downloaded * 100.0f / total);
    int progress_5 = static_cast<int>(std::floor(progress / 5.0f) * 5); // redondear a múltiplo de 5

    // Solo reportar si hemos avanzado al siguiente 5%
    if (!is_complete && last_progress_reported_[file_id] == progress_5) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    double speed_mbps = 0.0;

    if (last_downloaded_.count(file_id)) {
        int64_t prev_bytes = last_downloaded_[file_id];
        auto prev_time = last_time_[file_id];
        int64_t delta_bytes = downloaded - prev_bytes;
        double delta_time = std::chrono::duration<double>(now - prev_time).count(); // segundos

        if (delta_time > 0.0) {
            speed_mbps = (delta_bytes / 1024.0 / 1024.0) / delta_time;
        }
    }

    last_downloaded_[file_id] = downloaded;
    last_time_[file_id] = now;
    last_progress_reported_[file_id] = progress_5;

    // Calcular ETA aproximado
    double eta_sec = (speed_mbps > 0.0) ? ((total - downloaded) / 1024.0 / 1024.0) / speed_mbps : 0.0;
    int eta_min = static_cast<int>(eta_sec / 60);
    int eta_sec_rem = static_cast<int>(std::round(eta_sec)) % 60;

    rzLog(RZ_LOG_INFO,
          "Archivo %d: %d%% (%d/%d bytes) | Velocidad: %.2f MB/s | ETA: %d:%02d",
          file_id, progress_5, downloaded, total, speed_mbps, eta_min, eta_sec_rem);


    auto edit = td::td_api::make_object<td::td_api::editMessageText>();
    auto formattedText = td::td_api::make_object<td::td_api::formattedText>(
        "Descargando… %",
        std::vector<td::td_api::object_ptr<td::td_api::textEntity>>{} // vector vacío
    );

    auto inputMsg = td::td_api::make_object<td::td_api::inputMessageText>(
        std::move(formattedText),
        nullptr,  // sin link preview
        false     // clear_draft
    );

    edit->input_message_content_ = std::move(inputMsg);


    send_query(std::move(edit),nullptr);

    if (is_complete) {
        auto msg = td::td_api::make_object<td::td_api::sendMessage>();
        msg->chat_id_ = it->second.chat_id;

        auto formattedText = td::td_api::make_object<td::td_api::formattedText>(
            "Descargando… %",
            std::vector<td::td_api::object_ptr<td::td_api::textEntity>>{} // vector vacío
        );

        auto inputMsg = td::td_api::make_object<td::td_api::inputMessageText>(
            std::move(formattedText),
            nullptr,  // sin link preview
            false     // clear_draft
        );

        edit->input_message_content_ = std::move(inputMsg);

        // Enviar
        send_query(std::move(msg), nullptr);
        downloads_.erase(it); // ya no necesitamos el mensaje de progreso

        // Limpiar estado
        last_downloaded_.erase(file_id);
        last_time_.erase(file_id);
        last_progress_reported_.erase(file_id);
    }
}



void TelegramBot::process_response(td::td_api::object_ptr<td::td_api::Object> response) {
    if (!response) {
        rzLog(RZ_LOG_INFO, "[PROCESS] Respuesta null recibida");
        return;
    }

    int response_id = response->get_id();
    rzLog(RZ_LOG_DEBUG_EXTRA, "[PROCESS] Procesando respuesta tipo: %d", response_id);
    
    if (response_id == td::td_api::updateAuthorizationState::ID) {
        rzLog(RZ_LOG_INFO, "[PROCESS] -> Es updateAuthorizationState");
        auto update = td::td_api::move_object_as<td::td_api::updateAuthorizationState>(response);
        authorization_state_ = std::move(update->authorization_state_);
        handle_authorization_update();
    }
    else if (response_id == td::td_api::updateNewMessage::ID) {
        rzLog(RZ_LOG_INFO, "[PROCESS] -> Es updateNewMessage ¡MENSAJE RECIBIDO!");
        auto update = td::td_api::move_object_as<td::td_api::updateNewMessage>(response);
        handle_new_message(std::move(update->message_));
    }
    else if (response_id == td::td_api::error::ID) {
        rzLog(RZ_LOG_INFO, "[PROCESS] -> Es error");
        auto error = td::td_api::move_object_as<td::td_api::error>(response);
        handle_error(error.get());
    } else if (response->get_id() == td::td_api::updateFile::ID) {
        auto update = td::td_api::move_object_as<td::td_api::updateFile>(response);
        handle_file_update(std::move(update->file_));
    } else {
       rzLog(RZ_LOG_INFO, "[PROCESS] -> Tipo desconocido: %d (ignorando)", response_id);
    }
}

void TelegramBot::handle_authorization_update() {
    if (!authorization_state_) {
        rzLog(RZ_LOG_INFO, "[AUTH] Estado de autorización null");
        return;
    }

    int auth_state_id = authorization_state_->get_id();
    rzLog(RZ_LOG_INFO, "[AUTH] Estado de autorización: %d", auth_state_id);
    
    if (auth_state_id == td::td_api::authorizationStateWaitTdlibParameters::ID) {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Configurando parámetros TDLib...");
        send_tdlib_parameters();
    }
    else if (auth_state_id == td::td_api::authorizationStateWaitPhoneNumber::ID) {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Enviando token de bot...");
        send_bot_token();
    }
    else if (auth_state_id == td::td_api::authorizationStateReady::ID) {
        are_authorized_ = true;
        rzLog(RZ_LOG_INFO, "[AUTH] -> ¡¡¡BOT AUTORIZADO Y LISTO!!!");
    }
    else if (auth_state_id == td::td_api::authorizationStateClosed::ID) {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Conexión cerrada, programando reinicio");
        are_authorized_ = false;
        need_restart_ = true;
    }
    else if (auth_state_id == td::td_api::authorizationStateLoggingOut::ID) {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Cerrando sesión...");
        are_authorized_ = false;
    }
    else {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Estado desconocido: %d", auth_state_id);
    }
}

void TelegramBot::send_tdlib_parameters() {
    rzLog(RZ_LOG_INFO, "[PARAMS] Creando parámetros TDLib...");
    
    auto query = td::td_api::make_object<td::td_api::setTdlibParameters>();
    
    query->api_id_ = atoi(api_id_.c_str());
    query->api_hash_ = api_hash_;
    query->database_directory_ = "bot_db";
    query->device_model_ = "Bot";
    query->application_version_ = "1.0";
    query->use_message_database_ = false;
    query->use_secret_chats_ = false;
    query->system_language_code_ = "es";
    //query->enable_storage_optimizer_ = true;
    
    rzLog(RZ_LOG_INFO, "[PARAMS] Enviando setTdlibParameters con api_id=%d", 
    query->api_id_);
    
    send_query(std::move(query), nullptr);
}

void TelegramBot::send_bot_token() {
    rzLog(RZ_LOG_INFO, "[TOKEN] Enviando token de autenticación...");
    
    auto auth = td::td_api::make_object<td::td_api::checkAuthenticationBotToken>();
    auth->token_ = bot_token_;
    
    rzLog(RZ_LOG_INFO, "[TOKEN] Token: %s...", bot_token_.substr(0, 12).c_str());
    
    send_query(std::move(auth), nullptr);
}

void TelegramBot::handle_download_response(int32_t file_id, td::td_api::object_ptr<td::td_api::Object> response) {
    if (response->get_id() == td::td_api::file::ID) {
        auto file = td::move_tl_object_as<td::td_api::file>(response);
        rzLog(RZ_LOG_INFO, "[DESCARGA] Archivo %d descargado en: %s", file_id, file->local_->path_.c_str());
    } else if (response->get_id() == td::td_api::error::ID) {
        auto err = td::move_tl_object_as<td::td_api::error>(response);
        rzLog(RZ_LOG_ERROR, "[DESCARGA] Error al descargar archivo %d: %s", file_id, err->message_.c_str());
    } else {
        rzLog(RZ_LOG_WARN, "[DESCARGA] Respuesta inesperada (%d) al descargar archivo %d", response->get_id(), file_id);
    }
}


void TelegramBot::start_file_download(int32_t file_id) {
    auto download = td::td_api::make_object<td::td_api::downloadFile>();
    download->file_id_ = file_id;
    download->priority_ = 32;  // Prioridad alta (1-32, 32 = máxima)
    download->offset_ = 0;     // Desde el inicio
    download->limit_ = 0;      // 0 = descargar todo el archivo
    download->synchronous_ = false;  // Descarga asíncrona
    
    rzLog(RZ_LOG_INFO, "Iniciando descarga de archivo %d", file_id);
    
    send_query(std::move(download), [this, file_id](auto response) 
    {
        handle_download_response(file_id, std::move(response));
    });
}

/*Handler del mensaje que llega para su procesamiento*/
void TelegramBot::handle_new_message(td::td_api::object_ptr<td::td_api::message> message) {
    //TODO AUTENTICADOR 
    if (!message) {
       rzLog(RZ_LOG_INFO, "[MSG] Mensaje null recibido");
        return;
    }
    
    if (!message->content_) {
        rzLog(RZ_LOG_INFO, "[MSG] Contenido del mensaje null");
        return;
    }

    int64_t chat_id = message->chat_id_;
    int64_t message_id = message->id_;
    std::string text = extract_message_text(message_id, message->content_.get());
    
    rzLog(RZ_LOG_INFO, "[MSG] ¡MENSAJE RECIBIDO!");
    rzLog(RZ_LOG_INFO, "[MSG]   Chat ID: %lld", (long long)chat_id);
    rzLog(RZ_LOG_INFO, "[MSG]   Message ID: %lld", (long long)message_id);
    rzLog(RZ_LOG_INFO, "[MSG]   Texto: '%s'", text.c_str());

    if (!text.empty()) {
        rzLog(RZ_LOG_INFO, "[MSG] Enviando acción de escribir...");
        send_typing_action(chat_id);
        
        std::string response = generate_response(text);
        rzLog(RZ_LOG_INFO, "[MSG] Respuesta generada: '%s'", response.c_str());
        
        send_text_message(chat_id, response);
    } else {
        rzLog(RZ_LOG_INFO, "[MSG] Mensaje sin texto, ignorando");
    }
}

void TelegramBot::handle_video(int32_t chat_id, td::td_api::messageVideo* video)
{
    rzLog(RZ_LOG_INFO, "Procesando video");
    int32_t file_id = video->video_->video_->id_;
    int32_t size_bytes = video->video_->video_->size_;
    
    rzLog(RZ_LOG_INFO, "Video detectado - File ID: %d, Tamaño: %d bytes (%.2f MB)", 
          file_id, size_bytes, size_bytes / (1024.0 * 1024.0));

    // Formato inicial del mensaje
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s, Descargando: 0%%", video->video_->file_name_);
    std::string formattedStr(buffer);

    // Crear mensaje
    auto msg = td::td_api::make_object<td::td_api::sendMessage>();
    msg->chat_id_ = chat_id;

    auto formattedText = td::td_api::make_object<td::td_api::formattedText>(
        formattedStr,
        std::vector<td::td_api::object_ptr<td::td_api::textEntity>>{} // vacío
    );

    auto inputMsg = td::td_api::make_object<td::td_api::inputMessageText>(
        std::move(formattedText),
        nullptr,   // sin link preview
        false      // clear_draft
    );

    msg->input_message_content_ = std::move(inputMsg);

    // Enviar mensaje inicial y guardar message_id para edición futura
    send_query(std::move(msg), [this, file_id, chat_id](auto response) {
        if (response->get_id() == td::td_api::message::ID) {
            auto sent_msg = td::move_tl_object_as<td::td_api::message>(response);
            downloads_[file_id] = {file_id, chat_id, sent_msg->id_, 0};
        } else if (response->get_id() == td::td_api::error::ID) {
            auto err = td::move_tl_object_as<td::td_api::error>(response);
            rzLog(RZ_LOG_ERROR, "Error enviando mensaje de progreso: %s", err->message_.c_str());
        }
    });

    // Iniciar descarga del archivo
    start_file_download(file_id);
}


std::string TelegramBot::extract_message_text(int64_t chat_id, td::td_api::MessageContent* content) {
    if (!content) {
        rzLog(RZ_LOG_INFO, "[EXTRACT] Contenido null");
        return "";
    }
    
    int content_type = content->get_id();
    rzLog(RZ_LOG_INFO,"[EXTRACT] Tipo de contenido: %d", content_type);
    

    switch (content_type)
    {
    case td::td_api::messageText::ID:
        {
            td::td_api::messageText* text_message = static_cast<td::td_api::messageText*>(content);
            if (text_message->text_)
            {
                std::string result = text_message->text_->text_;
                rzLog(RZ_LOG_INFO,"[EXTRACT] Texto extraído: '%s'", result.c_str());
                return result;
            }
            break;
        }
    case td::td_api::messageVideo::ID:
        {
            td::td_api::messageVideo* video = static_cast<td::td_api::messageVideo*>(content);
            std::string name = video->video_->file_name_;
            handle_video(chat_id, video);

            return name;
            break;
        }
    default:
        rzLog(RZ_LOG_INFO,"[EXTRACT] No se pudo extraer DEFAULT");
        break;
    }
    
    return "";
}

std::string TelegramBot::generate_response(const std::string& text) {
    if (strcmp(text.c_str(), "/start") == 0) {
        return "Bienvenido DR.";
    }
    else if (strcmp(text.c_str(), "/help") == 0) {
        return "Comandos disponibles:\n/start - Saludo \n/help - Esta ayuda \n/debug- Info de debug";
    }
    else if (strcmp(text.c_str(), "/debug") == 0) {
        return "Bot funcionando correctamente. Estado autorizado: " + 
               std::string(are_authorized_ ? "SÍ" : "NO");
    }
    else if (strstr(text.c_str(), "hola") != NULL || strstr(text.c_str(), "Hola") != NULL) {
        return "¡Hola! ¿Cómo estás?";
    }
    else {
        return "Has dicho: " + text;
    }
}

void TelegramBot::send_text_message(int64_t chat_id, const std::string& text) {
    printf("[SEND] Enviando mensaje a chat %lld: '%s'", 
    (long long)chat_id, text.c_str());
    
    auto message = td::td_api::make_object<td::td_api::sendMessage>();
    message->chat_id_ = chat_id;
    
    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
    auto formatted_text = td::td_api::make_object<td::td_api::formattedText>();
    formatted_text->text_ = text;
    content->text_ = std::move(formatted_text);
    
    message->input_message_content_ = std::move(content);
    
    send_query(std::move(message),nullptr);
    printf("[SEND] Query enviado");
}

void TelegramBot::send_typing_action(int64_t chat_id) {
    printf("[TYPING] Enviando acción de escribir a chat %lld", 
    (long long)chat_id);
    
    auto action = td::td_api::make_object<td::td_api::sendChatAction>();
    action->chat_id_ = chat_id;
    action->action_ = td::td_api::make_object<td::td_api::chatActionTyping>();
    
    send_query(std::move(action), nullptr);
}

void TelegramBot::handle_error(td::td_api::error* error) {
    if (!error) return;
    
    printf("[ERROR] Error TDLib: %s (Código: %d)", 
    error->message_.c_str(), error->code_);
    
    if (error->code_ == 401) {
        printf("[ERROR] Error de autenticación! Reiniciando...");
        need_restart_ = true;
    }
}

void TelegramBot::send_query(
    td::td_api::object_ptr<td::td_api::Function> query,
    std::function<void(td::td_api::object_ptr<td::td_api::Object>)> handler) {
    
    if (!client_manager_ || !query) {
        rzLog(RZ_LOG_ERROR, "send_query: client_manager o query null");
        return;
    }
    
    uint64_t query_id = current_query_id_++;
    
    // Si hay handler, guardarlo
    if (handler) {
        handlers_[query_id] = std::move(handler);
        rzLog(RZ_LOG_DEBUG, "send_query: Query %llu con handler", query_id);
    }
    
    client_manager_->send(client_id_, query_id, std::move(query));
}