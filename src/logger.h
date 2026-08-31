/* logger.h: Diagnostic logging, crash reporter, and freeze watchdog for Fuse on PicoCalc
   Designed for Luckfox Lyra / PicoCalc platform
*/

#ifndef FUSE_LOGGER_H
#define FUSE_LOGGER_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the logging subsystem, rotate previous log, spawn watchdog, and install crash handlers */
int fuse_logger_init( const char *log_path );

/* Shutdown logger, terminate watchdog thread, and flush/close log file */
void fuse_logger_shutdown( void );

/* Main logging function with timestamp, subsystem tag, and formatted message */
void fuse_log( const char *module, const char *fmt, ... )
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
;

/* Log high-level user/system action and record into circular history buffer */
void fuse_log_action( const char *action, const char *details );

/* Ping the freeze-detection watchdog from main emulation loop */
void fuse_watchdog_ping( void );

/* Set the current ongoing action description for the watchdog */
void fuse_watchdog_set_action( const char *action );

/* Update watchdog pause status (so watchdog knows emulation is intentionally paused in menu) */
void fuse_watchdog_set_paused( int paused );

#ifdef __cplusplus
}
#endif

#endif /* FUSE_LOGGER_H */
