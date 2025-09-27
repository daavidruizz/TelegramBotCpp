#include "TelegramBot.h"
#include <chrono>
#include <iomanip>
#include "rzLogger.h"

// Función para timestamp
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&time_t), "%H:%M:%S") << "] ";
    return ss.str();
}

TelegramBot::TelegramBot() {
    client_manager_ = new td::ClientManager();
    client_id_ = client_manager_->create_client_id();
    
    authorization_state_ = td::td_api::make_object<td::td_api::authorizationStateClosed>();
    
    running_ = false;
    are_authorized_ = false;
    need_restart_ = false;
    current_query_id_ = 1;
    // 0 = solo errores críticos
    rzLog(RZ_LOG_INFO, "%s[BOT] Constructor completado\n", get_timestamp().c_str());
    fflush(stdout);
}

TelegramBot::~TelegramBot() {
    stop();
    
    if (client_manager_) {
        delete client_manager_;
        client_manager_ = nullptr;
    }
    rzLog(RZ_LOG_INFO, "%s[BOT] Destructor completado\n", get_timestamp().c_str());
}

bool TelegramBot::initialize(const std::string& api_id, const std::string& bot_token, const std::string& api_hash) {
    api_id_ = api_id;
    api_hash_ = api_hash;
    bot_token_ = bot_token;
    
   rzLog(RZ_LOG_INFO, "%s[BOT] Inicializado con API_ID=%s, API_HASH=%s..., BOT_TOKEN=%s...\n", 
           get_timestamp().c_str(),
           api_id.c_str(), 
           api_hash.substr(0, 8).c_str(),
           bot_token.substr(0, 12).c_str());
    return true;
}

void TelegramBot::run() {
    if (running_) {
       rzLog(RZ_LOG_INFO, "%s[BOT] Ya está ejecutándose\n", get_timestamp().c_str());
        return;
    }
    
    running_ = true;
    worker_thread_ = std::thread(&TelegramBot::main_loop, this);
    rzLog(RZ_LOG_INFO, "%s[BOT] Hilo de trabajo iniciado\n", get_timestamp().c_str());
    
    // FORZAR el inicio enviando parámetros inmediatamente
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    rzLog(RZ_LOG_INFO, "%s[BOT] Enviando parámetros iniciales...\n", get_timestamp().c_str());
    send_tdlib_parameters();
}

void TelegramBot::stop() {
    if (!running_) return;
    
    rzLog(RZ_LOG_INFO, "%s[BOT] Deteniendo...\n", get_timestamp().c_str());
    running_ = false;
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    rzLog(RZ_LOG_INFO, "%s[BOT] Detenido completamente\n", get_timestamp().c_str());
}

void TelegramBot::main_loop() {
    rzLog(RZ_LOG_INFO, "%s[LOOP] Bucle principal iniciado\n", get_timestamp().c_str());
    fflush(stdout);
    
    // Enviar parámetros iniciales inmediatamente
    bool params_sent = false;
    
    while (running_) {
        if (need_restart_) {
            rzLog(RZ_LOG_INFO, "%s[LOOP] Reiniciando cliente...\n", get_timestamp().c_str());
            
            if (client_manager_) {
                delete client_manager_;
            }
            
            client_manager_ = new td::ClientManager();
            client_id_ = client_manager_->create_client_id();
            
            need_restart_ = false;
            are_authorized_ = false;
            params_sent = false; // Reset para reenviar parámetros
            rzLog(RZ_LOG_INFO, "%s[LOOP] Cliente reiniciado con ID: %d\n", get_timestamp().c_str(), client_id_);
        }
        
        // Enviar parámetros en la primera iteración
        if (!params_sent) {
            rzLog(RZ_LOG_INFO, "%s[LOOP] Enviando parámetros TDLib por primera vez...\n", get_timestamp().c_str());
            send_tdlib_parameters();
            params_sent = true;
        }
        
        if (!client_manager_) {
            rzLog(RZ_LOG_INFO, "%s[LOOP] ERROR: client_manager_ es null!\n", get_timestamp().c_str());
            continue;
        }
        
        // Recibir respuesta con timeout
        auto response = client_manager_->receive(1.0);
        
        if (response.object) {
            rzLog(RZ_LOG_INFO, "%s[LOOP] Respuesta recibida, tipo: %d\n", get_timestamp().c_str(), response.object->get_id());
            fflush(stdout);
            process_response(std::move(response.object));
        } else {
            // Mostrar que estamos esperando (cada 10 iteraciones para no spam)
            static int wait_counter = 0;
            if (++wait_counter % 10 == 0) {
                rzLog(RZ_LOG_INFO, "%s[LOOP] Esperando respuestas... (autorizado: %s)\n", 
                       get_timestamp().c_str(), are_authorized_ ? "SÍ" : "NO");
                fflush(stdout);
            }
        }
    }
    
    rzLog(RZ_LOG_INFO, "%s[LOOP] Bucle principal terminado\n", get_timestamp().c_str());
}

