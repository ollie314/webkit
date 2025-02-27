/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef WTF_Seconds_h
#define WTF_Seconds_h

#include <wtf/MathExtras.h>

namespace WTF {

class MonotonicTime;
class PrintStream;
class TimeWithDynamicClockType;
class WallTime;

class Seconds {
public:
    Seconds() { }
    
    explicit constexpr Seconds(double value)
        : m_value(value)
    {
    }
    
    double value() const { return m_value; }
    
    double seconds() const { return m_value; }
    double milliseconds() const { return seconds() * 1000; }
    double microseconds() const { return milliseconds() * 1000; }
    double nanoseconds() const { return microseconds() * 1000; }
    
    static constexpr Seconds fromMilliseconds(double value)
    {
        return Seconds(value / 1000);
    }
    
    static constexpr Seconds fromMicroseconds(double value)
    {
        return fromMilliseconds(value / 1000);
    }
    
    static constexpr Seconds fromNanoseconds(double value)
    {
        return fromMicroseconds(value / 1000);
    }
    
    static constexpr Seconds infinity()
    {
        return Seconds(std::numeric_limits<double>::infinity());
    }
    
    explicit operator bool() const { return !!m_value; }
    
    Seconds operator+(Seconds other) const
    {
        return Seconds(value() + other.value());
    }
    
    Seconds operator-(Seconds other) const
    {
        return Seconds(value() - other.value());
    }
    
    Seconds operator-() const
    {
        return Seconds(-value());
    }
    
    // It makes sense to consider scaling a duration, like, "I want to wait 5 times as long as
    // last time!".
    Seconds operator*(double scalar) const
    {
        return Seconds(value() * scalar);
    }
    
    Seconds operator/(double scalar) const
    {
        return Seconds(value() / scalar);
    }
    
    // It's reasonable to think about ratios between Seconds.
    double operator/(Seconds other) const
    {
        return value() / other.value();
    }
    
    Seconds operator%(double scalar) const
    {
        return Seconds(fmod(value(), scalar));
    }
    
    // This solves for r, where:
    //
    //     floor(this / other) + r / other = this / other
    //
    // Therefore, if this is Seconds then r is Seconds.
    Seconds operator%(Seconds other) const
    {
        return Seconds(fmod(value(), other.value()));
    }
    
    Seconds& operator+=(Seconds other)
    {
        return *this = *this + other;
    }
    
    Seconds& operator-=(Seconds other)
    {
        return *this = *this - other;
    }
    
    Seconds& operator*=(double scalar)
    {
        return *this = *this * scalar;
    }
    
    Seconds& operator/=(double scalar)
    {
        return *this = *this / scalar;
    }
    
    Seconds& operator%=(double scalar)
    {
        return *this = *this % scalar;
    }
    
    Seconds& operator%=(Seconds other)
    {
        return *this = *this % other;
    }
    
    WTF_EXPORT_PRIVATE WallTime operator+(WallTime) const;
    WTF_EXPORT_PRIVATE MonotonicTime operator+(MonotonicTime) const;
    WTF_EXPORT_PRIVATE TimeWithDynamicClockType operator+(const TimeWithDynamicClockType&) const;
    
    WTF_EXPORT_PRIVATE WallTime operator-(WallTime) const;
    WTF_EXPORT_PRIVATE MonotonicTime operator-(MonotonicTime) const;
    WTF_EXPORT_PRIVATE TimeWithDynamicClockType operator-(const TimeWithDynamicClockType&) const;
    
    bool operator==(Seconds other) const
    {
        return m_value == other.m_value;
    }
    
    bool operator!=(Seconds other) const
    {
        return m_value != other.m_value;
    }
    
    bool operator<(Seconds other) const
    {
        return m_value < other.m_value;
    }
    
    bool operator>(Seconds other) const
    {
        return m_value > other.m_value;
    }
    
    bool operator<=(Seconds other) const
    {
        return m_value <= other.m_value;
    }
    
    bool operator>=(Seconds other) const
    {
        return m_value >= other.m_value;
    }
    
    WTF_EXPORT_PRIVATE void dump(PrintStream&) const;
    
private:
    double m_value { 0 };
};

WTF_EXPORT_PRIVATE void sleep(Seconds);

} // namespae WTF

using WTF::Seconds;

#endif // WTF_Seconds_h
