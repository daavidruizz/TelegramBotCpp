# Telegram Download Bot

Un bot de Telegram desarrollado en C++ usando TDLib para la descarga automática de archivos con seguimiento de progreso en tiempo real.

## Descripción

Este bot permite a usuarios autorizados enviar documentos, videos y otros archivos que serán descargados automáticamente al servidor. Durante el proceso de descarga, el bot actualiza el progreso mostrando velocidad de descarga, porcentaje completado y tiempo estimado de finalización.

## Características principales

- **Descarga automática**: Procesa documentos y videos enviados al bot
- **Seguimiento en tiempo real**: Actualiza el progreso cada 5% de descarga
- **Control de acceso**: Sistema de autorización por lista de usuarios permitidos
- **Gestión de estado**: Manejo robusto de conexiones y reintentos automáticos
- **Filtrado temporal**: Ignora mensajes enviados antes del inicio del bot

## Arquitectura

El bot está construido sobre las siguientes capas:

### Núcleo principal
- **TelegramBot**: Clase principal que coordina todas las operaciones
- **Authorization Handler**: Gestiona el proceso de autenticación con Telegram
- **Message Handler**: Procesa los mensajes entrantes y determina acciones
- **Download Manager**: Controla las descargas activas y su progreso
- **Query Handler**: Maneja las consultas asíncronas con TDLib

### Almacenamiento de datos
- **downloads_**: Mapa de descargas activas con información de progreso
- **pending_message_callbacks_**: Gestión de callbacks para obtener IDs reales de mensajes
- **user_ids_allowed**: Lista de usuarios autorizados
- **Progress tracking**: Mapas para velocidad, tiempo y porcentaje de descarga

### Interfaz TDLib
- **ClientManager**: Administrador de cliente TDLib
- **Client**: Instancia del cliente para comunicación con Telegram

## Configuración

### Parámetros requeridos
- `api_id`: ID de aplicación de Telegram
- `api_hash`: Hash de aplicación de Telegram  
- `bot_token`: Token del bot obtenido de BotFather
- `bot_db_path`: Ruta para la base de datos del bot
- `download_path`: Directorio de destino para archivos descargados
- `user_id_credentials`: Lista de IDs de usuarios autorizados (separados por comas)

### Ejemplo de inicialización
```cpp
TelegramBot bot;
bot.initialize("12345", "bot_token_here", "api_hash_here", 
               "/path/to/db", "/path/to/downloads", "123456,789012");
bot.run();
```

## Funcionamiento

### Flujo de autorización
1. El bot se inicia y conecta con TDLib
2. Envía parámetros de configuración
3. Autentica usando el token del bot
4. Queda en estado "Ready" para procesar mensajes

### Procesamiento de archivos
1. Usuario autorizado envía un documento/video
2. Bot crea entrada en el mapa de descargas
3. Inicia descarga y envía mensaje inicial
4. Actualiza progreso cada 5% de avance
5. Notifica finalización y limpia recursos

### Sistema de callbacks
- Los mensajes se envían con ID temporal
- Se almacenan callbacks para obtener el ID real
- Una vez obtenido el ID real, se pueden editar los mensajes de progreso

## Estados del bot

- **Closed**: Bot apagado o desconectado
- **WaitTdlibParameters**: Esperando configuración inicial
- **WaitPhoneNumber**: Esperando autenticación (token del bot)
- **Ready**: Bot operativo y procesando mensajes
- **LoggingOut**: Cerrando sesión
- **Restart**: Reiniciando por error de conexión

## Comandos soportados

- `/start`: Mensaje de bienvenida
- `/help`: Lista de comandos disponibles  
- `/debug`: Información de estado del bot

## Limitaciones actuales

- Solo procesa documentos y videos
- Requiere autorización previa de usuarios
- Un archivo por descarga (sin cola)
- Progreso reportado en incrementos del 5%

## Dependencias

- TDLib (Telegram Database Library)
- rzLogger (sistema de logging personalizado)
- C++17 o superior
- CMake para compilación

## Estado del proyecto

En desarrollo activo. Funcionalidades básicas implementadas y operativas. Se están refinando el manejo de errores y optimizando el rendimiento de descarga.

## Compilación

```bash
mkdir build && cd build
make
```

## Notas técnicas

- Utiliza programación asíncrona con callbacks
- Manejo robusto de reconexiones automáticas
- Logging detallado para debugging
- Gestión de memoria automática con smart pointers de TDLib