void TelegramBot::process_response(td::td_api::object_ptr<td::td_api::Object> response) {
    if (!response) {
        rzLog(RZ_LOG_INFO, "%s[PROCESS] Respuesta null recibida\n", get_timestamp().c_str());
        return;
    }

    int response_id = response->get_id();
    rzLog(RZ_LOG_INFO, "%s[PROCESS] Procesando respuesta tipo: %d\n", get_timestamp().c_str(), response_id);
    
    if (response_id == td::td_api::updateAuthorizationState::ID) {
        rzLog(RZ_LOG_INFO, "%s[PROCESS] -> Es updateAuthorizationState\n", get_timestamp().c_str());
        auto update = td::td_api::move_object_as<td::td_api::updateAuthorizationState>(response);
        authorization_state_ = std::move(update->authorization_state_);
        handle_authorization_update();
    }
    else if (response_id == td::td_api::updateNewMessage::ID) {
        rzLog(RZ_LOG_INFO, "%s[PROCESS] -> Es updateNewMessage ¡MENSAJE RECIBIDO!\n", get_timestamp().c_str());
        auto update = td::td_api::move_object_as<td::td_api::updateNewMessage>(response);
        handle_new_message(std::move(update->message_));
    }
    else if (response_id == td::td_api::error::ID) {
        rzLog(RZ_LOG_INFO, "%s[PROCESS] -> Es error\n", get_timestamp().c_str());
        auto error = td::td_api::move_object_as<td::td_api::error>(response);
        handle_error(error.get());
    }
    else {
       rzLog(RZ_LOG_INFO, "%s[PROCESS] -> Tipo desconocido: %d (ignorando)\n", get_timestamp().c_str(), response_id);
    }
}

void TelegramBot::handle_authorization_update() {
    if (!authorization_state_) {
        rzLog(RZ_LOG_INFO, "%s[AUTH] Estado de autorización null\n", get_timestamp().c_str());
        return;
    }

    int auth_state_id = authorization_state_->get_id();
    rzLog(RZ_LOG_INFO, "%s[AUTH] Estado de autorización: %d\n", get_timestamp().c_str(), auth_state_id);
    
    if (auth_state_id == td::td_api::authorizationStateWaitTdlibParameters::ID) {
        rzLog(RZ_LOG_INFO, "%s[AUTH] -> Configurando parámetros TDLib...\n", get_timestamp().c_str());
        send_tdlib_parameters();
    }
    else if (auth_state_id == td::td_api::authorizationStateWaitPhoneNumber::ID) {
        rzLog(RZ_LOG_INFO, "%s[AUTH] -> Enviando token de bot...\n", get_timestamp().c_str());
        send_bot_token();
    }
    else if (auth_state_id == td::td_api::authorizationStateReady::ID) {
        are_authorized_ = true;
        rzLog(RZ_LOG_INFO, "%s[AUTH] -> ¡¡¡BOT AUTORIZADO Y LISTO!!!\n", get_timestamp().c_str());
    }
    else if (auth_state_id == td::td_api::authorizationStateClosed::ID) {
        rzLog(RZ_LOG_INFO, "%s[AUTH] -> Conexión cerrada, programando reinicio\n", get_timestamp().c_str());
        are_authorized_ = false;
        need_restart_ = true;
    }
    else if (auth_state_id == td::td_api::authorizationStateLoggingOut::ID) {
        rzLog(RZ_LOG_INFO, "%s[AUTH] -> Cerrando sesión...\n", get_timestamp().c_str());
        are_authorized_ = false;
    }
    else {
        rzLog(RZ_LOG_INFO, "%s[AUTH] -> Estado desconocido: %d\n", get_timestamp().c_str(), auth_state_id);
    }
}

void TelegramBot::send_tdlib_parameters() {
    rzLog(RZ_LOG_INFO, "%s[PARAMS] Creando parámetros TDLib...\n", get_timestamp().c_str());
    
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
    
    rzLog(RZ_LOG_INFO, "%s[PARAMS] Enviando setTdlibParameters con api_id=%d\n", 
           get_timestamp().c_str(), query->api_id_);
    
    send_query(std::move(query));
}

void TelegramBot::send_bot_token() {
    rzLog(RZ_LOG_INFO, "%s[TOKEN] Enviando token de autenticación...\n", get_timestamp().c_str());
    
    auto auth = td::td_api::make_object<td::td_api::checkAuthenticationBotToken>();
    auth->token_ = bot_token_;
    
    rzLog(RZ_LOG_INFO, "%s[TOKEN] Token: %s...\n", get_timestamp().c_str(), bot_token_.substr(0, 12).c_str());
    
    send_query(std::move(auth));
}

