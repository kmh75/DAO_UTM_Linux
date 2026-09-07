#pragma once

#include <QString>

enum class ForceUnit
{
    N,
    Kgf,
    Gf
};

namespace ForceUnits
{
inline constexpr double NewtonsPerKgf = 9.80665;

inline double toNewtons(double value, ForceUnit unit)
{
    switch (unit)
    {
    case ForceUnit::Kgf: return value * NewtonsPerKgf;
    case ForceUnit::Gf: return value * NewtonsPerKgf / 1000.0;
    case ForceUnit::N: return value;
    }
    return value;
}

inline double fromNewtons(double valueN, ForceUnit unit)
{
    switch (unit)
    {
    case ForceUnit::Kgf: return valueN / NewtonsPerKgf;
    case ForceUnit::Gf: return valueN / NewtonsPerKgf * 1000.0;
    case ForceUnit::N: return valueN;
    }
    return valueN;
}

inline QString text(ForceUnit unit)
{
    switch (unit)
    {
    case ForceUnit::Kgf: return "kgf";
    case ForceUnit::Gf: return "gf";
    case ForceUnit::N: return "N";
    }
    return "N";
}

inline ForceUnit fromText(const QString& value)
{
    if (value.compare("kgf", Qt::CaseInsensitive) == 0) return ForceUnit::Kgf;
    if (value.compare("gf", Qt::CaseInsensitive) == 0) return ForceUnit::Gf;
    return ForceUnit::N;
}

inline bool isValidText(const QString& value)
{
    return value == "N" || value == "kgf" || value == "gf";
}
}
