#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <stdarg.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef __linux__
#include <sys/ioctl.h>
#endif

#include "xpath.h"

#define KB(N) (1000 *   (N))
#define MB(N) (1000 * KB(N))
#define GB(N) (1000 * MB(N))

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

enum {
    FORMAT_WIDE,
    FORMAT_LONG,
};

enum {
    SORT_NAME,
    SORT_EXTENSION,
    SORT_TIME,
};

static xp_path *paths = NULL;
static int path_count = 0;

static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int line_length = 80;
static int print_dir_name = false;
static int print_format = FORMAT_WIDE;
static int sort_file_type = SORT_NAME;
static bool all_files = false;

void cprint(int r, int g, int b, const char *fmt, ...) {
    printf("\x1b[38;2;%d;%d;%dm", r, g, b);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\x1b[0m");
}

void parse_arg(char *arg) {
    for (arg = arg + 1 ; *arg; arg++) {
        switch (*arg) {
        case 'a':
            all_files = true;
            break;
        case 'l':
            print_format = FORMAT_LONG;
            break;
        case 't':
            sort_file_type = SORT_TIME;
            break;
        case 'X':
            sort_file_type = SORT_EXTENSION;
            break;
        default:
            fprintf(stderr, "Lister: unknown option '%c'\n", *arg);
            exit(0);
        }
    }
}

void process_args(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] == '-') {
            parse_arg(arg);
        }
    }
}

int get_name_length(char *name) {
    // TODO: handle quotes, non-printable characters, special characters
    int length = 0;
    bool spaces = false;
    for (char *ptr = name; *ptr; ptr++) {
        switch (*ptr) {
        case ' ':
            length++;
            spaces = true;
            break;
        default:
            length++;
        }
    }
    // add space for quotes
    if (spaces) length += 2;

    return length;
}

int has_spaces(char *name) {
    for (char *ptr = name; *ptr; ptr++) {
        if (*ptr == ' ') return true;
    }
    return false;
}

void print_size(uint64_t bytes) {
    char size_header = '\0';
    float size = 0;
    if (bytes >= GB(1)) {
        size_header = 'G';
        size = (float)bytes / GB(1);
    } else if (bytes >= MB(1)) {
        size_header = 'M';
        size = (float)bytes / MB(1);
    } else if (bytes >= KB(1)) {
        size_header = 'K';
        size = (float)bytes / KB(1);
    } else {
        size = (float)bytes;
    }

    // TODO: cleanup
    if (size_header) {
        if (size >= 100.0f) {
            printf(" %d%c", (int)size, size_header);
        } else if (size >= 10.0f) {
            printf("  %d%c", (int)size, size_header);
        } else {
            printf(" %.1f%c", size, size_header);
        } 
    } else {
        printf(" %*d", 4, (int)size);
    }
}

void print_name(xp_file file) {
    char *name = file.name;
    bool spaces = has_spaces(file.name);
    if (spaces) {
        name = malloc(strlen(file.name) + 3);
        name[0] = '\'';
        strcpy(name + 1, file.name);
        name[strlen(file.name) + 1] = '\'';
        name[strlen(file.name) + 2] = 0;
    }

    if (file.attributes & XP_DIRECTORY) 
        cprint(0, 0x84, 0xD4, "%s", name);
    else if (file.attributes & XP_EXECUTABLE)
        cprint(0x56, 0xDB, 0x3A, "%s", name);
    else
        printf("%s", name);

    if (spaces) free(name);
}

void print_wide_format(xp_directory dir) {
    int max_name_length = 0;
    for (int file_index = 0; file_index < dir.file_count; file_index++) {
        char *name = dir.files[file_index].name;
        int name_length = get_name_length(name);
        if (name_length > max_name_length) max_name_length = name_length;
    }
    max_name_length += 2; // spaces
    
    int cols = line_length / max_name_length;// - ((line_length % max_name_length) != 0);
    if (cols <= 0) cols = 1;
    int rows = dir.file_count / cols;
    if (rows <= 0) rows = 1;
    cols = dir.file_count / rows;

    int file_index = 0;
    for (int row = 0; row < rows; row++) {
        file_index = row;
        for (int col = 0; col < cols; col++) {
            if (file_index >= dir.file_count) break;
            xp_file file = dir.files[file_index];

            print_name(file);
            if (col < cols - 1) {
                int len = get_name_length(file.name);
                int spaces = (rows == 1) ? 2 : max_name_length - len;
                for (int i = 0; i < spaces; i++) {
                    printf(" ");
                }
            }
            file_index += rows;
        }
        putchar('\n');
    }
}

void print_long_format(xp_directory dir) {
    for (int file_index = 0; file_index < dir.file_count; file_index++) {
        xp_file file = dir.files[file_index];
        // size - month - day - time - name
        // size := [0-9]* [KMGT]B
        // day := [1-31]
        // time [0-23] : [0-59]

        print_size(file.bytes);
        putchar(' ');

        xp_time time = xp_utc_time(file.time);
        printf("%s %*d %.2d:%.2d", months[time.month - 1], 2, time.day, time.hour, time.minute);

        putchar(' ');
        print_name(file);
        putchar('\n');
    }
}

void print_directory(xp_directory dir) {
    if (print_dir_name) {
        if (has_spaces((char *)dir.path.data)) {
            printf("'%s':\n", dir.path.data);
        } else {
            printf("%s:\n", dir.path.data);
        }
    }

    switch (print_format) {
    case FORMAT_WIDE:
        print_wide_format(dir);
        break;
    case FORMAT_LONG:
        print_long_format(dir);
        break;
    }
}

