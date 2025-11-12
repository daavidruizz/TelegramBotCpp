#include "TelegramBot.h"
#include <chrono>
#include <iomanip>
#include <chrono>
#include <string>
#include "rzLogger.h"
#include <td/telegram/Log.h>
#include <filesystem>
#include <functional>
#include <cmath>


/**
 * @brief Constructor de la clase
 */
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

/**
 * @brief Destructor de la clase
 */
TelegramBot::~TelegramBot() {
    stop();
    
    if (client_manager_) {
        delete client_manager_;
        client_manager_ = nullptr;
    }
    rzLog(RZ_LOG_INFO, "TelegramBot destruido");
}

/**
 * @brief Inicializa el objeto TelegramBot con los datos necesarios de Telegram.
 * @param api_id API_ID de Telegram
 * @param bot_token TOKEN del bot de Telegram
 * @param api_hash API_HASH del bot de Telegram
 * @param bot_db_path_str Path de la DB del Bot
 * @param download_path Path de destino de descarga de archivos
 * @param user_id_credentials Lista de USERS_ID separados por comas. En caso de estar vacio no se usara esta funcionalidad.
 * @return bool
 */
bool TelegramBot::initialize(const std::string& api_id, 
                            const std::string& bot_token, 
                            const std::string& api_hash, 
                            const std::string& bot_db_path, 
                            const std::string& download_path, 
                            const std::string& user_id_credentials)
{
    api_id_ = api_id;
    api_hash_ = api_hash;
    bot_token_ = bot_token;
    path_bot_db_ = bot_db_path;
    download_path_ = download_path;

    if (!user_id_credentials.empty()) {
        std::stringstream ss(user_id_credentials);
        std::string id_str;

        while (std::getline(ss, id_str, ',')) {
            // Limpiar espacios
            id_str.erase(0, id_str.find_first_not_of(" \t\n\r"));
            id_str.erase(id_str.find_last_not_of(" \t\n\r") + 1);
            
            if (id_str.empty())
                continue;
            
            try {
                int64_t user_id = std::stoll(id_str);
                user_ids_allowed.insert(user_id);  // insert en lugar de push_back
                rzLog(RZ_LOG_INFO, "Usuario autorizado: %lld", (long long)user_id);
            } catch (const std::exception& e) {
                rzLog(RZ_LOG_ERROR, "ID inválido: '%s' - %s", id_str.c_str(), e.what());
            }
        }
        
        usingCredentials = true;
        rzLog(RZ_LOG_INFO, "Credenciales activadas: %zu usuarios", user_ids_allowed.size());
    }

    if(!path_bot_db_.empty() && !download_path_.empty())
    {
        if(!std::filesystem::exists(path_bot_db_) || !std::filesystem::exists(download_path_))
        {
            rzLog(RZ_LOG_ERROR, "[BOT] Error inicializando bot: No existe el path especificado para la DB o DOWNLOADS");
            return false;
        }
    } 
    else 
    {
        rzLog(RZ_LOG_ERROR, "[BOT] Error inicializando bot: No existe el path especificado para la DB o DOWNLOADS");
        return false;
    }

  
   rzLog(RZ_LOG_INFO, "[BOT] Inicializado con API_ID=%s, API_HASH=%s..., BOT_TOKEN=%s...", 
           api_id.c_str(), 
           api_hash.substr(0, 8).c_str(),
           bot_token.substr(0, 12).c_str());

    bot_start_time_ = std::time(nullptr);
    return true;
}

/**
 * @brief Creación del Thread de ejecución para TelegramBot. Llama a main_loop().
 */
