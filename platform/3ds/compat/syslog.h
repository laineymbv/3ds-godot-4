// 3DS has no syslog; messages already go to stdout (sdmc:/3ds/godot/stdout.txt).
#pragma once
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_INFO 6
#define LOG_USER (1 << 3)
#define LOG_PID 0x01
#define LOG_CONS 0x02
static inline void openlog(const char *i, int o, int f) { (void)i; (void)o; (void)f; }
static inline void closelog(void) {}
static inline void syslog(int p, const char *fmt, ...) { (void)p; (void)fmt; }
static inline void vsyslog(int p, const char *fmt, __builtin_va_list ap) { (void)p; (void)fmt; (void)ap; }