char *get_file_extension(char *file_name) {
    char *extension = NULL;
    for (int i = 0; file_name[i] != 0; i++) {
        if (file_name[i] == '.') {
            extension = file_name + i + 1;
        }
    }
    return extension;
}

int compare_file_extension(xp_file file1, xp_file file2) {
    if (strcmp(file1.name, ".") == 0) return -1;
    if (strcmp(file1.name, "..") == 0) return -1;

    char *ext1 = get_file_extension(file1.name);
    char *ext2 = get_file_extension(file2.name);

    if (ext1 == NULL && ext2 == NULL) return 0;
    if (ext1 == NULL) return -1;
    if (ext2 == NULL) return 1;

   
    int len1 = ext1 ? (int)strlen(ext1) : 0;
    int len2 = ext2 ? (int)strlen(ext2) : 0;
    int len = MIN(len1, len2);
    for (int i = 0; i < len; i++) {
        int diff = ext1[i] - ext2[i];
        if (diff == 0) continue;
        else return diff;
    }
    return 0;
}

int compare_file_name(xp_file file1, xp_file file2) {
    int len = (int)MIN(strlen(file1.name), strlen(file2.name));
    for (int i = 0; i < len; i++) {
        int diff = file1.name[i] - file2.name[i];
        if (diff == 0) continue;
        else return diff;
    }
    return 0;
}

// NOTE: higher priority is later time
int compare_file_time(xp_file file1, xp_file file2) {
#ifdef _WIN32
    FILETIME ft1 = {.dwLowDateTime = (uint32_t)file1.time, .dwHighDateTime = (uint32_t)(file1.time >> 32)};
    FILETIME ft2 = {.dwLowDateTime = (uint32_t)file2.time, .dwHighDateTime = (uint32_t)(file2.time >> 32)};
    // switched times (priority)
    return CompareFileTime(&ft2, &ft1);
#elif __linux__
    return (int)difftime(file2.time, file1.time);
#endif
}

typedef int (*file_sort_t)(xp_file, xp_file);

void sort_files(xp_directory *dir, file_sort_t sort_func) {
    for (int i = 1; i < dir->file_count; i++) {
        xp_file key = dir->files[i];
        int j = i - 1;
        while (j >= 0 && sort_func(dir->files[j], key) > 0) {
            dir->files[j + 1] = dir->files[j];
            j--;
        }
        dir->files[j + 1] = key;
    }
}

void sort_directory_files(xp_directory *dir, int sort_type) {
    switch (sort_type) {
    case SORT_NAME:
        sort_files(dir, compare_file_name);
        break;
    case SORT_EXTENSION:
        sort_files(dir, compare_file_extension);
        break;
    case SORT_TIME:
        sort_files(dir, compare_file_time);
        break;
    }
}

bool abnormal_file(xp_file file) {
    if (file.attributes & XP_HIDDEN) return true;
    if (strncmp(file.name, ".", 1) == 0) return true;
    if (strcmp(file.name, "..") == 0) return true;
    return false;
}

bool file_interesting(xp_file file) {
    if (!all_files && abnormal_file(file)) return false;
    return true;
}

void filter_directory_files(xp_directory *dir) {
    xp_file *files = malloc(dir->file_count * sizeof(xp_file));
    int file_count = 0;
    for (int i = 0; i < dir->file_count; i++) {
        if (file_interesting(dir->files[i])) {
            files[file_count] = dir->files[i];
            file_count++;
        } else {
            free(dir->files[i].name);
        }
    }
    free(dir->files);
    files = realloc(files, file_count * sizeof(xp_file));
    dir->files = files;
    dir->file_count = file_count;
}

int main(int argc, char **argv) {
    argc--; argv++;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO screen_buffer_info;
    HANDLE hc = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dw = 0;

    // NOTE: colored output on windows
    GetConsoleMode(hc, &dw);
    dw |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hc, dw);

    GetConsoleScreenBufferInfo(hc, &screen_buffer_info);
    line_length = screen_buffer_info.srWindow.Right - screen_buffer_info.srWindow.Left + 1;
#elif defined(__linux__)
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    line_length = w.ws_col;
#endif

    process_args(argc, argv);

    for (int i = 0; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] == '-') {
        } else {
            xp_path path = xp_path_new(argv[i]);
            path_count++;
            paths = realloc(paths, path_count * sizeof(xp_path));
            paths[path_count - 1] = path;
        }
    }

    if (path_count == 0) {
        xp_path current_path = xp_current_path();
        path_count++;
        paths = realloc(NULL, path_count * sizeof(xp_path));
        paths[path_count - 1] = current_path;
    }

    if (path_count > 1) {
        print_dir_name = true;
    }

    for (int i = 0; i < path_count; i++) {
        xp_path arg = paths[i];
        xp_path path = arg;
        if (xp_path_relative(arg)) {
            path = xp_fullpath(arg);
            paths[i] = path;
        }

        xp_directory dir = {0};
        if (xp_directory_new(path, &dir)) {
            filter_directory_files(&dir);
            sort_directory_files(&dir, SORT_NAME);
            sort_directory_files(&dir, sort_file_type);
            print_directory(dir);

            if (i < path_count - 1) {
                putchar('\n');
            }
        } else {
            fprintf(stderr, "Lister: failed to access '%s': No such file or directory\n", dir.path.data);
        }
    }

    return 0;
}