void TelegramBot::run() {
    if (running_) {
       rzLog(RZ_LOG_INFO, "[BOT] Run");
        return;
    }
    
    running_ = true;
    worker_thread_ = std::thread(&TelegramBot::main_loop, this);
    
    // FORZAR el inicio enviando parámetros inmediatamente
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

/**
 * @brief Finalización de TelegramBot. Elimina el Hilo.
 */
void TelegramBot::stop() {
    if (!running_) return;
    
    rzLog(RZ_LOG_INFO, "[BOT] Deteniendo...");
    running_ = false;
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    rzLog(RZ_LOG_INFO, "[BOT] Detenido completamente");
}

/**
 * @brief Loop principal del objeto TelegramBot
 */
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
            process_response(response.request_id, std::move(response.object));
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

/**
 * @brief Handler para la actualización de archivos durante la descarga de estos.
 * @param file Archivo que se actualiza.
 */
void TelegramBot::handle_file_update(td::td_api::object_ptr<td::td_api::file>&& file) {
    
    int32_t file_id = file->id_;
    int64_t downloaded = file->local_->downloaded_size_;
    int64_t total = file->size_;
    
    DownloadMap::iterator it = downloads_.find(file_id); 
    
    if (it == downloads_.end()) {
        //TODO VER POR QUE AL TERMINAR VA POR AQUI
        rzLog(RZ_LOG_DEBUG, "Archivo %d no encontrado en downloads activos (posible update post-completado).", file_id);
        return;
    }
    
    //bool is_downloading = file->local_->is_downloading_active_;
    bool is_complete = file->local_->is_downloading_completed_;
    
    int64_t chat_id = it->second.chat_id;
    int64_t message_id = it->second.message_id;
    
    if (is_complete) 
    {    
        std::time_t finish = time(nullptr); 
        double diff = difftime(finish, downloads_[file_id].start_time);
        int minutes = static_cast<int>(diff / 60);
        std::string mensaje = "Archivo completado!\nTiempo de descarga: " +
        std::to_string(minutes) + " min";
        
        send_text_message(chat_id, mensaje, nullptr);
        downloads_.erase(it); // ya no necesitamos el mensaje de progreso
        
        // Limpiar estado
        last_downloaded_.erase(file_id);
        last_time_.erase(file_id);
        last_progress_reported_.erase(file_id);
        rzLog(RZ_LOG_INFO, mensaje.c_str());
        return;
    }
    
    // IMPORTANTE: Solo editar si tenemos el ID real
    if (message_id == -1) {
        rzLog(RZ_LOG_DEBUG, "Archivo %d: Esperando ID real del mensaje antes de editar...", file_id);
        return;  // Salir, esperamos siguiente updateFile
    }

    if (total <= 0)
    {
        rzLog(RZ_LOG_ERROR, "Division por 0");
        return; // Evitar división por cero  
    } 
        

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


    char buffer[256];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "Archivo %d: %d%% (%ld/%ld bytes) | Velocidad: %.2f MB/s | ETA: %d:%02d",
        file_id, progress_5, downloaded, total, speed_mbps, eta_min, eta_sec_rem
    );

    rzLog(RZ_LOG_DEBUG, buffer);
    
    std::string editText = downloads_[file_id].original_text + "\n\nDescargando " + it->second.file.fileName + "\nExtension: " + it->second.file.extension;

    send_edited_message(chat_id, message_id, editText + "\n" + buffer);
}

/**
 * @brief Obtiene el user ID de un objeto MessageSender
 * 
 * @param sender MessageSender
 * 
 * @return Devuelve el sender ID
 */
int64_t TelegramBot::get_sender_user_id(td::td_api::MessageSender* sender)
{
    if (!sender) {
        return -1;
    }
    
    if (sender->get_id() == td::td_api::messageSenderUser::ID) {
        auto sender_user = static_cast<td::td_api::messageSenderUser*>(sender);
        return sender_user->user_id_;
    }
    
    // No es un usuario (es un canal/grupo)
    return -1;  
}


/**
 * @brief Funcion que comprueba si el mensaje esta permitido por el Bot
 * 
 * @param user_id
 * 
 * @return True si el USER_ID esta permitido. -1 en otro caso.
 */
bool TelegramBot::is_message_allowed(int64_t user_id) {
    // Sender no es usuario
    if (user_id == -1) {
        rzLog(RZ_LOG_WARN, "Mensaje de sender no-usuario. Ignorando.");
        return false;
    }
    
    // Si no usamos credenciales, permitir todos
    if (!usingCredentials) {
        return true;
    }
    
    // Verificar lista de autorizados
    bool is_allowed = user_ids_allowed.count(user_id) > 0;
    
    if (!is_allowed) {
        rzLog(RZ_LOG_WARN, "Usuario %lld NO autorizado.", (long long)user_id);
    }
    
    return is_allowed;
}


/**
 * @brief Principal funcion de procesado de mensajes. 
 * Inicio del ciclo de proceso cuando llega un mensaje, actualizacion, etc. 
 * Dependiendo del tipo de response (ID), se procesará de una forma u otra.
 * @param response Respuesta a procesar.
 */
