/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <string>
#include <iostream>

namespace Firmir {

class Logger
{
public:
    enum class Category { Debug, Warning, Error, Info };

    Logger(Category category);
    ~Logger();

    template <typename T>
    inline Logger &operator<<(T value)
    {
        if (enabled()) {
            auto &out = m_category == Category::Error ? std::cerr : std::cout;
            out << value << ' ';
        }
        return *this;
    }

private:
    bool enabled();

    Category m_category;
};

class Debug : public Logger
{
public:
    Debug();
};

class Warning : public Logger
{
public:
    Warning();
};

class Error : public Logger
{
public:
    Error();
};

class Info : public Logger
{
public:
    Info();
};

} // namespace Firmir
