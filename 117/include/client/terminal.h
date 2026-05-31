#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <termios.h>
#include <sys/ioctl.h>

namespace moshpp {

class Terminal {
public:
    Terminal();
    ~Terminal();

    bool set_raw_mode();
    bool restore_mode();

    ssize_t read(void* buf, size_t count);
    ssize_t write(const void* buf, size_t count);

    bool get_size(uint16_t& rows, uint16_t& cols);
    bool set_size(uint16_t rows, uint16_t cols);

    void clear_screen();
    void move_cursor(uint16_t row, uint16_t col);
    void hide_cursor();
    void show_cursor();

private:
    int fd_;
    struct termios original_termios_;
    bool is_raw_;
    bool termios_saved_;
};

}