void TelegramBot::process_response(uint64_t query_id, td::td_api::object_ptr<td::td_api::Object>&& response) {
    if (!response) {
        rzLog(RZ_LOG_INFO, "[PROCESS] Respuesta null recibida");
        return;
    }

    // PRIMERO: Buscar handler para este query_id
    if (query_id != 0 && handlers_.find(query_id) != handlers_.end()) {
        rzLog(RZ_LOG_INFO, "EJECUTANDO CALLBACK para query_id %llu", query_id);
        handlers_[query_id](std::move(response));
        handlers_.erase(query_id);
        return; // Ya manejamos esta respuesta
    }

    int response_id = response->get_id();
    
    rzLog(RZ_LOG_DEBUG_EXTRA, "[PROCESS] Procesando respuesta tipo: %d", response_id);

    switch (response_id) {
        case td::td_api::updateOption::ID:
        {
            break; //Simplemente para que entre aqui
        }
        case td::td_api::updateAuthorizationState::ID: 
        {
            rzLog(RZ_LOG_INFO, "[PROCESS] -> updateAuthorizationState");
            auto update = td::td_api::move_object_as<td::td_api::updateAuthorizationState>(std::move(response));
            authorization_state_ = std::move(update->authorization_state_);
            handle_authorization_update();
            break;
        }
        case td::td_api::updateConnectionState::ID:
        {
            break;//Simplemente para que entre aqui
        }
        case td::td_api::updateNewMessage::ID: 
        {
            auto update = td::td_api::move_object_as<td::td_api::updateNewMessage>(std::move(response));
            
            if(update->message_->date_ < bot_start_time_)
            break; //No procesamos mensajes anteriores a la inicializacion del bot
            
            rzLog(RZ_LOG_INFO, "[PROCESS] -> updateNewMessage ¡MENSAJE RECIBIDO!");
            // Guardar valores ANTES de mover update->message_
            int64_t chat_id = update->message_->chat_id_;
            int64_t user_id = get_sender_user_id(update->message_->sender_id_.get());
            
            if (!is_message_allowed(user_id)) {
                send_text_message(chat_id, 
                                 "No estás autorizado para usar este bot.", 
                                 nullptr);
                break;
            }

            rzLog(RZ_LOG_DEBUG, "update ptr=%p update->message_=%p", (void*)update.get(), (void*)update->message_.get());
            handle_new_updateNewMessage(std::move(update->message_));
            rzLog(RZ_LOG_DEBUG, "after move update->message_=%p", (void*)update->message_.get());
            break;
        }
        case td::td_api::file::ID: 
        {
            rzLog(RZ_LOG_INFO, "[PROCESS] -> file RECIBIDO!");
            break;
        }
        case td::td_api::updateMessageEdited::ID: 
        {
            rzLog(RZ_LOG_INFO, "[PROCESS] -> updateMessageEdited");
            auto update = td::td_api::move_object_as<td::td_api::updateMessageEdited>(std::move(response));
            // Ignorar ediciones por ahora
            break;
        }
        
        case td::td_api::updateNewCallbackQuery::ID: 
        {
            auto update = td::td_api::move_object_as<td::td_api::updateNewCallbackQuery>(std::move(response));
            
            if (!is_message_allowed(update->sender_user_id_)) {
                rzLog(RZ_LOG_WARN, "Callback query de usuario no autorizado: %lld", 
                      update->sender_user_id_);
                break;
            }
            
            // TODO: Procesar callback (botones inline)
            break;
        }
        
        case td::td_api::updateNewInlineQuery::ID: 
        {
            auto update = td::td_api::move_object_as<td::td_api::updateNewInlineQuery>(std::move(response));
            
            if (!is_message_allowed(update->sender_user_id_)) {
                rzLog(RZ_LOG_WARN, "Inline query de usuario no autorizado: %lld", 
                      update->sender_user_id_);
                break;
            }
            
            // TODO: Procesar inline query
            break;
        }
        
        case td::td_api::updateNewChosenInlineResult::ID: 
        {
            auto update = td::td_api::move_object_as<td::td_api::updateNewChosenInlineResult>(std::move(response));
            
            if (!is_message_allowed(update->sender_user_id_)) {
                break;
            }
            
            // TODO: Procesar resultado inline elegido
            break;
        }
        
        case td::td_api::updateMessageContent::ID: 
        {
            rzLog(RZ_LOG_INFO, "[PROCESS] -> updateMessageContent");
            auto update = td::td_api::move_object_as<td::td_api::updateMessageContent>(std::move(response));
            // Ignorar por ahora
            break;
        }
        
        case td::td_api::error::ID: 
        {
            rzLog(RZ_LOG_INFO, "[PROCESS] -> error");
            auto error = td::td_api::move_object_as<td::td_api::error>(std::move(response));
            handle_error(error.get());
            break;
        }
        
        case td::td_api::updateFile::ID: 
        {
            auto update = td::td_api::move_object_as<td::td_api::updateFile>(std::move(response));
            handle_file_update(std::move(update->file_));
            break;
        }
        
        case td::td_api::updateMessageSendSucceeded::ID: 
        {
            rzLog(RZ_LOG_INFO, "[PROCESS] -> updateMessageSendSucceeded");
            auto update = td::td_api::move_object_as<td::td_api::updateMessageSendSucceeded>(std::move(response));
            
            int64_t temp_id = update->old_message_id_;
            int64_t real_id = update->message_->id_;
            
            rzLog(RZ_LOG_INFO, "[PROCESS] -> ID temporal %lld → ID REAL %lld", 
                  (long long)temp_id, (long long)real_id);
            
            auto it = pending_message_callbacks_.find(temp_id);
            if (it != pending_message_callbacks_.end()) {
                rzLog(RZ_LOG_DEBUG, "Ejecutando callback con ID real %lld", (long long)real_id);
                it->second(real_id);
                pending_message_callbacks_.erase(it);
            }
            
            break;
        }
        
        case td::td_api::ok::ID: {
            rzLog(RZ_LOG_DEBUG, "[PROCESS] -> ok");
            // No hacer nada, solo confirmar
            break;
        }
        
        case td::td_api::message::ID: {
            rzLog(RZ_LOG_INFO, "[PROCESS] -> message");
            auto message = td::td_api::move_object_as<td::td_api::message>(std::move(response));
            rzLog(RZ_LOG_INFO, "MENSAJE CREADO - ID: %lld, Chat: %lld", 
                  (long long)message->id_, (long long)message->chat_id_);
            break;
        }
        
        default: {
            rzLog(RZ_LOG_INFO, "[PROCESS] -> Tipo desconocido: %d (ignorando)", response_id);
            break;
        }
    }
}

