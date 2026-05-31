#include "client/terminal.h"
#include "common/utils.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

namespace moshpp {

Terminal::Terminal()
    : fd_(STDIN_FILENO)
    , is_raw_(false)
    , termios_saved_(false)
{
}

Terminal::~Terminal() {
    restore_mode();
}

bool Terminal::set_raw_mode() {
    if (is_raw_) {
        return true;
    }

    struct termios tios;
    if (tcgetattr(fd_, &tios) < 0) {
        Logger::error("Failed to get terminal attributes: " + std::string(strerror(errno)));
        return false;
    }

    original_termios_ = tios;
    termios_saved_ = true;

    cfmakeraw(&tios);
    tios.c_cc[VMIN] = 1;
    tios.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tios) < 0) {
        Logger::error("Failed to set terminal attributes: " + std::string(strerror(errno)));
        return false;
    }

    is_raw_ = true;
    return true;
}

bool Terminal::restore_mode() {
    if (!is_raw_ || !termios_saved_) {
        return true;
    }

    if (tcsetattr(fd_, TCSANOW, &original_termios_) < 0) {
        Logger::error("Failed to restore terminal attributes: " + std::string(strerror(errno)));
        return false;
    }

    is_raw_ = false;
    return true;
}

ssize_t Terminal::read(void* buf, size_t count) {
    return ::read(fd_, buf, count);
}

ssize_t Terminal::write(const void* buf, size_t count) {
    return ::write(STDOUT_FILENO, buf, count);
}

bool Terminal::get_size(uint16_t& rows, uint16_t& cols) {
    struct winsize ws;
    if (ioctl(fd_, TIOCGWINSZ, &ws) < 0) {
        return false;
    }
    rows = ws.ws_row;
    cols = ws.ws_col;
    return true;
}

bool Terminal::set_size(uint16_t rows, uint16_t cols) {
    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    
    if (ioctl(fd_, TIOCSWINSZ, &ws) < 0) {
        return false;
    }
    return true;
}

void Terminal::clear_screen() {
    const char* clear = "\x1b[2J\x1b[H";
    write(clear, strlen(clear));
}

void Terminal::move_cursor(uint16_t row, uint16_t col) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row + 1, col + 1);
    write(buf, len);
}

void Terminal::hide_cursor() {
    const char* hide = "\x1b[?25l";
    write(hide, strlen(hide));
}

void Terminal::show_cursor() {
    const char* show = "\x1b[?25h";
    write(show, strlen(show));
}

}
