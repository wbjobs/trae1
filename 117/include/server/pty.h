#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <termios.h>
#include <sys/ioctl.h>

namespace moshpp {

class PTY {
public:
    PTY();
    ~PTY();

    bool open();
    void close();

    bool spawn_shell(const std::string& shell = "/bin/bash");
    bool spawn_command(const std::string& cmd, const std::vector<std::string>& args);

    ssize_t read(void* buf, size_t count);
    ssize_t write(const void* buf, size_t count);

    int get_master_fd() const { return master_fd_; }
    int get_slave_fd() const { return slave_fd_; }

    bool set_window_size(uint16_t rows, uint16_t cols);
    bool get_window_size(uint16_t& rows, uint16_t& cols);

    pid_t get_child_pid() const { return child_pid_; }
    void wait_for_child();

private:
    int master_fd_;
    int slave_fd_;
    pid_t child_pid_;
    std::string slave_name_;
    struct termios original_termios_;
    bool termios_saved_;

    bool grantpt();
    bool unlockpt();
    bool ptsname();
    void setup_slave_termios();
};

}
