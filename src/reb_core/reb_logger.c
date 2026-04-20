#include "reb_logger.h"
#include <stdio.h>
#include <time.h>
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static void reb_logger_write(const char *level, const char *message)
{
    #if defined(_WIN32) && !defined(__CYGWIN__)
        _mkdir("artifacts");
        _mkdir("artifacts/logs");
    #else
        mkdir("artifacts", 0777);
        mkdir("artifacts/logs", 0777);
    #endif
    

    FILE *log_file = fopen("artifacts/logs/reb.log", "a");

    if (log_file == NULL)
    {
        printf("Erro ao abrir arquivo de log!\n");
        return;
    }

    time_t rawtime;
    struct tm *info;
    char buffer[80];

    time(&rawtime);
    info = localtime(&rawtime);

    // Format: dd/mm/yyyy hh:mm:ss
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", info);

    fprintf(log_file, "[%s] [%s] %s\n", level, buffer, message);
    fclose(log_file);
}

void reb_logger_info(const char *message)
{
    reb_logger_write("INFO", message);
}

void reb_logger_warn(const char *message)
{
    reb_logger_write("WARN", message);
}

void reb_logger_error(const char *message)
{
    reb_logger_write("ERROR", message);
}