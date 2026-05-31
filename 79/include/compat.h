#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32
    #include <io.h>
    #include <direct.h>
    #define strcasecmp   _stricmp
    #define strncasecmp  _strnicmp
    #define access       _access
    #define mkdir(dir)   _mkdir(dir)
    #define localtime_r(t, res) localtime_s(res, t)
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <strings.h>
    #define mkdir(dir)   mkdir(dir, 0755)
    #define sprintf_s    snprintf
    #define strcpy_s(dst, dst_size, src)  strncpy(dst, src, dst_size)
    #define strncpy_s(dst, dst_size, src, count) strncpy(dst, src, dst_size)
    #define _TRUNCATE    ((size_t)-1)
    #define localtime_s(res, t) localtime_r(t, res)
#endif

#endif
