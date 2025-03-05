#ifndef XPATH_H
#define XPATH_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#endif

#ifdef __linux__
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#endif

#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define XP_NORMAL          0x1
#define XP_DIRECTORY       0x2
#define XP_HIDDEN          0x4
#define XP_READONLY        0x8
#define XP_SYSTEM          0x10
#define XP_EXECUTABLE      0x20

typedef struct {
    unsigned char *data;
    int count;
} xp_path;

typedef struct {
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
    uint32_t milliseconds;
} xp_time;

typedef struct {
    char *name;
    uint64_t bytes;
    uint32_t attributes;
    uint64_t time;
} xp_file;

typedef struct {
    xp_path path;
    xp_file *files;
    int file_count;
    // int file_cap;
} xp_directory;

#ifdef _WIN32
bool xp_path_relative(xp_path path) {
    assert(path.count > 0);
    assert(path.data);

    if (path.data[0] == '~') {
        return false; 
    }
    return PathIsRelativeA((LPSTR)path.data);
}
#elif defined(__linux__)
bool xp_path_relative(xp_path path) {
    assert(path.count > 0);
    assert(path.data);

    if (path.data[0] == '~' || path.data[0] == '/') {
        return false;
    }
    return true;
}
#endif


void xp_append(xp_path *path, char *str) {
    assert(path->data);
    int len = path->count + (int)strlen(str);
    char last = path->data[path->count - 1];
    if (last != '\\' && last != '/') {
        len++;
    }
    char *ptr = (char *)malloc(len + 1);
    strncpy(ptr, (char *)path->data, path->count);
    if (last != '\\' && last != '/') {
        strcat(ptr, "/");
    }
    ptr[path->count] = '\0';
    strcat(ptr, str);

    free(path->data);
    path->data = (unsigned char *)ptr;
    path->count = len;
}

void xp_path_free(xp_path *path) {
    if (path->data) free(path->data);
    path->data = NULL;
    path->count = 0;
}

inline void xp_directory_free(xp_directory *directory) {
    xp_path_free(&directory->path);
    if (directory->files) {
        free(directory->files);
    }
    memset(&directory->path, 0, sizeof(xp_path));
    directory->files = NULL;
    directory->file_count = 0;
}

xp_path xp_path_new(char *file_name) {
    xp_path path;
    int len = (int)strlen(file_name);
    path.data = (unsigned char *)malloc(len + 1);
    strcpy((char *)path.data, file_name);
    path.count = len;
    return path;
}

xp_path xp_path_copy(xp_path path) {
    xp_path copy;
    copy.data = (unsigned char *)malloc(path.count + 1);
    strncpy((char *)copy.data, (char *)path.data, path.count + 1);
    copy.count = path.count;
    return copy;
}

#ifdef _WIN32
xp_path xp_get_home_path() {
    char buffer[MAX_PATH];
    GetEnvironmentVariableA("USERPROFILE", buffer, MAX_PATH);
    strcat(buffer, "/");
    xp_path home = xp_path_new(buffer);
    return home;
}
#elif defined(__linux__)
xp_path xp_get_home_path() {
    char *home_path = NULL;
    if ((home_path = getenv("HOME")) == NULL) {
        // home_path = getpwuid(getuid())->pw_dir;
    }
    xp_path home = {home_path, strlen(home_path)};
    return home;
}
#endif

void xp_file_push(xp_directory *directory, xp_file file) {
    assert(directory);
    directory->files = (xp_file *)realloc(directory->files, sizeof(xp_file) * (directory->file_count + 1));
    directory->files[directory->file_count++] = file;
}

void xp_replace_slashes(xp_path path) {
    for (int i = 0; i < path.count; i++) {
        if (path.data[i] == '\\')
            path.data[i] = '/';
    }
}