/**
 * @brief Gestiona los cambios de estado de autorización del cliente TDLib.
 * 
 * Se invoca cuando TDLib notifica un cambio en el estado de autorización del bot.
 * Controla las transiciones necesarias para completar la autenticación.
 */
void TelegramBot::handle_authorization_update() {
    if (!authorization_state_) 
    {
        rzLog(RZ_LOG_INFO, "[AUTH] Estado de autorización null");
        return;
    }

    int auth_state_id = authorization_state_->get_id();
    rzLog(RZ_LOG_INFO, "[AUTH] Estado de autorización: %d", auth_state_id);
    
    if (auth_state_id == td::td_api::authorizationStateWaitTdlibParameters::ID) 
    {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Configurando parámetros TDLib...");
    }
    else if (auth_state_id == td::td_api::authorizationStateWaitPhoneNumber::ID) 
    {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Enviando BOT_TOKEN...");
        send_bot_token();
    }
    else if (auth_state_id == td::td_api::authorizationStateReady::ID) 
    {
        are_authorized_ = true;
        rzLog(RZ_LOG_INFO, "[AUTH] -> ¡¡¡BOT AUTORIZADO Y LISTO!!!");
    }
    else if (auth_state_id == td::td_api::authorizationStateClosed::ID) 
    {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Conexión cerrada, programando reinicio");
        are_authorized_ = false;
        need_restart_ = true;
    }
    else if (auth_state_id == td::td_api::authorizationStateLoggingOut::ID) 
    {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Cerrando sesión...");
        are_authorized_ = false;
    }
    else 
    {
        rzLog(RZ_LOG_INFO, "[AUTH] -> Estado desconocido: %d", auth_state_id);
    }
}

/**
 * @brief Envía los parámetros de configuración de TDLib necesarios para iniciar sesión.
 * 
 * Incluye los valores como API ID, API hash, base de datos local, y otras opciones
 * necesarias para inicializar correctamente la instancia de TDLib.
 */