void TelegramBot::handle_new_message(td::td_api::object_ptr<td::td_api::message> message) {
    if (!message) {
       rzLog(RZ_LOG_INFO, "%s[MSG] Mensaje null recibido\n", get_timestamp().c_str());
        return;
    }
    
    if (!message->content_) {
        rzLog(RZ_LOG_INFO, "%s[MSG] Contenido del mensaje null\n", get_timestamp().c_str());
        return;
    }

    int64_t chat_id = message->chat_id_;
    int64_t message_id = message->id_;
    std::string text = extract_message_text(message->content_.get());

    rzLog(RZ_LOG_INFO, "%s[MSG] ¡MENSAJE RECIBIDO!\n", get_timestamp().c_str());
    rzLog(RZ_LOG_INFO, "%s[MSG]   Chat ID: %lld\n", get_timestamp().c_str(), (long long)chat_id);
    rzLog(RZ_LOG_INFO, "%s[MSG]   Message ID: %lld\n", get_timestamp().c_str(), (long long)message_id);
    rzLog(RZ_LOG_INFO, "%s[MSG]   Texto: '%s'\n", get_timestamp().c_str(), text.c_str());

    if (!text.empty()) {
        rzLog(RZ_LOG_INFO, "%s[MSG] Enviando acción de escribir...\n", get_timestamp().c_str());
        send_typing_action(chat_id);
        
        std::string response = generate_response(text);
        rzLog(RZ_LOG_INFO, "%s[MSG] Respuesta generada: '%s'\n", get_timestamp().c_str(), response.c_str());
        
        send_text_message(chat_id, response);
    } else {
        rzLog(RZ_LOG_INFO, "%s[MSG] Mensaje sin texto, ignorando\n", get_timestamp().c_str());
    }
}

std::string TelegramBot::extract_message_text(td::td_api::MessageContent* content) {
    if (!content) {
        rzLog(RZ_LOG_INFO, "%s[EXTRACT] Contenido null\n", get_timestamp().c_str());
        return "";
    }
    
    int content_type = content->get_id();
    printf("%s[EXTRACT] Tipo de contenido: %d\n", get_timestamp().c_str(), content_type);
    
    if (content_type == td::td_api::messageText::ID) {
        td::td_api::messageText* text_message = static_cast<td::td_api::messageText*>(content);
        if (text_message->text_) {
            std::string result = text_message->text_->text_;
            printf("%s[EXTRACT] Texto extraído: '%s'\n", get_timestamp().c_str(), result.c_str());
            return result;
        }
    }
    
    printf("%s[EXTRACT] No se pudo extraer texto\n", get_timestamp().c_str());
    return "";
}

std::string TelegramBot::generate_response(const std::string& text) {
    if (strcmp(text.c_str(), "/start") == 0) {
        return "¡Hola! Soy un bot simple de Telegram con logs de debug.";
    }
    else if (strcmp(text.c_str(), "/help") == 0) {
        return "Comandos disponibles:\n/start - Saludo\n/help - Esta ayuda\n/debug - Info de debug";
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
    printf("%s[SEND] Enviando mensaje a chat %lld: '%s'\n", 
           get_timestamp().c_str(), (long long)chat_id, text.c_str());
    
    auto message = td::td_api::make_object<td::td_api::sendMessage>();
    message->chat_id_ = chat_id;
    
    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
    auto formatted_text = td::td_api::make_object<td::td_api::formattedText>();
    formatted_text->text_ = text;
    content->text_ = std::move(formatted_text);
    
    message->input_message_content_ = std::move(content);
    
    send_query(std::move(message));
    printf("%s[SEND] Query enviado\n", get_timestamp().c_str());
}

void TelegramBot::send_typing_action(int64_t chat_id) {
    printf("%s[TYPING] Enviando acción de escribir a chat %lld\n", 
           get_timestamp().c_str(), (long long)chat_id);
    
    auto action = td::td_api::make_object<td::td_api::sendChatAction>();
    action->chat_id_ = chat_id;
    action->action_ = td::td_api::make_object<td::td_api::chatActionTyping>();
    
    send_query(std::move(action));
}

void TelegramBot::handle_error(td::td_api::error* error) {
    if (!error) return;
    
    printf("%s[ERROR] Error TDLib: %s (Código: %d)\n", 
           get_timestamp().c_str(), error->message_.c_str(), error->code_);
    
    if (error->code_ == 401) {
        printf("%s[ERROR] Error de autenticación! Reiniciando...\n", get_timestamp().c_str());
        need_restart_ = true;
    }
}

void TelegramBot::send_query(td::td_api::object_ptr<td::td_api::Function> query) {
    if (!client_manager_ || !query) {
        printf("%s[QUERY] Error: client_manager o query null\n", get_timestamp().c_str());
        return;
    }
    
    uint64_t query_id = current_query_id_++;
    printf("%s[QUERY] Enviando query ID %llu, tipo: %d\n", 
           get_timestamp().c_str(), (unsigned long long)query_id, query->get_id());
    
    client_manager_->send(client_id_, query_id, std::move(query));
}