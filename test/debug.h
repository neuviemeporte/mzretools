#ifndef TEST_DEBUG_H
#define TEST_DEBUG_H

#include <iostream>

class DebugStream {
private:
    class NullStream : public std::ostream {
    private:
        class NullBuffer : public std::streambuf {
        public:
            int overflow(int c) override { return c; }
        } buffer_;
    public:
        NullStream() : std::ostream(&buffer_) {}
    } null_;

    std::ostream &output_;
    bool enabled_;

public:
    DebugStream(std::ostream &output = std::cout) : output_(output), enabled_(false) {}

    template <typename T> std::ostream& operator<<(const T &arg) {
        if (enabled_) return output_ << arg;
        else return null_ << arg;
    }

    void enable(const bool enable) {
        //if (!enable) *this << "Debugging output disabled" << std::cout;
        enabled_ = enable;
        //if (enable) *this << "Debugging output enabled" << std::cout;
    }
};

extern DebugStream debug_stream;

#define TRACE_ENABLE(x) debug_stream.enable(x)
#define TRACELN(x) debug_stream << x << std::endl
#define TRACE(x) debug_stream << x

#endif // TEST_DEBUG_H