void TelegramBot::send_tdlib_parameters() 
{
    rzLog(RZ_LOG_INFO, "[PARAMS] Creando parámetros TDLib...");
    
    auto query = td::td_api::make_object<td::td_api::setTdlibParameters>();
    

    query->api_id_ = atoi(api_id_.c_str());
    query->api_hash_ = api_hash_;
    query->database_directory_ = path_bot_db_;
    query->files_directory_= download_path_;
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

/**
 * @brief Envía el token del bot a TDLib para completar la autenticación.
 */
void TelegramBot::send_bot_token() 
{
    rzLog(RZ_LOG_INFO, "[TOKEN] Enviando token de autenticación...");
    
    auto auth = td::td_api::make_object<td::td_api::checkAuthenticationBotToken>();
    auth->token_ = bot_token_;
    
    rzLog(RZ_LOG_INFO, "[TOKEN] Token: %s...", bot_token_.substr(0, 12).c_str());
    
    send_query(std::move(auth), nullptr);
}

/**
 * @brief Maneja la respuesta de la descarga de archivo recibida desde TDLib.
 * 
 * @param file_id Identificador del archivo descargado.
 * @param response Objeto de respuesta devuelto por TDLib que contiene el resultado de la operacion de descarga.
 */
void TelegramBot::handle_download_response(int32_t file_id, td::td_api::object_ptr<td::td_api::Object>&& response) 
{
    if (response->get_id() == td::td_api::file::ID) 
    {
        auto file = td::move_tl_object_as<td::td_api::file>(response);
        rzLog(RZ_LOG_INFO, "[DESCARGA] Archivo %d descargado en: %s", file_id, file->local_->path_.c_str());
    } else if (response->get_id() == td::td_api::error::ID) 
    {
        auto err = td::move_tl_object_as<td::td_api::error>(response);
        rzLog(RZ_LOG_ERROR, "[DESCARGA] Error al descargar archivo %d: %s", file_id, err->message_.c_str());
    } else 
    {
        rzLog(RZ_LOG_WARN, "[DESCARGA] Respuesta inesperada (%d) al descargar archivo %d", response->get_id(), file_id);
    }
}


/**
 * @brief Inicia la descarga de un archivo especificado.
 * @param file_id Identificador del archivo a descargar.
 */
void TelegramBot::start_file_download(int32_t file_id) 
{
    auto download = td::td_api::make_object<td::td_api::downloadFile>();
    download->file_id_ = file_id;
    download->priority_ = 32;  // Prioridad alta (1-32, 32 = máxima)
    download->offset_ = 0;     // Desde el inicio
    download->limit_ = 0;      // 0 = descargar todo el archivo
    download->synchronous_ = false;  // Descarga asíncrona
    
    rzLog(RZ_LOG_INFO, "Iniciando descarga de archivo %d", file_id);
    
    std::string text = "Iniciando descarga de " + downloads_[file_id].file.fileName + "\n Extension: '" + downloads_[file_id].file.extension + "'.";

    //Actualizamos tiempo de comienzo de descarga
    downloads_[file_id].start_time = std::time(nullptr);
    downloads_[file_id].original_text = text;

    send_text_message(downloads_[file_id].chat_id, text,
    [this, file_id](int64_t msg_id)
    {
        rzLog(RZ_LOG_DEBUG, "Callback ejecutado para archivo %d, message_id: %ld", file_id, msg_id);
        
        if(msg_id != -1) 
        {
            downloads_[file_id].message_id = msg_id;
            rzLog(RZ_LOG_INFO, "Message ID actualizado para archivo %d: %ld", file_id, msg_id);
        } else 
        {
            rzLog(RZ_LOG_ERROR, "Message ID invalido recibido para archivo %d", file_id);
        }
    });

    send_query(std::move(download), [this, file_id](auto response) 
    {
        handle_download_response(file_id, std::move(response));
    });
}

/*Handler del mensaje que llega para su procesamiento*/
/**
 * @brief Procesa un nuevo mensaje recibido por el bot.
 * 
 * Handler principal para las actualizaciones entrantes del tipo message.
 * Se encarga de analizar el contenido.
 * @param message Objeto del mensaje recibido desde TDLib.
 */
void TelegramBot::handle_new_updateNewMessage(td::td_api::object_ptr<td::td_api::message>&& message) 
{
    //TODO AUTENTICADOR Y FILTRAR POR TIPO DE ARCHIVO
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
    std::string text = extract_updateNewMessage_data(chat_id, message->content_.get());
    
    rzLog(RZ_LOG_INFO, "[MSG] ¡MENSAJE RECIBIDO!");
    rzLog(RZ_LOG_INFO, "[MSG]   Chat ID: %lld", (long long)chat_id);
    rzLog(RZ_LOG_INFO, "[MSG]   Message ID: %lld", (long long)message_id);
    rzLog(RZ_LOG_INFO, "[MSG]   Texto: '%s'", text.c_str());

    if (!text.empty()) {
        //rzLog(RZ_LOG_INFO, "[MSG] Enviando acción de escribir...");
        //send_typing_action(chat_id);
    } else {
        rzLog(RZ_LOG_INFO, "[MSG] Mensaje sin texto, ignorando");
    }
}

/**
 * @brief Gestiona los mensajes de tipo Document recibidos.
 * 
 * @param chat_id ID del chat donde se recibió el video.
 * @param document Puntero al objeto messageDocument recibido.
 */
void TelegramBot::handle_document(int32_t chat_id, td::td_api::messageDocument* document)
{
    FileType file;
    std::string name = document->document_->file_name_;
    std::string caption = document->caption_->text_;
    std::string mime_type = document->document_->mime_type_;
    std::string extension = std::filesystem::path(name).extension().string();
    
    int32_t file_id = document->document_->document_->id_;
    int32_t size_bytes = document->document_->document_->size_;

    rzLog(RZ_LOG_INFO,"Documento: Nombre: '%s', Caption: '%s', Extension: '%s', Type: '%s'", 
                            name.c_str(), 
                            caption.c_str(), 
                            extension.c_str(),
                            mime_type.c_str());

    if (!caption.empty())
    {
        document->document_->file_name_ = caption;
    }

    rzLog(RZ_LOG_INFO, "Procesando documento");
    rzLog(RZ_LOG_INFO, "Documento detectado - File ID: %d, Tamaño: %d bytes (%.2f MB)", 
          file_id, size_bytes, size_bytes / (1024.0 * 1024.0));

    file.fileName = name;
    file.extension = extension;
    file.fileSize = size_bytes;
    file.mimeType = mime_type;

    downloads_[file_id] = DownloadInfo
    {
        chat_id,
        -1, //No inicializado,
        "",
        std::time(nullptr), //De momento NULL
        std::time(nullptr), //De momento NULL
        file
    };

    // Iniciar descarga del archivo
    start_file_download(file_id);
}

/**
 * @brief Gestiona los mensajes de tipo fotos recibidos.
 * 
 * @param chat_id ID del chat donde se recibió el video.
 * @param photo Puntero al objeto messagePhoto recibido.
 */
/*
void TelegramBot::handle_photo(int32_t chat_id, td::td_api::messagePhoto* photo)
{
    FileType file;
    //std::string name = photo->photo_->;
    std::string caption = photo->caption_->text_;
    std::string extension = std::filesystem::path(name).extension().string();
    
    int32_t file_id = video->video_->video_->id_;
    int32_t size_bytes = video->video_->video_->size_;

    rzLog(RZ_LOG_INFO,"Video: Nombre: '%s', Caption: '%s', Extension: '%s', Type: '%s'", 
                            name.c_str(), 
                            caption.c_str(), 
                            extension.c_str(),
                            mime_type.c_str());
    
    if (!caption.empty())
    {
        video->video_->file_name_ = caption;
    }

    rzLog(RZ_LOG_INFO, "Procesando video");
    rzLog(RZ_LOG_INFO, "Video detectado - File ID: %d, Tamaño: %d bytes (%.2f MB)", 
          file_id, size_bytes, size_bytes / (1024.0 * 1024.0));

    file.fileName = name;
    file.extension = extension;
    file.fileSize = size_bytes;
    file.mimeType = mime_type;

    downloads_[file_id] = DownloadInfo{
        chat_id,
        -1, //No inicializado,
        "",
        std::time(nullptr), //De momento NULL
        std::time(nullptr), //De momento NULL
        file
    };

    // Iniciar descarga del archivo
    start_file_download(file_id);
}*/

/**
 * @brief Gestiona los mensajes de tipo video recibidos.
 * 
 * @param chat_id ID del chat donde se recibió el video.
 * @param video Puntero al objeto messageVideo recibido.
 */
void TelegramBot::handle_video(int32_t chat_id, td::td_api::messageVideo* video)
{
    FileType file;
    std::string name = video->video_->file_name_;
    std::string caption = video->caption_->text_;
    std::string mime_type = video->video_->mime_type_;
    std::string extension = std::filesystem::path(name).extension().string();
    
    int32_t file_id = video->video_->video_->id_;
    int32_t size_bytes = video->video_->video_->size_;

    rzLog(RZ_LOG_INFO,"Video: Nombre: '%s', Caption: '%s', Extension: '%s', Type: '%s'", 
                            name.c_str(), 
                            caption.c_str(), 
                            extension.c_str(),
                            mime_type.c_str());
    
    if (!caption.empty())
    {
        video->video_->file_name_ = caption;
    }

    rzLog(RZ_LOG_INFO, "Procesando video");
    rzLog(RZ_LOG_INFO, "Video detectado - File ID: %d, Tamaño: %d bytes (%.2f MB)", 
          file_id, size_bytes, size_bytes / (1024.0 * 1024.0));

    file.fileName = name;
    file.extension = extension;
    file.fileSize = size_bytes;
    file.mimeType = mime_type;

    downloads_[file_id] = DownloadInfo{
        chat_id,
        -1, //No inicializado,
        "",
        std::time(nullptr), //De momento NULL
        std::time(nullptr), //De momento NULL
        file
    };

    // Iniciar descarga del archivo
    start_file_download(file_id);
}

/**
 * @brief Extrae datos relevantes de un objeto MessageContent.
 * 
 * @param chat_id Identificador del chat asociado.
 * @param content Puntero al contenido del mensaje.
 * 
 * @return Cadena con el texto o información extraída del mensaje.
 */
std::string TelegramBot::extract_updateNewMessage_data(int64_t chat_id, td::td_api::MessageContent* content) {
    
    std::string null_str = "";

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
                    rzLog(RZ_LOG_DEBUG_EXTRA,"[EXTRACT] Texto extraído: '%s'", result.c_str());
                    
                    std::string response = generate_response(result);
                    rzLog(RZ_LOG_DEBUG_EXTRA, "[MSG] Respuesta generada: '%s'", response.c_str());
                    
                    send_text_message(chat_id, response, 
                    [](int64_t msg_id)
                    {
                        if(msg_id != -1)
                            rzLog(RZ_LOG_DEBUG_EXTRA, "MSG ID: %d", msg_id);
                    });
                }
                break;
            }
        case td::td_api::messageVideo::ID:
            {
                td::td_api::messageVideo* video = static_cast<td::td_api::messageVideo*>(content);
                handle_video(chat_id, video);
                return null_str;
                break;
            }
        case td::td_api::messagePhoto::ID:
            {
                //td::td_api::messagePhoto* photo = static_cast<td::td_api::messagePhoto*>(content);
                break;
            }
        case td::td_api::messageDocument::ID:
            {
                td::td_api::messageDocument* document = static_cast<td::td_api::messageDocument*>(content);
                handle_document(chat_id, document);
                break;
            }
        default:
            rzLog(RZ_LOG_WARN,"[EXTRACT] No se pudo extraer DEFAULT");
            break;
    }
    
    return "";
}

