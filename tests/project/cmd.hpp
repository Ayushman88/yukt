#pragma once

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>

struct CmdResult {
    int status = 1;
    std::string out;
    std::string err;
};

inline void cmd_read_some(int fd, std::string& into) {
    char buffer[4096];
    while (true) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            into.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
}

inline CmdResult run_cmd(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                         int timeout_sec = 30) {
    CmdResult result;
    if (argv.empty()) {
        return result;
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        return result;
    }
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

    const pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        return result;
    }

    if (pid == 0) {
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            _exit(127);
        }
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        std::vector<char*> cargv;
        for (const auto& arg : argv) {
            cargv.push_back(const_cast<char*>(arg.c_str()));
        }
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);

    const time_t start = time(nullptr);
    int wstatus = 0;
    bool done = false;
    while (!done) {
        cmd_read_some(out_pipe[0], result.out);
        cmd_read_some(err_pipe[0], result.err);
        const pid_t waited = waitpid(pid, &wstatus, WNOHANG);
        if (waited == pid) {
            done = true;
            break;
        }
        if (timeout_sec > 0 && time(nullptr) - start >= timeout_sec) {
            kill(pid, SIGKILL);
            waitpid(pid, &wstatus, 0);
            result.status = 124;
            result.err += "timeout\n";
            close(out_pipe[0]);
            close(err_pipe[0]);
            return result;
        }
        usleep(20000);
    }

    cmd_read_some(out_pipe[0], result.out);
    cmd_read_some(err_pipe[0], result.err);
    close(out_pipe[0]);
    close(err_pipe[0]);
    if (WIFEXITED(wstatus)) {
        result.status = WEXITSTATUS(wstatus);
    }
    return result;
}