xp_path xp_parent_path(xp_path path) {
    char *ptr = strrchr((char *)path.data, '/');
    assert(ptr != NULL);
    size_t len = ptr - (char *)path.data;

    xp_path parent = {0};
    parent.data = (unsigned char *)malloc(len + 2);
    strncpy((char *)parent.data, (char *)path.data, len);
    parent.data[len] = '/';
    parent.data[len + 1] = '\0';
    parent.count = (int)len + 1;
    return parent;
}

#ifdef _WIN32
xp_path xp_current_path() {
    DWORD length = GetCurrentDirectory(0, NULL);
    char *str = (char *)malloc(length + 1);
    DWORD ret = GetCurrentDirectory(length, str);
    str[ret] = '/';
    str[ret + 1] = '\0';
    xp_path path = {(unsigned char *)str, (int)ret};
    xp_replace_slashes(path);
    return path;
}
#elif defined(__linux__)
xp_path xp_current_path() {
    char *str = getcwd(NULL, 0);
    xp_path path = {str, strlen(str)};
    return path;
}
#endif


xp_path xp_substr(xp_path path, int start, int count) {
    if (count > path.count - start) count = path.count - start;
    xp_path sub_path;
    sub_path.data = (unsigned char *)malloc(count + 1);
    sub_path.count = count;
    strncpy((char *)sub_path.data, (char *)path.data + start, count);
    sub_path.data[count] = '\0';
    return sub_path;
}

void xp_normalize(xp_path *path) {
    assert(path);
    assert(path->count > 0);
    // NOTE: replace '~' home directory
    // Consider only replacing it for internal uses when calling different OS APIs
    // but keeping the squiggle for everything else
    if (path->data[0] == '~') {
        xp_path new_path = xp_get_home_path();
        xp_path rest = xp_substr(*path, 1, path->count - 1);
        xp_append(&new_path, (char *)rest.data);
        xp_path_free(path);
        xp_path_free(&rest);
        *path = new_path;
    }
    xp_replace_slashes(*path);
}

#if defined(_WIN32)
xp_path xp_fullpath(xp_path path) {
    assert(path.count > 0);
    DWORD n = GetFullPathNameA((char *)path.data, 0, NULL, NULL);
    xp_path full_path;
    full_path.data = (unsigned char *)malloc(n);
    n = GetFullPathNameA((char *)path.data, n, (char *)full_path.data, NULL);
    full_path.count = (int)strlen((char *)full_path.data);
    xp_replace_slashes(full_path);
    return full_path;
}
#elif defined(__linux__)
xp_path xp_fullpath(xp_path path) {
    assert(path.count > 0);
    xp_path full_path = path;
    char *ptr = realpath(path.data, NULL);
    if (ptr) {
        full_path.data = ptr;
        full_path.count = strlen(ptr);
    } else {
        // TODO: realpath error
    }
    return full_path;
}
#endif