/**
 * @brief Genera una respuesta automática en función del texto recibido.
 * 
 * @param text Texto recibido o procesado.
 * 
 * @return Cadena con la respuesta generada.
 */
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

/**
 * @brief Edita un mensaje existente.
 * 
 * @param chat_id Identificador del chat destino.
 * @param message_id Identificador del mensaje que se desea editar.
 * @param text Texto nuevo que reemplazará el contenido anterior.
 */
void TelegramBot::send_edited_message(int64_t chat_id, int64_t message_id, const std::string& text)
{
    rzLog(RZ_LOG_INFO, "[SEND] Editando mensaje %lld en chat %lld: '%s'", 
        (long long)message_id, (long long)chat_id, text.c_str());
    
    auto edit_message = td::td_api::make_object<td::td_api::editMessageText>();
    edit_message->chat_id_ = chat_id;
    edit_message->message_id_ = message_id;

    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
    auto formatted_text = td::td_api::make_object<td::td_api::formattedText>();
    formatted_text->text_ = text;

    // Log del texto que se va a establecer
    rzLog(RZ_LOG_DEBUG, "[SEND] Editando mensaje %lld con texto: '%s'", (long long)message_id, text.c_str());

    content->text_ = std::move(formatted_text);
    edit_message->input_message_content_ = std::move(content);

    send_query(std::move(edit_message), [this, chat_id, message_id](td::td_api::object_ptr<td::td_api::Object> object) {
        if (object && object->get_id() == td::td_api::error::ID) {
            auto error = td::td_api::move_object_as<td::td_api::error>(std::move(object));
            rzLog(RZ_LOG_ERROR, "[SEND] Error al editar mensaje %lld en chat %lld: %s", 
                (long long)message_id, (long long)chat_id, error->message_.c_str());
        } else {
            rzLog(RZ_LOG_DEBUG, "[SEND] Mensaje %lld editado exitosamente en chat %lld", 
                (long long)message_id, (long long)chat_id);
        }
    });
}

