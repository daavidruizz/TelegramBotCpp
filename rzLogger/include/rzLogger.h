#ifndef RZ_LOGGER_H
#define RZ_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RZ_LOG_ERROR        = 1,
    RZ_LOG_WARN         = 2,
    RZ_LOG_INFO         = 3,
    RZ_LOG_DEBUG        = 4,
    RZ_LOG_DEBUG_EXTRA  = 5
} rz_logger_level_t;

// Función principal de logging
void rzLog_impl(rz_logger_level_t level, const char* file, int line, const char* func, const char* format, ...);

// Macro para uso conveniente
#define rzLog(level, format, ...) \
    rzLog_impl(level, __FILE__, __LINE__, __FUNCTION__, format, ##__VA_ARGS__)

// Macros de niveles de logging
#define rzERROR(format, ...) rzLog_impl(RZ_LOG_ERROR, __FILE__, __LINE__, __FUNCTION__, format, ##__VA_ARGS__)
#define rzWARN(format, ...)  rzLog_impl(RZ_LOG_WARN,  __FILE__, __LINE__, __FUNCTION__, format, ##__VA_ARGS__)  
#define rzINFO(format, ...)  rzLog_impl(RZ_LOG_INFO,  __FILE__, __LINE__, __FUNCTION__, format, ##__VA_ARGS__)
#define rzDEBUG(format, ...) rzLog_impl(RZ_LOG_DEBUG, __FILE__, __LINE__, __FUNCTION__, format, ##__VA_ARGS__)

// Inicialización y configuración
void rzLog_init();
void rzLog_stop();
void rzLog_set_level(rz_logger_level_t min_level); // NULL para leer de variable ENV
void rzLog_set_output(const char* filename); // NULL para consola

#ifdef __cplusplus
}
#endif

#endif // RZ_LOGGER_H
