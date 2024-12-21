#ifndef ERROR_H
#define ERROR_H

#include <exception>
#include <string>

class Error : public std::exception {
private:
    const std::string why_;
    const bool fatal_;

public:
    Error(const std::string &why, const bool fatal = false) : std::exception(), why_{why}, fatal_{fatal} {}
    const std::string& why() const { return why_; }
    virtual const char* what() const noexcept override { return why_.c_str(); }
    bool fatal() const { return fatal_; }
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

class AddressError : public Error {
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

class AnalysisError : public Error {
    using Error::Error;
};

#endif // ERROR_H
