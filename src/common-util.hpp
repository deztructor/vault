#ifndef _COMMON_UTIL_HPP_
#define _COMMON_UTIL_HPP_

#include <qtaround/debug.hpp>
#include <qtaround/error.hpp>

#include <QDebug>
#include <QCommandLineParser>

#include <string>
#include <memory>
#include <string.h>

namespace debug = qtaround::debug;
namespace error = qtaround::error;

template <typename T>
int loggable
(QDebug const& , T v, typename std::enable_if<std::is_enum<T>::value>::type* = 0)
{
    return static_cast<int>(v);
}

template <typename T>
QDebug & operator << (QDebug &dst, T v)
{
    dst << loggable(dst, v);
    return dst;
}

inline QCommandLineParser & operator <<
(QCommandLineParser &parser, QCommandLineOption const &opt)
{
    parser.addOption(opt);
    return parser;
}

inline QString qstr(int v)
{
    return QString::number(v);
}

inline QString qstr(char const *v)
{
    return QString(v);
}

inline QString qstr(std::string const &s)
{
    return QString::fromStdString(s);
}

inline QString qstr(QByteArray const &s)
{
    return QString::fromUtf8(s);
}

inline QString loggable(QDebug const&, std::string const &s)
{
    return qstr(s);
}

template <typename T>
bool is_valid(T v, typename std::enable_if<std::is_enum<T>::value>::type* = 0)
{
    auto i = static_cast<int>(v), first = static_cast<int>(T::First_)
        , last = static_cast<int>(T::Last_);
    return (v >= first && v <= last);
}

template <typename T>
T log_result(std::string const &name, T &&res)
{
    debug::debug(name, res);
    return std::move(res);
}

template <typename T>
QDebug & operator << (QDebug &dst, std::shared_ptr<T> const &p)
{
    dst << "ptr=(";
    if (p) dst << *p; else dst << "null";
    dst << ")";
    return dst;
}

typedef std::unique_ptr<char, void (*)(char *)> cstring_handle;
static cstring_handle strdup_unique(char const *from)
{
    return cstring_handle(::strdup(from), [](char *p) { ::free(p); });
}

typedef std::function<void (QVariantMap const&)> ErrorCallback;

inline void raise_std_error(QVariantMap &&info)
{
    error::raise(info, map({{"errno", errno}, {"strerror", ::strerror(errno)}}));
}

#endif // _COMMON_UTIL_HPP_
