#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstdio>
#endif

inline bool backgroundize() {
#ifdef _WIN32
    if (!FreeConsole()) {
        DWORD err = GetLastError();
        if (err != ERROR_INVALID_HANDLE) {
            return false;
        }
    }
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) {
        std::perror("daemonize: fork failed");
        return false;
    }
    if (pid > 0) {
        std::exit(EXIT_SUCCESS);
    }
    if (setsid() < 0) {
        std::perror("daemonize: setsid failed");
        return false;
    }
    pid = fork();
    if (pid < 0) {
        std::perror("daemonize: second fork failed");
        return false;
    }
    if (pid > 0) {
        std::exit(EXIT_SUCCESS);
    }
    if (chdir("/") < 0) {
        std::perror("daemonize: chdir failed");
    }
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }
    return true;
#endif
}
