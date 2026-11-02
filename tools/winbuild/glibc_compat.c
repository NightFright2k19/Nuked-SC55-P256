/* glibc baseline of the native Linux plug-ins (issue #112, NK_TARGET=linux in target.sh).
 * With glibc 2.38 headers, g++ (which always defines _GNU_SOURCE) maps strtol, strtoul and sscanf to the C23
 * variants __isoc23_*, which exist only from glibc 2.38 on (Ubuntu 24.04, Debian 13). These local definitions
 * forward them to the old versions, so the plug-ins only need glibc 2.35 (Ubuntu 22.04, Debian 12), the same
 * baseline as the Linux patcher. The only difference of the C23 variants is the "0b" prefix for binary numbers,
 * which no code of the plug-ins parses. Compiled separately without LTO (the .symver references need that). */
#include <stdarg.h>

long strtol_old(const char*, char**, int);
unsigned long strtoul_old(const char*, char**, int);
int vsscanf_old(const char*, const char*, va_list);
__asm__(".symver strtol_old, strtol@GLIBC_2.2.5");
__asm__(".symver strtoul_old, strtoul@GLIBC_2.2.5");
__asm__(".symver vsscanf_old, __isoc99_vsscanf@GLIBC_2.7");

#define LOCAL __attribute__((visibility("hidden")))   /* bound inside the plug-in, never exported */
LOCAL long __isoc23_strtol(const char* s, char** end, int base) { return strtol_old(s, end, base); }
LOCAL unsigned long __isoc23_strtoul(const char* s, char** end, int base) { return strtoul_old(s, end, base); }
LOCAL int __isoc23_sscanf(const char* s, const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	const int n = vsscanf_old(s, format, ap);
	va_end(ap);
	return n;
}
