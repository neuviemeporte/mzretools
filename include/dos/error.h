#ifndef ERROR_H
#define ERROR_H

#include <exception>
#include <string>

class Error : public std::exception {
private:
    std::string why_;

public:
    Error(const std::string &why) : std::exception(), why_{why} {}
    virtual const char* what() const noexcept override { return why_.c_str(); }
};

class ArgError : public Error {
    using Error::Error;
};

class ParseError : public Error {
    using Error::Error;
};

class LogicError : public Error {
    using Error::Error;
};

class CpuError : public Error {
    using Error::Error;
};

class MemoryError : public Error {
    using Error::Error;
};

class IoError : public Error {
    using Error::Error;
};

class InterruptError : public Error {
    using Error::Error;
};

class DosError : public Error {
    using Error::Error;
};

class SystemError : public Error {
    using Error::Error;
};

#endif // ERROR_H