#if defined(_WIN32)
bool xp_directory_new(xp_path path, xp_directory *directory) {
    xp_normalize(&path);
    memset(directory, 0, sizeof(xp_directory));
    directory->path = xp_fullpath(path);
    
    char *find_path = (char *)malloc(path.count + strlen("/*") + 1);
    memset(find_path, 0, path.count + strlen("/*") + 1);
    strncpy(find_path, (char *)path.data, path.count);
    strcat(find_path, "/*");

    WIN32_FIND_DATAA find_data = {0};
    HANDLE find_handle = FindFirstFileA(find_path, &find_data);
    free(find_path);
    if (find_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        // fprintf(stderr, "FindFirstFile failed (%d)\n", err);
        return false;
    }

    do {
        xp_file file = {0};

        uint64_t bytes = (find_data.nFileSizeHigh * (MAXDWORD+1)) + find_data.nFileSizeLow;
        char *file_name = (char *)malloc(strlen(find_data.cFileName) + 1);
        strcpy(file_name, find_data.cFileName);
        DWORD file_attributes = find_data.dwFileAttributes;
        uint32_t attributes = 0;
        
        file.time = ((uint64_t)find_data.ftLastWriteTime.dwHighDateTime << 32) | (find_data.ftLastWriteTime.dwLowDateTime);

        DWORD dw;
        if (GetBinaryTypeA(find_data.cFileName, &dw)) {
            attributes |= XP_EXECUTABLE;
        }

        if (file_attributes & FILE_ATTRIBUTE_DIRECTORY) {
            attributes |= XP_DIRECTORY;
        }
        if (file_attributes & FILE_ATTRIBUTE_READONLY) {
            attributes |= XP_READONLY;
        }
        if (file_attributes & FILE_ATTRIBUTE_NORMAL) {
            attributes |= XP_NORMAL;
        }

        file.name = file_name;
        file.bytes = bytes;
        file.attributes = attributes;
        xp_file_push(directory, file);
    } while (FindNextFileA(find_handle, &find_data));

    FindClose(find_handle);

    return true;
}
#elif defined(__linux__)
bool xp_directory_new(xp_path path, xp_directory *directory) {
    xp_normalize(&path);
    memset(directory, 0, sizeof(xp_directory));
    directory->path = xp_fullpath(path);

    DIR *d = opendir(path.data);
    if (d == NULL) {
        return false;
    }

    int dir_fd = dirfd(d);
    if (dir_fd == -1) {
        return false;
    }
    
    if (d) {
        for (;;) {
            struct dirent *dir = readdir(d);
            if (!dir) break;

            struct stat f_stat;
            int stat_res = fstatat(dir_fd, dir->d_name, &f_stat, 0);

            xp_file file = {0};
            file.name = malloc(strlen(dir->d_name) + 1);
            strcpy(file.name, dir->d_name);
            file.bytes = (uint64_t)f_stat.st_size;
            file.time = f_stat.st_mtime; 

            file.attributes |= (S_ISDIR(f_stat.st_mode) ? XP_DIRECTORY : 0);
            file.attributes |= (S_ISREG(f_stat.st_mode) ? XP_NORMAL : 0);
            file.attributes |= ((f_stat.st_mode & S_IXUSR) ? XP_EXECUTABLE : 0);

            xp_file_push(directory, file);
        }
        closedir(d);
    }
    return true;
}
#endif

void xp_path_append(xp_path *path, char *str) {
    char *ptr = (char *)malloc(path->count + 1 + strlen(str) + 1);
    strncpy(ptr, (char *)path->data, path->count);
    strcat(ptr, "/");
    strcat(ptr, str);

    free(path->data);
    path->data = (unsigned char *)ptr;
    path->count = (int)strlen(ptr);
}

#if defined(_WIN32)
xp_time xp_utc_time(uint64_t time) {
    xp_time utc_time = {0};
    FILETIME ft = {.dwLowDateTime = (uint32_t)time,
        .dwHighDateTime = (uint32_t)(time >> 32)
    };
    FILETIME local_ft = {0};
    SYSTEMTIME systime = {0};
    if (FileTimeToLocalFileTime(&ft, &local_ft)) {
        if (FileTimeToSystemTime(&local_ft, &systime)) {
            utc_time.year = systime.wYear;
            utc_time.month = systime.wMonth;
            utc_time.day = systime.wDay;
            utc_time.hour = systime.wHour;
            utc_time.minute = systime.wMinute;
            utc_time.second = systime.wSecond;
            utc_time.milliseconds = systime.wMilliseconds;
        } else {
            // fprintf(stderr, "FileTimeToSystemTime error\n");
        }
    } else {
        // fprintf(stderr, "FileTimeToLocalFileTime error\n");
    }
    return utc_time;
}
#elif defined(__linux__)
xp_time xp_utc_time(uint64_t time) {
    time_t time_ = (time_t)time;
    struct tm *local_time = localtime(&time_);
    xp_time utc_time = { 0 };
    utc_time.year = local_time->tm_year;
    utc_time.month = local_time->tm_mon + 1;
    utc_time.day = local_time->tm_mday;
    utc_time.hour = local_time->tm_hour;
    utc_time.minute = local_time->tm_min;
    utc_time.second = local_time->tm_sec;
    return utc_time;
}
#endif

#endif // XPATH_H