/**
 * @brief Envía un mensaje de texto al chat especificado.
 * 
 * @param chat_id Identificador del chat destino.
 * @param text Contenido textual del mensaje.
 * @param callback Función callback opcional que recibe el ID del mensaje enviado.
 */
void TelegramBot::send_text_message(int64_t chat_id, const std::string& text,
                                     std::function<void(int64_t message_id)> callback) 
{
    rzLog(RZ_LOG_INFO,"[SEND] Enviando mensaje a chat %lld: '%s'", 
    (long long)chat_id, text.c_str());
    
    auto message = td::td_api::make_object<td::td_api::sendMessage>();
    message->chat_id_ = chat_id;

    auto content = td::td_api::make_object<td::td_api::inputMessageText>();
    auto formatted_text = td::td_api::make_object<td::td_api::formattedText>();
    formatted_text->text_ = text;
    content->text_ = std::move(formatted_text);
    
    message->input_message_content_ = std::move(content);
    
    if(callback != nullptr)
    {
        send_query(std::move(message), [this, callback](td::td_api::object_ptr<td::td_api::Object> object) {
            if (object && object->get_id() == td::td_api::message::ID) {
                auto message_obj = td::td_api::move_object_as<td::td_api::message>(std::move(object));
                int64_t message_id = message_obj->id_; 
                int64_t temp_id = message_obj->id_;  // Este es el ID TEMPORAL
                rzLog(RZ_LOG_INFO, "[SEND] Mensaje enviado con ID TEMPORAL: %lld", (long long)message_id);
                
                // Guardar callback para cuando llegue el ID real
                pending_message_callbacks_[temp_id] = callback;
            } 
            else if (object && object->get_id() == td::td_api::error::ID) {
                auto error = td::td_api::move_object_as<td::td_api::error>(std::move(object));
                rzLog(RZ_LOG_ERROR, "[SEND] Error al enviar mensaje: %s", error->message_.c_str());
                
                if (callback) {
                    callback(-1);
                }
            }
            else {
                rzLog(RZ_LOG_WARN, "[SEND] Respuesta inesperada al enviar mensaje");
                if (callback) {
                    callback(-1);
                }
            }
        });
    } 
    else 
    {
        send_query(std::move(message), nullptr);
    }
    rzLog(RZ_LOG_INFO,"[SEND] Query enviado");
}

