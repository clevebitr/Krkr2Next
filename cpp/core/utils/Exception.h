
#ifndef __EXCEPTION_H__
#define __EXCEPTION_H__

namespace krkr::utils {
    class Exception {
        ttstr message_;
    public:
        Exception(const ttstr &mes) : message_(mes) {}
        [[nodiscard]] virtual const ttstr &what() const { return message_; }
    };

    class EAbort : public Exception {
    public:
        EAbort(const ttstr &mes) : Exception(mes) {}
    };
} // namespace krkr::utils

using krkr::utils::Exception;
using krkr::utils::EAbort;

#endif // __EXCEPTION_H__
