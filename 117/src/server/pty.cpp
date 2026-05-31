#include "server/pty.h"
#include "common/utils.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pty.h>
#include <utmp.h>

namespace moshpp {

PTY::PTY()
    : master_fd_(-1)
    , slave_fd_(-1)
    , child_pid_(-1)
    , termios_saved_(false)
{
}

PTY::~PTY() {
    close();
}

bool PTY::open() {
    master_fd_ = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd_ < 0) {
        Logger::error("Failed to open PTY master: " + std::string(strerror(errno)));
        return false;
    }

    if (!grantpt()) {
        ::close(master_fd_);
        master_fd_ = -1;
        return false;
    }

    if (!unlockpt()) {
        ::close(master_fd_);
        master_fd_ = -1;
        return false;
    }

    if (!ptsname()) {
        ::close(master_fd_);
        master_fd_ = -1;
        return false;
    }

    int flags = fcntl(master_fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);
    }

    return true;
}

void PTY::close() {
    if (child_pid_ > 0) {
        kill(child_pid_, SIGHUP);
        wait_for_child();
        child_pid_ = -1;
    }

    if (master_fd_ >= 0) {
        ::close(master_fd_);
        master_fd_ = -1;
    }

    if (slave_fd_ >= 0) {
        ::close(slave_fd_);
        slave_fd_ = -1;
    }
}

bool PTY::grantpt() {
    if (::grantpt(master_fd_) < 0) {
        Logger::error("grantpt failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

bool PTY::unlockpt() {
    if (::unlockpt(master_fd_) < 0) {
        Logger::error("unlockpt failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

bool PTY::ptsname() {
    const char* name = ::ptsname(master_fd_);
    if (!name) {
        Logger::error("ptsname failed: " + std::string(strerror(errno)));
        return false;
    }
    slave_name_ = name;
    return true;
}

void PTY::setup_slave_termios() {
    struct termios tios;
    if (tcgetattr(slave_fd_, &tios) == 0) {
        original_termios_ = tios;
        termios_saved_ = true;
        
        cfmakeraw(&tios);
        tios.c_cflag |= CREAD | CS8 | HUPCL;
        tios.c_lflag |= ISIG;
        tios.c_cc[VINTR] = 03;
        tios.c_cc[VQUIT] = 034;
        tios.c_cc[VERASE] = 0177;
        tios.c_cc[VKILL] = 025;
        tios.c_cc[VEOF] = 04;
        tios.c_cc[VEOL] = 0;
        tios.c_cc[VEOL2] = 0;
        tios.c_cc[VSWTC] = 0;
        tios.c_cc[VSTART] = 021;
        tios.c_cc[VSTOP] = 023;
        tios.c_cc[VSUSP] = 032;
        tios.c_cc[VREPRINT] = 022;
        tios.c_cc[VDISCARD] = 017;
        tios.c_cc[VWERASE] = 027;
        tios.c_cc[VLNEXT] = 026;
        tios.c_cc[VMIN] = 1;
        tios.c_cc[VTIME] = 0;
        
        tcsetattr(slave_fd_, TCSANOW, &tios);
    }
}

bool PTY::spawn_shell(const std::string& shell) {
    std::vector<std::string> args;
    args.push_back("-" + shell.substr(shell.find_last_of('/') + 1));
    return spawn_command(shell, args);
}

bool PTY::spawn_command(const std::string& cmd, const std::vector<std::string>& args) {
    if (master_fd_ < 0) {
        if (!open()) {
            return false;
        }
    }

    child_pid_ = fork();
    if (child_pid_ < 0) {
        Logger::error("fork failed: " + std::string(strerror(errno)));
        return false;
    }

    if (child_pid_ == 0) {
        setsid();
        
        slave_fd_ = ::open(slave_name_.c_str(), O_RDWR);
        if (slave_fd_ < 0) {
            Logger::error("Failed to open PTY slave: " + std::string(strerror(errno)));
            _exit(1);
        }

        setup_slave_termios();

        dup2(slave_fd_, STDIN_FILENO);
        dup2(slave_fd_, STDOUT_FILENO);
        dup2(slave_fd_, STDERR_FILENO);

        if (master_fd_ > 2) {
            ::close(master_fd_);
        }

        struct winsize ws;
        ws.ws_row = 24;
        ws.ws_col = 80;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        ioctl(slave_fd_, TIOCSWINSZ, &ws);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(args.empty() ? cmd.c_str() : args[0].c_str()));
        for (size_t i = 1; i < args.size(); ++i) {
            argv.push_back(const_cast<char*>(args[i].c_str()));
        }
        argv.push_back(nullptr);

        setenv("TERM", "xterm-256color", 1);
        setenv("SHELL", cmd.c_str(), 1);
        
        execvp(cmd.c_str(), argv.data());
        Logger::error("execvp failed: " + std::string(strerror(errno)));
        _exit(1);
    }

    return true;
}

ssize_t PTY::read(void* buf, size_t count) {
    if (master_fd_ < 0) {
        return -1;
    }
    return ::read(master_fd_, buf, count);
}

ssize_t PTY::write(const void* buf, size_t count) {
    if (master_fd_ < 0) {
        return -1;
    }
    return ::write(master_fd_, buf, count);
}

bool PTY::set_window_size(uint16_t rows, uint16_t cols) {
    if (master_fd_ < 0) {
        return false;
    }

    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    
    if (ioctl(master_fd_, TIOCSWINSZ, &ws) < 0) {
        Logger::warn("ioctl TIOCSWINSZ failed: " + std::string(strerror(errno)));
        return false;
    }
    
    if (child_pid_ > 0) {
        kill(child_pid_, SIGWINCH);
    }
    
    return true;
}

bool PTY::get_window_size(uint16_t& rows, uint16_t& cols) {
    if (master_fd_ < 0) {
        return false;
    }

    struct winsize ws;
    if (ioctl(master_fd_, TIOCGWINSZ, &ws) < 0) {
        return false;
    }
    
    rows = ws.ws_row;
    cols = ws.ws_col;
    return true;
}

void PTY::wait_for_child() {
    if (child_pid_ > 0) {
        int status;
        waitpid(child_pid_, &status, 0);
    }
}

}