void TelegramBot::send_typing_action(int64_t chat_id) 
{
    rzLog(RZ_LOG_INFO,"[TYPING] Enviando acción de escribir a chat %lld", 
    (long long)chat_id);
    
    auto action = td::td_api::make_object<td::td_api::sendChatAction>();
    action->chat_id_ = chat_id;
    action->action_ = td::td_api::make_object<td::td_api::chatActionTyping>();
    
    send_query(std::move(action),nullptr);
}


/**
 * @brief Maneja los errores devueltos por TDLib.
 * Registra o actúa según el tipo de error recibido en la respuesta TDLib.
 * 
 * @param error Puntero al objeto de error recibido.
 */
void TelegramBot::handle_error(td::td_api::error* error) {
    if (!error) return;
    
    rzLog(RZ_LOG_ERROR,"TDLib: %s (Código: %d)", 
    error->message_.c_str(), error->code_);
    
    if (error->code_ == 401) {
        rzLog(RZ_LOG_ERROR,"Error de autenticación! Reiniciando...");
        need_restart_ = true;
    }
}


/**
 * @brief Envía una consulta genérica a TDLib y asigna un handler para su respuesta.
 * 
 * @param query Objeto que representa la función o acción a ejecutar en TDLib.
 * 
 * @param handler Función callback que recibe la respuesta de TDLib.
 */
void TelegramBot::send_query(
    td::td_api::object_ptr<td::td_api::Function>&& query,
    std::function<void(td::td_api::object_ptr<td::td_api::Object>)>&& handler) {
    
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