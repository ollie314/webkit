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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "URLParser.h"

#include "Logging.h"
#include <array>
#include <unicode/uidna.h>
#include <unicode/utypes.h>

namespace WebCore {

template<typename CharacterType>
class CodePointIterator {
public:
    CodePointIterator() { }
    CodePointIterator(const CharacterType* begin, const CharacterType* end)
        : m_begin(begin)
        , m_end(end)
    {
    }
    
    CodePointIterator(const CodePointIterator& begin, const CodePointIterator& end)
        : CodePointIterator(begin.m_begin, end.m_begin)
    {
        ASSERT(end.m_begin >= begin.m_begin);
    }
    
    UChar32 operator*() const;
    CodePointIterator& operator++();

    bool operator==(const CodePointIterator& other) const
    {
        return m_begin == other.m_begin
            && m_end == other.m_end;
    }
    bool operator!=(const CodePointIterator& other) const { return !(*this == other); }
    
    CodePointIterator& operator=(const CodePointIterator& other)
    {
        m_begin = other.m_begin;
        m_end = other.m_end;
        return *this;
    }

    bool atEnd() const
    {
        ASSERT(m_begin <= m_end);
        return m_begin >= m_end;
    }
    
    size_t codeUnitsSince(const CharacterType* reference) const
    {
        ASSERT(m_begin >= reference);
        return m_begin - reference;
    }
    
private:
    const CharacterType* m_begin { nullptr };
    const CharacterType* m_end { nullptr };
};

template<>
UChar32 CodePointIterator<LChar>::operator*() const
{
    ASSERT(!atEnd());
    return *m_begin;
}

template<>
auto CodePointIterator<LChar>::operator++() -> CodePointIterator&
{
    ASSERT(!atEnd());
    m_begin++;
    return *this;
}

template<>
UChar32 CodePointIterator<UChar>::operator*() const
{
    ASSERT(!atEnd());
    UChar32 c;
    U16_GET(m_begin, 0, 0, m_end - m_begin, c);
    return c;
}

template<>
auto CodePointIterator<UChar>::operator++() -> CodePointIterator&
{
    ASSERT(!atEnd());
    unsigned i = 0;
    size_t length = m_end - m_begin;
    U16_FWD_1(m_begin, i, length);
    m_begin += i;
    return *this;
}
    
static void appendCodePoint(Vector<UChar>& destination, UChar32 codePoint)
{
    if (U_IS_BMP(codePoint)) {
        destination.append(static_cast<UChar>(codePoint));
        return;
    }
    destination.reserveCapacity(destination.size() + 2);
    destination.uncheckedAppend(U16_LEAD(codePoint));
    destination.uncheckedAppend(U16_TRAIL(codePoint));
}

enum URLCharacterClass {
    UserInfo = 0x1,
    Default = 0x2,
    InvalidDomain = 0x4,
    QueryPercent = 0x8,
    SlashQuestionOrHash = 0x10,
    Scheme = 0x20,
};

static const uint8_t characterClassTable[256] = {
    UserInfo | Default | InvalidDomain | QueryPercent, // 0x0
    UserInfo | Default | QueryPercent, // 0x1
    UserInfo | Default | QueryPercent, // 0x2
    UserInfo | Default | QueryPercent, // 0x3
    UserInfo | Default | QueryPercent, // 0x4
    UserInfo | Default | QueryPercent, // 0x5
    UserInfo | Default | QueryPercent, // 0x6
    UserInfo | Default | QueryPercent, // 0x7
    UserInfo | Default | QueryPercent, // 0x8
    UserInfo | Default | InvalidDomain | QueryPercent, // 0x9
    UserInfo | Default | InvalidDomain | QueryPercent, // 0xA
    UserInfo | Default | QueryPercent, // 0xB
    UserInfo | Default | QueryPercent, // 0xC
    UserInfo | Default | InvalidDomain | QueryPercent, // 0xD
    UserInfo | Default | QueryPercent, // 0xE
    UserInfo | Default | QueryPercent, // 0xF
    UserInfo | Default | QueryPercent, // 0x10
    UserInfo | Default | QueryPercent, // 0x11
    UserInfo | Default | QueryPercent, // 0x12
    UserInfo | Default | QueryPercent, // 0x13
    UserInfo | Default | QueryPercent, // 0x14
    UserInfo | Default | QueryPercent, // 0x15
    UserInfo | Default | QueryPercent, // 0x16
    UserInfo | Default | QueryPercent, // 0x17
    UserInfo | Default | QueryPercent, // 0x18
    UserInfo | Default | QueryPercent, // 0x19
    UserInfo | Default | QueryPercent, // 0x1A
    UserInfo | Default | QueryPercent, // 0x1B
    UserInfo | Default | QueryPercent, // 0x1C
    UserInfo | Default | QueryPercent, // 0x1D
    UserInfo | Default | QueryPercent, // 0x1E
    UserInfo | Default | QueryPercent, // 0x1F
    UserInfo | Default | InvalidDomain | QueryPercent, // ' '
    0, // '!'
    UserInfo | Default | QueryPercent, // '"'
    UserInfo | Default | InvalidDomain | QueryPercent | SlashQuestionOrHash, // '#'
    0, // '$'
    InvalidDomain, // '%'
    0, // '&'
    0, // '''
    0, // '('
    0, // ')'
    0, // '*'
    Scheme, // '+'
    0, // ','
    Scheme, // '-'
    Scheme, // '.'
    UserInfo | InvalidDomain | SlashQuestionOrHash, // '/'
    Scheme, // '0'
    Scheme, // '1'
    Scheme, // '2'
    Scheme, // '3'
    Scheme, // '4'
    Scheme, // '5'
    Scheme, // '6'
    Scheme, // '7'
    Scheme, // '8'
    Scheme, // '9'
    UserInfo | InvalidDomain, // ':'
    UserInfo, // ';'
    UserInfo | Default | QueryPercent, // '<'
    UserInfo, // '='
    UserInfo | Default | QueryPercent, // '>'
    UserInfo | Default | InvalidDomain | SlashQuestionOrHash, // '?'
    UserInfo | InvalidDomain, // '@'
    Scheme, // 'A'
    Scheme, // 'B'
    Scheme, // 'C'
    Scheme, // 'D'
    Scheme, // 'E'
    Scheme, // 'F'
    Scheme, // 'G'
    Scheme, // 'H'
    Scheme, // 'I'
    Scheme, // 'J'
    Scheme, // 'K'
    Scheme, // 'L'
    Scheme, // 'M'
    Scheme, // 'N'
    Scheme, // 'O'
    Scheme, // 'P'
    Scheme, // 'Q'
    Scheme, // 'R'
    Scheme, // 'S'
    Scheme, // 'T'
    Scheme, // 'U'
    Scheme, // 'V'
    Scheme, // 'W'
    Scheme, // 'X'
    Scheme, // 'Y'
    Scheme, // 'Z'
    UserInfo | InvalidDomain, // '['
    UserInfo | InvalidDomain | SlashQuestionOrHash, // '\\'
    UserInfo | InvalidDomain, // ']'
    UserInfo, // '^'
    0, // '_'
    UserInfo | Default, // '`'
    Scheme, // 'a'
    Scheme, // 'b'
    Scheme, // 'c'
    Scheme, // 'd'
    Scheme, // 'e'
    Scheme, // 'f'
    Scheme, // 'g'
    Scheme, // 'h'
    Scheme, // 'i'
    Scheme, // 'j'
    Scheme, // 'k'
    Scheme, // 'l'
    Scheme, // 'm'
    Scheme, // 'n'
    Scheme, // 'o'
    Scheme, // 'p'
    Scheme, // 'q'
    Scheme, // 'r'
    Scheme, // 's'
    Scheme, // 't'
    Scheme, // 'u'
    Scheme, // 'v'
    Scheme, // 'w'
    Scheme, // 'x'
    Scheme, // 'y'
    Scheme, // 'z'
    UserInfo | Default, // '{'
    UserInfo, // '|'
    UserInfo | Default, // '}'
    0, // '~'
    QueryPercent, // 0x7F
    QueryPercent, // 0x80
    QueryPercent, // 0x81
    QueryPercent, // 0x82
    QueryPercent, // 0x83
    QueryPercent, // 0x84
    QueryPercent, // 0x85
    QueryPercent, // 0x86
    QueryPercent, // 0x87
    QueryPercent, // 0x88
    QueryPercent, // 0x89
    QueryPercent, // 0x8A
    QueryPercent, // 0x8B
    QueryPercent, // 0x8C
    QueryPercent, // 0x8D
    QueryPercent, // 0x8E
    QueryPercent, // 0x8F
    QueryPercent, // 0x90
    QueryPercent, // 0x91
    QueryPercent, // 0x92
    QueryPercent, // 0x93
    QueryPercent, // 0x94
    QueryPercent, // 0x95
    QueryPercent, // 0x96
    QueryPercent, // 0x97
    QueryPercent, // 0x98
    QueryPercent, // 0x99
    QueryPercent, // 0x9A
    QueryPercent, // 0x9B
    QueryPercent, // 0x9C
    QueryPercent, // 0x9D
    QueryPercent, // 0x9E
    QueryPercent, // 0x9F
    QueryPercent, // 0xA0
    QueryPercent, // 0xA1
    QueryPercent, // 0xA2
    QueryPercent, // 0xA3
    QueryPercent, // 0xA4
    QueryPercent, // 0xA5
    QueryPercent, // 0xA6
    QueryPercent, // 0xA7
    QueryPercent, // 0xA8
    QueryPercent, // 0xA9
    QueryPercent, // 0xAA
    QueryPercent, // 0xAB
    QueryPercent, // 0xAC
    QueryPercent, // 0xAD
    QueryPercent, // 0xAE
    QueryPercent, // 0xAF
    QueryPercent, // 0xB0
    QueryPercent, // 0xB1
    QueryPercent, // 0xB2
    QueryPercent, // 0xB3
    QueryPercent, // 0xB4
    QueryPercent, // 0xB5
    QueryPercent, // 0xB6
    QueryPercent, // 0xB7
    QueryPercent, // 0xB8
    QueryPercent, // 0xB9
    QueryPercent, // 0xBA
    QueryPercent, // 0xBB
    QueryPercent, // 0xBC
    QueryPercent, // 0xBD
    QueryPercent, // 0xBE
    QueryPercent, // 0xBF
    QueryPercent, // 0xC0
    QueryPercent, // 0xC1
    QueryPercent, // 0xC2
    QueryPercent, // 0xC3
    QueryPercent, // 0xC4
    QueryPercent, // 0xC5
    QueryPercent, // 0xC6
    QueryPercent, // 0xC7
    QueryPercent, // 0xC8
    QueryPercent, // 0xC9
    QueryPercent, // 0xCA
    QueryPercent, // 0xCB
    QueryPercent, // 0xCC
    QueryPercent, // 0xCD
    QueryPercent, // 0xCE
    QueryPercent, // 0xCF
    QueryPercent, // 0xD0
    QueryPercent, // 0xD1
    QueryPercent, // 0xD2
    QueryPercent, // 0xD3
    QueryPercent, // 0xD4
    QueryPercent, // 0xD5
    QueryPercent, // 0xD6
    QueryPercent, // 0xD7
    QueryPercent, // 0xD8
    QueryPercent, // 0xD9
    QueryPercent, // 0xDA
    QueryPercent, // 0xDB
    QueryPercent, // 0xDC
    QueryPercent, // 0xDD
    QueryPercent, // 0xDE
    QueryPercent, // 0xDF
    QueryPercent, // 0xE0
    QueryPercent, // 0xE1
    QueryPercent, // 0xE2
    QueryPercent, // 0xE3
    QueryPercent, // 0xE4
    QueryPercent, // 0xE5
    QueryPercent, // 0xE6
    QueryPercent, // 0xE7
    QueryPercent, // 0xE8
    QueryPercent, // 0xE9
    QueryPercent, // 0xEA
    QueryPercent, // 0xEB
    QueryPercent, // 0xEC
    QueryPercent, // 0xED
    QueryPercent, // 0xEE
    QueryPercent, // 0xEF
    QueryPercent, // 0xF0
    QueryPercent, // 0xF1
    QueryPercent, // 0xF2
    QueryPercent, // 0xF3
    QueryPercent, // 0xF4
    QueryPercent, // 0xF5
    QueryPercent, // 0xF6
    QueryPercent, // 0xF7
    QueryPercent, // 0xF8
    QueryPercent, // 0xF9
    QueryPercent, // 0xFA
    QueryPercent, // 0xFB
    QueryPercent, // 0xFC
    QueryPercent, // 0xFD
    QueryPercent, // 0xFE
    QueryPercent, // 0xFF
};

template<typename CharacterType> inline static bool isC0Control(CharacterType character) { return character <= 0x1F; }
template<typename CharacterType> inline static bool isC0ControlOrSpace(CharacterType character) { return character <= 0x20; }
template<typename CharacterType> inline static bool isTabOrNewline(CharacterType character) { return character <= 0xD && character >= 0x9 && character != 0xB && character != 0xC; }
template<typename CharacterType> inline static bool isInSimpleEncodeSet(CharacterType character) { return character > 0x7E || isC0Control(character); }
template<typename CharacterType> inline static bool isInDefaultEncodeSet(CharacterType character) { return character > 0x7E || characterClassTable[character] & Default; }
template<typename CharacterType> inline static bool isInUserInfoEncodeSet(CharacterType character) { return character > 0x7E || characterClassTable[character] & UserInfo; }
template<typename CharacterType> inline static bool isInvalidDomainCharacter(CharacterType character) { return character <= ']' && characterClassTable[character] & InvalidDomain; }
template<typename CharacterType> inline static bool isPercentOrNonASCII(CharacterType character) { return !isASCII(character) || character == '%'; }
template<typename CharacterType> inline static bool isSlashQuestionOrHash(CharacterType character) { return character <= '\\' && characterClassTable[character] & SlashQuestionOrHash; }
template<typename CharacterType> inline static bool isValidSchemeCharacter(CharacterType character) { return character <= 'z' && characterClassTable[character] & Scheme; }
static bool shouldPercentEncodeQueryByte(uint8_t byte) { return characterClassTable[byte] & QueryPercent; }

template<typename CharacterType>
void URLParser::incrementIteratorSkippingTabAndNewLine(CodePointIterator<CharacterType>& iterator)
{
    ++iterator;
    while (!iterator.atEnd() && isTabOrNewline(*iterator)) {
        syntaxError(iterator);
        ++iterator;
    }
}

template<typename CharacterType>
bool URLParser::isWindowsDriveLetter(CodePointIterator<CharacterType> iterator)
{
    if (iterator.atEnd() || !isASCIIAlpha(*iterator))
        return false;
    incrementIteratorSkippingTabAndNewLine(iterator);
    if (iterator.atEnd())
        return false;
    if (*iterator == ':')
        return true;
    if (*iterator == '|') {
        syntaxError(iterator);
        return true;
    }
    return false;
}

inline static bool isWindowsDriveLetter(const Vector<LChar>& buffer, size_t index)
{
    if (buffer.size() < index + 2)
        return false;
    return isASCIIAlpha(buffer[index]) && (buffer[index + 1] == ':' || buffer[index + 1] == '|');
}

void URLParser::appendToASCIIBuffer(UChar32 codePoint)
{
    ASSERT(m_unicodeFragmentBuffer.isEmpty());
    ASSERT(isASCII(codePoint));
    if (m_seenSyntaxError)
        m_asciiBuffer.append(codePoint);
}

void URLParser::appendToASCIIBuffer(const char* characters, size_t length)
{
    ASSERT(m_unicodeFragmentBuffer.isEmpty());
    if (m_seenSyntaxError)
        m_asciiBuffer.append(characters, length);
}

template<typename CharacterType>
void URLParser::checkWindowsDriveLetter(CodePointIterator<CharacterType>& iterator)
{
    if (isWindowsDriveLetter(iterator)) {
        appendToASCIIBuffer(*iterator);
        incrementIteratorSkippingTabAndNewLine(iterator);
        ASSERT(!iterator.atEnd());
        ASSERT(*iterator == ':' || *iterator == '|');
        appendToASCIIBuffer(':');
        incrementIteratorSkippingTabAndNewLine(iterator);
    }
}

template<typename CharacterType>
bool URLParser::shouldCopyFileURL(CodePointIterator<CharacterType> iterator)
{
    if (!isWindowsDriveLetter(iterator))
        return true;
    if (iterator.atEnd())
        return false;
    incrementIteratorSkippingTabAndNewLine(iterator);
    if (iterator.atEnd())
        return true;
    incrementIteratorSkippingTabAndNewLine(iterator);
    if (iterator.atEnd())
        return true;
    return !isSlashQuestionOrHash(*iterator);
}

inline static void percentEncodeByte(uint8_t byte, Vector<LChar>& buffer)
{
    buffer.append('%');
    buffer.append(upperNibbleToASCIIHexDigit(byte));
    buffer.append(lowerNibbleToASCIIHexDigit(byte));
}

void URLParser::percentEncodeByte(uint8_t byte)
{
    appendToASCIIBuffer('%');
    appendToASCIIBuffer(upperNibbleToASCIIHexDigit(byte));
    appendToASCIIBuffer(lowerNibbleToASCIIHexDigit(byte));
}

const char replacementCharacterUTF8PercentEncoded[10] = "%EF%BF%BD";
const size_t replacementCharacterUTF8PercentEncodedLength = sizeof(replacementCharacterUTF8PercentEncoded) - 1;

template<bool(*isInCodeSet)(UChar32)>
void URLParser::utf8PercentEncode(UChar32 codePoint)
{
    if (isASCII(codePoint)) {
        if (isInCodeSet(codePoint))
            percentEncodeByte(codePoint);
        else
            appendToASCIIBuffer(codePoint);
        return;
    }
    ASSERT_WITH_MESSAGE(isInCodeSet(codePoint), "isInCodeSet should always return true for non-ASCII characters");
    
    if (!U_IS_UNICODE_CHAR(codePoint)) {
        appendToASCIIBuffer(replacementCharacterUTF8PercentEncoded, replacementCharacterUTF8PercentEncodedLength);
        return;
    }
    
    uint8_t buffer[U8_MAX_LENGTH];
    int32_t offset = 0;
    U8_APPEND_UNSAFE(buffer, offset, codePoint);
    for (int32_t i = 0; i < offset; ++i)
        percentEncodeByte(buffer[i]);
}


void URLParser::utf8QueryEncode(UChar32 codePoint)
{
    if (isASCII(codePoint)) {
        if (shouldPercentEncodeQueryByte(codePoint))
            percentEncodeByte(codePoint);
        else
            appendToASCIIBuffer(codePoint);
        return;
    }
    
    if (!U_IS_UNICODE_CHAR(codePoint)) {
        appendToASCIIBuffer(replacementCharacterUTF8PercentEncoded, replacementCharacterUTF8PercentEncodedLength);
        return;
    }

    uint8_t buffer[U8_MAX_LENGTH];
    int32_t offset = 0;
    U8_APPEND_UNSAFE(buffer, offset, codePoint);
    for (int32_t i = 0; i < offset; ++i) {
        auto byte = buffer[i];
        if (shouldPercentEncodeQueryByte(byte))
            percentEncodeByte(byte);
        else
            appendToASCIIBuffer(byte);
    }
}
    
void URLParser::encodeQuery(const Vector<UChar>& source, const TextEncoding& encoding)
{
    // FIXME: It is unclear in the spec what to do when encoding fails. The behavior should be specified and tested.
    CString encoded = encoding.encode(StringView(source.data(), source.size()), URLEncodedEntitiesForUnencodables);
    const char* data = encoded.data();
    size_t length = encoded.length();
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        if (shouldPercentEncodeQueryByte(byte))
            percentEncodeByte(byte);
        else
            appendToASCIIBuffer(byte);
    }
}

inline static bool isDefaultPort(StringView scheme, uint16_t port)
{
    static const uint16_t ftpPort = 21;
    static const uint16_t gopherPort = 70;
    static const uint16_t httpPort = 80;
    static const uint16_t httpsPort = 443;
    static const uint16_t wsPort = 80;
    static const uint16_t wssPort = 443;
    
    auto length = scheme.length();
    if (!length)
        return false;
    switch (scheme[0]) {
    case 'w':
        switch (length) {
        case 2:
            return scheme[1] == 's'
                && port == wsPort;
        case 3:
            return scheme[1] == 's'
                && scheme[2] == 's'
                && port == wssPort;
        default:
            return false;
        }
    case 'h':
        switch (length) {
        case 4:
            return scheme[1] == 't'
                && scheme[2] == 't'
                && scheme[3] == 'p'
                && port == httpPort;
        case 5:
            return scheme[1] == 't'
                && scheme[2] == 't'
                && scheme[3] == 'p'
                && scheme[4] == 's'
                && port == httpsPort;
        default:
            return false;
        }
    case 'g':
        return length == 6
            && scheme[1] == 'o'
            && scheme[2] == 'p'
            && scheme[3] == 'h'
            && scheme[4] == 'e'
            && scheme[5] == 'r'
            && port == gopherPort;
    case 'f':
        return length == 3
            && scheme[1] == 't'
            && scheme[2] == 'p'
            && port == ftpPort;
        return false;
    default:
        return false;
    }
}

inline static bool isSpecialScheme(StringView scheme)
{
    auto length = scheme.length();
    if (!length)
        return false;
    switch (scheme[0]) {
    case 'f':
        switch (length) {
        case 3:
            return scheme[1] == 't'
                && scheme[2] == 'p';
        case 4:
            return scheme[1] == 'i'
                && scheme[2] == 'l'
                && scheme[3] == 'e';
        default:
            return false;
        }
    case 'g':
        return length == 6
            && scheme[1] == 'o'
            && scheme[2] == 'p'
            && scheme[3] == 'h'
            && scheme[4] == 'e'
            && scheme[5] == 'r';
    case 'h':
        switch (length) {
        case 4:
            return scheme[1] == 't'
                && scheme[2] == 't'
                && scheme[3] == 'p';
        case 5:
            return scheme[1] == 't'
                && scheme[2] == 't'
                && scheme[3] == 'p'
                && scheme[4] == 's';
        default:
            return false;
        }
    case 'w':
        switch (length) {
        case 2:
            return scheme[1] == 's';
        case 3:
            return scheme[1] == 's'
                && scheme[2] == 's';
        default:
            return false;
        }
    default:
        return false;
    }
}

enum class URLParser::URLPart {
    SchemeEnd,
    UserStart,
    UserEnd,
    PasswordEnd,
    HostEnd,
    PortEnd,
    PathAfterLastSlash,
    PathEnd,
    QueryEnd,
    FragmentEnd,
};

size_t URLParser::urlLengthUntilPart(const URL& url, URLPart part)
{
    switch (part) {
    case URLPart::FragmentEnd:
        return url.m_fragmentEnd;
    case URLPart::QueryEnd:
        return url.m_queryEnd;
    case URLPart::PathEnd:
        return url.m_pathEnd;
    case URLPart::PathAfterLastSlash:
        return url.m_pathAfterLastSlash;
    case URLPart::PortEnd:
        return url.m_portEnd;
    case URLPart::HostEnd:
        return url.m_hostEnd;
    case URLPart::PasswordEnd:
        return url.m_passwordEnd;
    case URLPart::UserEnd:
        return url.m_userEnd;
    case URLPart::UserStart:
        return url.m_userStart;
    case URLPart::SchemeEnd:
        return url.m_schemeEnd;
    }
    ASSERT_NOT_REACHED();
    return 0;
}

void URLParser::copyASCIIStringUntil(const String& string, size_t lengthIf8Bit, size_t lengthIf16Bit)
{
    if (string.isNull()) {
        ASSERT(!lengthIf8Bit);
        ASSERT(!lengthIf16Bit);
        return;
    }
    ASSERT(m_asciiBuffer.isEmpty());
    if (string.is8Bit()) {
        RELEASE_ASSERT(lengthIf8Bit <= string.length());
        appendToASCIIBuffer(string.characters8(), lengthIf8Bit);
    } else {
        RELEASE_ASSERT(lengthIf16Bit <= string.length());
        const UChar* characters = string.characters16();
        for (size_t i = 0; i < lengthIf16Bit; ++i) {
            UChar c = characters[i];
            ASSERT_WITH_SECURITY_IMPLICATION(isASCII(c));
            appendToASCIIBuffer(c);
        }
    }
}

void URLParser::copyURLPartsUntil(const URL& base, URLPart part)
{
    m_asciiBuffer.clear();
    m_unicodeFragmentBuffer.clear();
    if (part == URLPart::FragmentEnd) {
        copyASCIIStringUntil(base.m_string, urlLengthUntilPart(base, URLPart::FragmentEnd), urlLengthUntilPart(base, URLPart::QueryEnd));
        if (!base.m_string.is8Bit()) {
            const String& fragment = base.m_string;
            bool seenUnicode = false;
            for (size_t i = base.m_queryEnd; i < base.m_fragmentEnd; ++i) {
                if (!seenUnicode && !isASCII(fragment[i]))
                    seenUnicode = true;
                if (seenUnicode)
                    m_unicodeFragmentBuffer.uncheckedAppend(fragment[i]);
                else
                    m_asciiBuffer.uncheckedAppend(fragment[i]);
            }
        }
    } else {
        size_t length = urlLengthUntilPart(base, part);
        copyASCIIStringUntil(base.m_string, length, length);
    }
    switch (part) {
    case URLPart::FragmentEnd:
        m_url.m_fragmentEnd = base.m_fragmentEnd;
        FALLTHROUGH;
    case URLPart::QueryEnd:
        m_url.m_queryEnd = base.m_queryEnd;
        FALLTHROUGH;
    case URLPart::PathEnd:
        m_url.m_pathEnd = base.m_pathEnd;
        FALLTHROUGH;
    case URLPart::PathAfterLastSlash:
        m_url.m_pathAfterLastSlash = base.m_pathAfterLastSlash;
        FALLTHROUGH;
    case URLPart::PortEnd:
        m_url.m_portEnd = base.m_portEnd;
        FALLTHROUGH;
    case URLPart::HostEnd:
        m_url.m_hostEnd = base.m_hostEnd;
        FALLTHROUGH;
    case URLPart::PasswordEnd:
        m_url.m_passwordEnd = base.m_passwordEnd;
        FALLTHROUGH;
    case URLPart::UserEnd:
        m_url.m_userEnd = base.m_userEnd;
        FALLTHROUGH;
    case URLPart::UserStart:
        m_url.m_userStart = base.m_userStart;
        FALLTHROUGH;
    case URLPart::SchemeEnd:
        m_url.m_isValid = base.m_isValid;
        m_url.m_protocolIsInHTTPFamily = base.m_protocolIsInHTTPFamily;
        m_url.m_schemeEnd = base.m_schemeEnd;
    }
    m_urlIsSpecial = isSpecialScheme(StringView(m_asciiBuffer.data(), m_url.m_schemeEnd));
}

static const char* dotASCIICode = "2e";

template<typename CharacterType>
inline static bool isPercentEncodedDot(CodePointIterator<CharacterType> c)
{
    if (c.atEnd())
        return false;
    if (*c != '%')
        return false;
    ++c;
    if (c.atEnd())
        return false;
    if (*c != dotASCIICode[0])
        return false;
    ++c;
    if (c.atEnd())
        return false;
    return toASCIILower(*c) == dotASCIICode[1];
}

template<typename CharacterType>
inline static bool isSingleDotPathSegment(CodePointIterator<CharacterType> c)
{
    if (c.atEnd())
        return false;
    if (*c == '.') {
        ++c;
        return c.atEnd() || isSlashQuestionOrHash(*c);
    }
    if (*c != '%')
        return false;
    ++c;
    if (c.atEnd() || *c != dotASCIICode[0])
        return false;
    ++c;
    if (c.atEnd())
        return false;
    if (toASCIILower(*c) == dotASCIICode[1]) {
        ++c;
        return c.atEnd() || isSlashQuestionOrHash(*c);
    }
    return false;
}

template<typename CharacterType>
inline static bool isDoubleDotPathSegment(CodePointIterator<CharacterType> c)
{
    if (c.atEnd())
        return false;
    if (*c == '.') {
        ++c;
        return isSingleDotPathSegment(c);
    }
    if (*c != '%')
        return false;
    ++c;
    if (c.atEnd() || *c != dotASCIICode[0])
        return false;
    ++c;
    if (c.atEnd())
        return false;
    if (toASCIILower(*c) == dotASCIICode[1]) {
        ++c;
        return isSingleDotPathSegment(c);
    }
    return false;
}

template<typename CharacterType>
inline static void consumeSingleDotPathSegment(CodePointIterator<CharacterType>& c)
{
    ASSERT(isSingleDotPathSegment(c));
    if (*c == '.') {
        ++c;
        if (!c.atEnd()) {
            if (*c == '/' || *c == '\\')
                ++c;
            else
                ASSERT(*c == '?' || *c == '#');
        }
    } else {
        ASSERT(*c == '%');
        ++c;
        ASSERT(*c == dotASCIICode[0]);
        ++c;
        ASSERT(toASCIILower(*c) == dotASCIICode[1]);
        ++c;
        if (!c.atEnd()) {
            if (*c == '/' || *c == '\\')
                ++c;
            else
                ASSERT(*c == '?' || *c == '#');
        }
    }
}

template<typename CharacterType>
inline static void consumeDoubleDotPathSegment(CodePointIterator<CharacterType>& c)
{
    ASSERT(isDoubleDotPathSegment(c));
    if (*c == '.')
        ++c;
    else {
        ASSERT(*c == '%');
        ++c;
        ASSERT(*c == dotASCIICode[0]);
        ++c;
        ASSERT(toASCIILower(*c) == dotASCIICode[1]);
        ++c;
    }
    consumeSingleDotPathSegment(c);
}

void URLParser::popPath()
{
    if (m_url.m_pathAfterLastSlash > m_url.m_portEnd + 1) {
        m_url.m_pathAfterLastSlash--;
        if (m_asciiBuffer[m_url.m_pathAfterLastSlash] == '/')
            m_url.m_pathAfterLastSlash--;
        while (m_url.m_pathAfterLastSlash > m_url.m_portEnd && m_asciiBuffer[m_url.m_pathAfterLastSlash] != '/')
            m_url.m_pathAfterLastSlash--;
        m_url.m_pathAfterLastSlash++;
    }
    m_asciiBuffer.resize(m_url.m_pathAfterLastSlash);
}

template<typename CharacterType>
void URLParser::syntaxError(const CodePointIterator<CharacterType>&)
{
    // FIXME: Implement.
}

void URLParser::failure()
{
    m_url.invalidate();
    m_url.m_string = m_inputString;
}

template<typename CharacterType>
size_t URLParser::currentPosition(const CodePointIterator<CharacterType>& iterator)
{
    if (m_seenSyntaxError)
        return m_asciiBuffer.size();
    
    return iterator.codeUnitsSince(reinterpret_cast<const CharacterType*>(m_inputBegin));
}

URLParser::URLParser(const String& input, const URL& base, const TextEncoding& encoding)
    : m_inputString(input)
{
    if (input.isNull())
        return;

    if (input.is8Bit()) {
        m_inputBegin = input.characters8();
        parse(input.characters8(), input.length(), base, encoding);
    } else {
        m_inputBegin = input.characters16();
        parse(input.characters16(), input.length(), base, encoding);
    }
}

template<typename CharacterType>
void URLParser::parse(const CharacterType* input, const unsigned length, const URL& base, const TextEncoding& encoding)
{
    LOG(URLParser, "Parsing URL <%s> base <%s>", String(input, length).utf8().data(), base.string().utf8().data());
    m_url = { };
    ASSERT(m_asciiBuffer.isEmpty());
    ASSERT(m_unicodeFragmentBuffer.isEmpty());
    m_asciiBuffer.reserveInitialCapacity(length);
    
    bool isUTF8Encoding = encoding == UTF8Encoding();
    Vector<UChar> queryBuffer;

    unsigned endIndex = length;
    while (endIndex && isC0ControlOrSpace(input[endIndex - 1]))
        endIndex--;
    CodePointIterator<CharacterType> c(input, input + endIndex);
    CodePointIterator<CharacterType> authorityOrHostBegin;
    while (!c.atEnd() && isC0ControlOrSpace(*c))
        ++c;
    auto beginAfterControlAndSpace = c;

    enum class State : uint8_t {
        SchemeStart,
        Scheme,
        NoScheme,
        SpecialRelativeOrAuthority,
        PathOrAuthority,
        Relative,
        RelativeSlash,
        SpecialAuthoritySlashes,
        SpecialAuthorityIgnoreSlashes,
        AuthorityOrHost,
        Host,
        File,
        FileSlash,
        FileHost,
        PathStart,
        Path,
        CannotBeABaseURLPath,
        Query,
        Fragment,
    };

#define LOG_STATE(x) LOG(URLParser, "State %s, code point %c, asciiBuffer size %zu", x, *c, currentPosition(c))
#define LOG_FINAL_STATE(x) LOG(URLParser, "Final State: %s", x)

    State state = State::SchemeStart;
    while (!c.atEnd()) {
        if (isTabOrNewline(*c)) {
            syntaxError(c);
            ++c;
            continue;
        }

        switch (state) {
        case State::SchemeStart:
            LOG_STATE("SchemeStart");
            if (isASCIIAlpha(*c)) {
                appendToASCIIBuffer(toASCIILower(*c));
                incrementIteratorSkippingTabAndNewLine(c);
                if (c.atEnd()) {
                    m_asciiBuffer.clear();
                    state = State::NoScheme;
                    c = beginAfterControlAndSpace;
                }
                state = State::Scheme;
            } else
                state = State::NoScheme;
            break;
        case State::Scheme:
            LOG_STATE("Scheme");
            if (isValidSchemeCharacter(*c))
                appendToASCIIBuffer(toASCIILower(*c));
            else if (*c == ':') {
                m_url.m_schemeEnd = currentPosition(c);
                StringView urlScheme = StringView(m_asciiBuffer.data(), m_url.m_schemeEnd);
                m_url.m_protocolIsInHTTPFamily = urlScheme == "http" || urlScheme == "https";
                if (urlScheme == "file") {
                    m_urlIsSpecial = true;
                    state = State::File;
                    appendToASCIIBuffer(':');
                    ++c;
                    break;
                }
                appendToASCIIBuffer(':');
                if (isSpecialScheme(urlScheme)) {
                    m_urlIsSpecial = true;
                    if (base.protocolIs(m_asciiBuffer.data(), currentPosition(c) - 1))
                        state = State::SpecialRelativeOrAuthority;
                    else
                        state = State::SpecialAuthoritySlashes;
                } else {
                    auto maybeSlash = c;
                    incrementIteratorSkippingTabAndNewLine(maybeSlash);
                    if (!maybeSlash.atEnd() && *maybeSlash == '/') {
                        appendToASCIIBuffer('/');
                        m_url.m_userStart = currentPosition(c);
                        state = State::PathOrAuthority;
                        c = maybeSlash;
                        ASSERT(*c == '/');
                    } else {
                        m_url.m_userStart = currentPosition(c);
                        m_url.m_userEnd = m_url.m_userStart;
                        m_url.m_passwordEnd = m_url.m_userStart;
                        m_url.m_hostEnd = m_url.m_userStart;
                        m_url.m_portEnd = m_url.m_userStart;
                        m_url.m_pathAfterLastSlash = m_url.m_userStart;
                        m_url.m_cannotBeABaseURL = true;
                        state = State::CannotBeABaseURLPath;
                    }
                }
                ++c;
                break;
            } else {
                m_asciiBuffer.clear();
                state = State::NoScheme;
                c = beginAfterControlAndSpace;
                break;
            }
            incrementIteratorSkippingTabAndNewLine(c);
            if (c.atEnd()) {
                m_asciiBuffer.clear();
                state = State::NoScheme;
                c = beginAfterControlAndSpace;
            }
            break;
        case State::NoScheme:
            LOG_STATE("NoScheme");
            if (!base.isValid() || (base.m_cannotBeABaseURL && *c != '#')) {
                failure();
                return;
            }
            if (base.m_cannotBeABaseURL && *c == '#') {
                copyURLPartsUntil(base, URLPart::QueryEnd);
                state = State::Fragment;
                appendToASCIIBuffer('#');
                ++c;
                break;
            }
            if (!base.protocolIs("file")) {
                state = State::Relative;
                break;
            }
            copyURLPartsUntil(base, URLPart::SchemeEnd);
            appendToASCIIBuffer(':');
            state = State::File;
            break;
        case State::SpecialRelativeOrAuthority:
            LOG_STATE("SpecialRelativeOrAuthority");
            if (*c == '/') {
                appendToASCIIBuffer('/');
                incrementIteratorSkippingTabAndNewLine(c);
                if (c.atEnd()) {
                    failure();
                    return;
                }
                if (*c == '/') {
                    appendToASCIIBuffer('/');
                    state = State::SpecialAuthorityIgnoreSlashes;
                    ++c;
                } else
                    state = State::RelativeSlash;
            } else
                state = State::Relative;
            break;
        case State::PathOrAuthority:
            LOG_STATE("PathOrAuthority");
            if (*c == '/') {
                appendToASCIIBuffer('/');
                m_url.m_userStart = currentPosition(c);
                state = State::AuthorityOrHost;
                ++c;
                authorityOrHostBegin = c;
            } else {
                ASSERT(m_asciiBuffer.last() == '/');
                m_url.m_userStart = currentPosition(c) - 1;
                m_url.m_userEnd = m_url.m_userStart;
                m_url.m_passwordEnd = m_url.m_userStart;
                m_url.m_hostEnd = m_url.m_userStart;
                m_url.m_portEnd = m_url.m_userStart;
                m_url.m_pathAfterLastSlash = m_url.m_userStart + 1;
                state = State::Path;
            }
            break;
        case State::Relative:
            LOG_STATE("Relative");
            switch (*c) {
            case '/':
            case '\\':
                state = State::RelativeSlash;
                ++c;
                break;
            case '?':
                copyURLPartsUntil(base, URLPart::PathEnd);
                appendToASCIIBuffer('?');
                state = State::Query;
                ++c;
                break;
            case '#':
                copyURLPartsUntil(base, URLPart::QueryEnd);
                appendToASCIIBuffer('#');
                state = State::Fragment;
                ++c;
                break;
            default:
                copyURLPartsUntil(base, URLPart::PathAfterLastSlash);
                state = State::Path;
                break;
            }
            break;
        case State::RelativeSlash:
            LOG_STATE("RelativeSlash");
            if (*c == '/' || *c == '\\') {
                ++c;
                copyURLPartsUntil(base, URLPart::SchemeEnd);
                appendToASCIIBuffer("://", 3);
                state = State::SpecialAuthorityIgnoreSlashes;
            } else {
                copyURLPartsUntil(base, URLPart::PortEnd);
                appendToASCIIBuffer('/');
                m_url.m_pathAfterLastSlash = base.m_portEnd + 1;
                state = State::Path;
            }
            break;
        case State::SpecialAuthoritySlashes:
            LOG_STATE("SpecialAuthoritySlashes");
            appendToASCIIBuffer("//", 2);
            if (*c == '/' || *c == '\\') {
                incrementIteratorSkippingTabAndNewLine(c);
                if (!c.atEnd() && (*c == '/' || *c == '\\'))
                    ++c;
            }
            state = State::SpecialAuthorityIgnoreSlashes;
            break;
        case State::SpecialAuthorityIgnoreSlashes:
            LOG_STATE("SpecialAuthorityIgnoreSlashes");
            if (*c == '/' || *c == '\\') {
                appendToASCIIBuffer('/');
                ++c;
            }
            m_url.m_userStart = currentPosition(c);
            state = State::AuthorityOrHost;
            authorityOrHostBegin = c;
            break;
        case State::AuthorityOrHost:
            LOG_STATE("AuthorityOrHost");
            {
                if (*c == '@') {
                    auto lastAt = c;
                    auto findLastAt = c;
                    while (!findLastAt.atEnd()) {
                        if (*findLastAt == '@')
                            lastAt = findLastAt;
                        ++findLastAt;
                    }
                    parseAuthority(CodePointIterator<CharacterType>(authorityOrHostBegin, lastAt));
                    c = lastAt;
                    incrementIteratorSkippingTabAndNewLine(c);
                    authorityOrHostBegin = c;
                    state = State::Host;
                    m_hostHasPercentOrNonASCII = false;
                    break;
                }
                bool isSlash = *c == '/' || (m_urlIsSpecial && *c == '\\');
                if (isSlash || *c == '?' || *c == '#') {
                    m_url.m_userEnd = currentPosition(c);
                    m_url.m_passwordEnd = m_url.m_userEnd;
                    if (!parseHostAndPort(CodePointIterator<CharacterType>(authorityOrHostBegin, c))) {
                        failure();
                        return;
                    }
                    if (!isSlash) {
                        appendToASCIIBuffer('/');
                        m_url.m_pathAfterLastSlash = currentPosition(c);
                    }
                    state = State::Path;
                    break;
                }
                if (isPercentOrNonASCII(*c))
                    m_hostHasPercentOrNonASCII = true;
                ++c;
            }
            break;
        case State::Host:
            LOG_STATE("Host");
            if (*c == '/' || *c == '?' || *c == '#') {
                if (!parseHostAndPort(CodePointIterator<CharacterType>(authorityOrHostBegin, c))) {
                    failure();
                    return;
                }
                state = State::Path;
                break;
            }
            if (isPercentOrNonASCII(*c))
                m_hostHasPercentOrNonASCII = true;
            ++c;
            break;
        case State::File:
            LOG_STATE("File");
            switch (*c) {
            case '/':
            case '\\':
                appendToASCIIBuffer('/');
                state = State::FileSlash;
                ++c;
                break;
            case '?':
                if (base.isValid() && base.protocolIs("file"))
                    copyURLPartsUntil(base, URLPart::PathEnd);
                appendToASCIIBuffer("///?", 4);
                m_url.m_userStart = currentPosition(c) - 2;
                m_url.m_userEnd = m_url.m_userStart;
                m_url.m_passwordEnd = m_url.m_userStart;
                m_url.m_hostEnd = m_url.m_userStart;
                m_url.m_portEnd = m_url.m_userStart;
                m_url.m_pathAfterLastSlash = m_url.m_userStart + 1;
                m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
                state = State::Query;
                ++c;
                break;
            case '#':
                if (base.isValid() && base.protocolIs("file"))
                    copyURLPartsUntil(base, URLPart::QueryEnd);
                appendToASCIIBuffer("///#", 4);
                m_url.m_userStart = currentPosition(c) - 2;
                m_url.m_userEnd = m_url.m_userStart;
                m_url.m_passwordEnd = m_url.m_userStart;
                m_url.m_hostEnd = m_url.m_userStart;
                m_url.m_portEnd = m_url.m_userStart;
                m_url.m_pathAfterLastSlash = m_url.m_userStart + 1;
                m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
                m_url.m_queryEnd = m_url.m_pathAfterLastSlash;
                state = State::Fragment;
                ++c;
                break;
            default:
                if (base.isValid() && base.protocolIs("file") && shouldCopyFileURL(c))
                    copyURLPartsUntil(base, URLPart::PathAfterLastSlash);
                else {
                    appendToASCIIBuffer("///", 3);
                    m_url.m_userStart = currentPosition(c) - 1;
                    m_url.m_userEnd = m_url.m_userStart;
                    m_url.m_passwordEnd = m_url.m_userStart;
                    m_url.m_hostEnd = m_url.m_userStart;
                    m_url.m_portEnd = m_url.m_userStart;
                    m_url.m_pathAfterLastSlash = m_url.m_userStart + 1;
                    checkWindowsDriveLetter(c);
                }
                state = State::Path;
                break;
            }
            break;
        case State::FileSlash:
            LOG_STATE("FileSlash");
            if (*c == '/' || *c == '\\') {
                ++c;
                appendToASCIIBuffer('/');
                m_url.m_userStart = currentPosition(c);
                m_url.m_userEnd = m_url.m_userStart;
                m_url.m_passwordEnd = m_url.m_userStart;
                m_url.m_hostEnd = m_url.m_userStart;
                m_url.m_portEnd = m_url.m_userStart;
                authorityOrHostBegin = c;
                state = State::FileHost;
                break;
            }
            if (base.isValid() && base.protocolIs("file")) {
                // FIXME: This String copy is unnecessary.
                String basePath = base.path();
                if (basePath.length() >= 2) {
                    bool windowsQuirk = basePath.is8Bit()
                        ? isWindowsDriveLetter(CodePointIterator<LChar>(basePath.characters8(), basePath.characters8() + basePath.length()))
                        : isWindowsDriveLetter(CodePointIterator<UChar>(basePath.characters16(), basePath.characters16() + basePath.length()));
                    if (windowsQuirk) {
                        appendToASCIIBuffer(basePath[0]);
                        appendToASCIIBuffer(basePath[1]);
                    }
                }
            }
            appendToASCIIBuffer("//", 2);
            m_url.m_userStart = currentPosition(c) - 1;
            m_url.m_userEnd = m_url.m_userStart;
            m_url.m_passwordEnd = m_url.m_userStart;
            m_url.m_hostEnd = m_url.m_userStart;
            m_url.m_portEnd = m_url.m_userStart;
            m_url.m_pathAfterLastSlash = m_url.m_userStart + 1;
            checkWindowsDriveLetter(c);
            state = State::Path;
            break;
        case State::FileHost:
            LOG_STATE("FileHost");
            if (isSlashQuestionOrHash(*c)) {
                if (WebCore::isWindowsDriveLetter(m_asciiBuffer, m_url.m_portEnd + 1)) {
                    state = State::Path;
                    break;
                }
                if (authorityOrHostBegin == c) {
                    ASSERT(m_asciiBuffer[currentPosition(c) - 1] == '/');
                    if (*c == '?') {
                        appendToASCIIBuffer("/?", 2);
                        m_url.m_pathAfterLastSlash = currentPosition(c) - 1;
                        m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
                        state = State::Query;
                        ++c;
                        break;
                    }
                    if (*c == '#') {
                        appendToASCIIBuffer("/#", 2);
                        m_url.m_pathAfterLastSlash = currentPosition(c) - 1;
                        m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
                        m_url.m_queryEnd = m_url.m_pathAfterLastSlash;
                        state = State::Fragment;
                        ++c;
                        break;
                    }
                    state = State::Path;
                    break;
                }
                if (!parseHostAndPort(CodePointIterator<CharacterType>(authorityOrHostBegin, c))) {
                    failure();
                    return;
                }
                
                if (StringView(m_asciiBuffer.data() + m_url.m_passwordEnd, currentPosition(c) - m_url.m_passwordEnd) == "localhost")  {
                    m_asciiBuffer.shrink(m_url.m_passwordEnd);
                    m_url.m_hostEnd = currentPosition(c);
                    m_url.m_portEnd = m_url.m_hostEnd;
                }
                
                state = State::PathStart;
                break;
            }
            if (isPercentOrNonASCII(*c))
                m_hostHasPercentOrNonASCII = true;
            ++c;
            break;
        case State::PathStart:
            LOG_STATE("PathStart");
            if (*c != '/' && *c != '\\')
                ++c;
            state = State::Path;
            break;
        case State::Path:
            LOG_STATE("Path");
            if (*c == '/' || (m_urlIsSpecial && *c == '\\')) {
                appendToASCIIBuffer('/');
                m_url.m_pathAfterLastSlash = currentPosition(c);
                ++c;
                break;
            }
            if (currentPosition(c) && m_asciiBuffer[currentPosition(c) - 1] == '/') {
                if (isDoubleDotPathSegment(c)) {
                    consumeDoubleDotPathSegment(c);
                    popPath();
                    break;
                }
                if (m_asciiBuffer[currentPosition(c) - 1] == '/' && isSingleDotPathSegment(c)) {
                    consumeSingleDotPathSegment(c);
                    break;
                }
            }
            if (*c == '?') {
                m_url.m_pathEnd = currentPosition(c);
                state = State::Query;
                break;
            }
            if (*c == '#') {
                m_url.m_pathEnd = currentPosition(c);
                m_url.m_queryEnd = m_url.m_pathEnd;
                state = State::Fragment;
                break;
            }
            if (isPercentEncodedDot(c)) {
                appendToASCIIBuffer('.');
                ASSERT(*c == '%');
                ++c;
                ASSERT(*c == dotASCIICode[0]);
                ++c;
                ASSERT(toASCIILower(*c) == dotASCIICode[1]);
                ++c;
                break;
            }
            utf8PercentEncode<isInDefaultEncodeSet>(*c);
            ++c;
            break;
        case State::CannotBeABaseURLPath:
            LOG_STATE("CannotBeABaseURLPath");
            if (*c == '?') {
                m_url.m_pathEnd = currentPosition(c);
                state = State::Query;
            } else if (*c == '#') {
                m_url.m_pathEnd = currentPosition(c);
                m_url.m_queryEnd = m_url.m_pathEnd;
                state = State::Fragment;
            } else if (*c == '/') {
                appendToASCIIBuffer('/');
                m_url.m_pathAfterLastSlash = currentPosition(c);
                ++c;
            } else {
                utf8PercentEncode<isInSimpleEncodeSet>(*c);
                ++c;
            }
            break;
        case State::Query:
            LOG_STATE("Query");
            if (*c == '#') {
                if (!isUTF8Encoding)
                    encodeQuery(queryBuffer, encoding);
                m_url.m_queryEnd = currentPosition(c);
                state = State::Fragment;
                break;
            }
            if (isUTF8Encoding)
                utf8QueryEncode(*c);
            else
                appendCodePoint(queryBuffer, *c);
            ++c;
            break;
        case State::Fragment:
            LOG_STATE("Fragment");
            if (m_unicodeFragmentBuffer.isEmpty() && isASCII(*c))
                appendToASCIIBuffer(*c);
            else
                appendCodePoint(m_unicodeFragmentBuffer, *c);
            ++c;
            break;
        }
    }

    switch (state) {
    case State::SchemeStart:
        LOG_FINAL_STATE("SchemeStart");
        if (!currentPosition(c) && base.isValid()) {
            m_url = base;
            return;
        }
        failure();
        return;
    case State::Scheme:
        LOG_FINAL_STATE("Scheme");
        failure();
        return;
    case State::NoScheme:
        LOG_FINAL_STATE("NoScheme");
        RELEASE_ASSERT_NOT_REACHED();
    case State::SpecialRelativeOrAuthority:
        LOG_FINAL_STATE("SpecialRelativeOrAuthority");
        copyURLPartsUntil(base, URLPart::QueryEnd);
        m_url.m_fragmentEnd = m_url.m_queryEnd;
        break;
    case State::PathOrAuthority:
        LOG_FINAL_STATE("PathOrAuthority");
        ASSERT(m_url.m_userStart);
        ASSERT(m_url.m_userStart == currentPosition(c));
        ASSERT(m_asciiBuffer.last() == '/');
        m_url.m_userStart--;
        m_url.m_userEnd = m_url.m_userStart;
        m_url.m_passwordEnd = m_url.m_userStart;
        m_url.m_hostEnd = m_url.m_userStart;
        m_url.m_portEnd = m_url.m_userStart;
        m_url.m_pathAfterLastSlash = m_url.m_userStart + 1;
        m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
        m_url.m_queryEnd = m_url.m_pathAfterLastSlash;
        m_url.m_fragmentEnd = m_url.m_pathAfterLastSlash;
        break;
    case State::Relative:
        LOG_FINAL_STATE("Relative");
        copyURLPartsUntil(base, URLPart::FragmentEnd);
        break;
    case State::RelativeSlash:
        LOG_FINAL_STATE("RelativeSlash");
        copyURLPartsUntil(base, URLPart::PortEnd);
        appendToASCIIBuffer('/');
        m_url.m_pathAfterLastSlash = base.m_portEnd + 1;
        m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
        m_url.m_queryEnd = m_url.m_pathAfterLastSlash;
        m_url.m_fragmentEnd = m_url.m_pathAfterLastSlash;
        break;
    case State::SpecialAuthoritySlashes:
        LOG_FINAL_STATE("SpecialAuthoritySlashes");
        m_url.m_userStart = currentPosition(c);
        m_url.m_userEnd = m_url.m_userStart;
        m_url.m_passwordEnd = m_url.m_userStart;
        m_url.m_hostEnd = m_url.m_userStart;
        m_url.m_portEnd = m_url.m_userStart;
        m_url.m_pathAfterLastSlash = m_url.m_userStart;
        m_url.m_pathEnd = m_url.m_userStart;
        m_url.m_queryEnd = m_url.m_userStart;
        m_url.m_fragmentEnd = m_url.m_userStart;
        break;
    case State::SpecialAuthorityIgnoreSlashes:
        LOG_FINAL_STATE("SpecialAuthorityIgnoreSlashes");
        failure();
        return;
        break;
    case State::AuthorityOrHost:
        LOG_FINAL_STATE("AuthorityOrHost");
        m_url.m_userEnd = currentPosition(c);
        m_url.m_passwordEnd = m_url.m_userEnd;
        if (authorityOrHostBegin.atEnd()) {
            m_url.m_hostEnd = m_url.m_userEnd;
            m_url.m_portEnd = m_url.m_userEnd;
        } else if (!parseHostAndPort(authorityOrHostBegin)) {
            failure();
            return;
        }
        appendToASCIIBuffer('/');
        m_url.m_pathEnd = m_url.m_portEnd + 1;
        m_url.m_pathAfterLastSlash = m_url.m_pathEnd;
        m_url.m_queryEnd = m_url.m_pathEnd;
        m_url.m_fragmentEnd = m_url.m_pathEnd;
        break;
    case State::Host:
        LOG_FINAL_STATE("Host");
        if (!parseHostAndPort(authorityOrHostBegin)) {
            failure();
            return;
        }
        appendToASCIIBuffer('/');
        m_url.m_pathEnd = m_url.m_portEnd + 1;
        m_url.m_pathAfterLastSlash = m_url.m_pathEnd;
        m_url.m_queryEnd = m_url.m_pathEnd;
        m_url.m_fragmentEnd = m_url.m_pathEnd;
        break;
    case State::File:
        LOG_FINAL_STATE("File");
        if (base.isValid() && base.protocolIs("file")) {
            copyURLPartsUntil(base, URLPart::QueryEnd);
            appendToASCIIBuffer(':');
        }
        appendToASCIIBuffer("///", 3);
        m_url.m_userStart = currentPosition(c) - 1;
        m_url.m_userEnd = m_url.m_userStart;
        m_url.m_passwordEnd = m_url.m_userStart;
        m_url.m_hostEnd = m_url.m_userStart;
        m_url.m_portEnd = m_url.m_userStart;
        m_url.m_pathAfterLastSlash = m_url.m_userStart + 1;
        m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
        m_url.m_queryEnd = m_url.m_pathAfterLastSlash;
        m_url.m_fragmentEnd = m_url.m_pathAfterLastSlash;
        break;
    case State::FileSlash:
        LOG_FINAL_STATE("FileSlash");
        appendToASCIIBuffer("//", 2);
        m_url.m_userStart = currentPosition(c) - 1;
        m_url.m_userEnd = m_url.m_userStart;
        m_url.m_passwordEnd = m_url.m_userStart;
        m_url.m_hostEnd = m_url.m_userStart;
        m_url.m_portEnd = m_url.m_userStart;
        m_url.m_pathAfterLastSlash = m_url.m_userStart + 1;
        m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
        m_url.m_queryEnd = m_url.m_pathAfterLastSlash;
        m_url.m_fragmentEnd = m_url.m_pathAfterLastSlash;
        break;
    case State::FileHost:
        LOG_FINAL_STATE("FileHost");
        if (authorityOrHostBegin == c) {
            appendToASCIIBuffer('/');
            m_url.m_userStart = currentPosition(c) - 1;
            m_url.m_userEnd = m_url.m_userStart;
            m_url.m_passwordEnd = m_url.m_userStart;
            m_url.m_hostEnd = m_url.m_userStart;
            m_url.m_portEnd = m_url.m_userStart;
            m_url.m_pathAfterLastSlash = m_url.m_userStart + 1;
            m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
            m_url.m_queryEnd = m_url.m_pathAfterLastSlash;
            m_url.m_fragmentEnd = m_url.m_pathAfterLastSlash;
            break;
        }

        if (!parseHostAndPort(CodePointIterator<CharacterType>(authorityOrHostBegin, c))) {
            failure();
            return;
        }

        if (StringView(m_asciiBuffer.data() + m_url.m_passwordEnd, currentPosition(c) - m_url.m_passwordEnd) == "localhost")  {
            m_asciiBuffer.shrink(m_url.m_passwordEnd);
            m_url.m_hostEnd = currentPosition(c);
            m_url.m_portEnd = m_url.m_hostEnd;
        }
        appendToASCIIBuffer('/');
        m_url.m_pathAfterLastSlash = m_url.m_hostEnd + 1;
        m_url.m_pathEnd = m_url.m_pathAfterLastSlash;
        m_url.m_queryEnd = m_url.m_pathAfterLastSlash;
        m_url.m_fragmentEnd = m_url.m_pathAfterLastSlash;
        break;
    case State::PathStart:
        LOG_FINAL_STATE("PathStart");
        RELEASE_ASSERT_NOT_REACHED();
    case State::Path:
        LOG_FINAL_STATE("Path");
        m_url.m_pathEnd = currentPosition(c);
        m_url.m_queryEnd = m_url.m_pathEnd;
        m_url.m_fragmentEnd = m_url.m_pathEnd;
        break;
    case State::CannotBeABaseURLPath:
        LOG_FINAL_STATE("CannotBeABaseURLPath");
        m_url.m_pathEnd = currentPosition(c);
        m_url.m_queryEnd = m_url.m_pathEnd;
        m_url.m_fragmentEnd = m_url.m_pathEnd;
        break;
    case State::Query:
        LOG_FINAL_STATE("Query");
        if (!isUTF8Encoding)
            encodeQuery(queryBuffer, encoding);
        m_url.m_queryEnd = currentPosition(c);
        m_url.m_fragmentEnd = m_url.m_queryEnd;
        break;
    case State::Fragment:
        LOG_FINAL_STATE("Fragment");
        m_url.m_fragmentEnd = currentPosition(c) + m_unicodeFragmentBuffer.size();
        break;
    }

    if (!m_seenSyntaxError) {
        m_url.m_string = m_inputString;
        ASSERT(m_asciiBuffer.isEmpty());
        ASSERT(m_unicodeFragmentBuffer.isEmpty());
    } else if (m_unicodeFragmentBuffer.isEmpty())
        m_url.m_string = String::adopt(WTFMove(m_asciiBuffer));
    else {
        Vector<UChar> buffer;
        buffer.reserveInitialCapacity(currentPosition(c) + m_unicodeFragmentBuffer.size());
        buffer.appendVector(m_asciiBuffer);
        buffer.appendVector(m_unicodeFragmentBuffer);
        m_url.m_string = String::adopt(WTFMove(buffer));
    }
    m_url.m_isValid = true;
    LOG(URLParser, "Parsed URL <%s>", m_url.m_string.utf8().data());
    ASSERT(internalValuesConsistent(m_url));
}

template<typename CharacterType>
void URLParser::parseAuthority(CodePointIterator<CharacterType> iterator)
{
    if (iterator.atEnd()) {
        m_url.m_userEnd = currentPosition(iterator);
        m_url.m_passwordEnd = m_url.m_userEnd;
        return;
    }
    for (; !iterator.atEnd(); ++iterator) {
        if (*iterator == ':') {
            ++iterator;
            m_url.m_userEnd = currentPosition(iterator);
            if (iterator.atEnd()) {
                m_url.m_passwordEnd = m_url.m_userEnd;
                if (m_url.m_userEnd > m_url.m_userStart)
                    appendToASCIIBuffer('@');
                return;
            }
            appendToASCIIBuffer(':');
            break;
        }
        utf8PercentEncode<isInUserInfoEncodeSet>(*iterator);
    }
    for (; !iterator.atEnd(); ++iterator)
        utf8PercentEncode<isInUserInfoEncodeSet>(*iterator);
    m_url.m_passwordEnd = currentPosition(iterator);
    if (!m_url.m_userEnd)
        m_url.m_userEnd = m_url.m_passwordEnd;
    appendToASCIIBuffer('@');
}

template<typename UnsignedIntegerType>
void URLParser::appendNumberToASCIIBuffer(UnsignedIntegerType number)
{
    LChar buf[sizeof(UnsignedIntegerType) * 3 + 1];
    LChar* end = buf + WTF_ARRAY_LENGTH(buf);
    LChar* p = end;
    do {
        *--p = (number % 10) + '0';
        number /= 10;
    } while (number);
    appendToASCIIBuffer(p, end - p);
}

void URLParser::serializeIPv4(IPv4Address address)
{
    appendNumberToASCIIBuffer<uint8_t>(address >> 24);
    appendToASCIIBuffer('.');
    appendNumberToASCIIBuffer<uint8_t>(address >> 16);
    appendToASCIIBuffer('.');
    appendNumberToASCIIBuffer<uint8_t>(address >> 8);
    appendToASCIIBuffer('.');
    appendNumberToASCIIBuffer<uint8_t>(address);
}
    
inline static size_t zeroSequenceLength(const std::array<uint16_t, 8>& address, size_t begin)
{
    size_t end = begin;
    for (; end < 8; end++) {
        if (address[end])
            break;
    }
    return end - begin;
}

inline static Optional<size_t> findLongestZeroSequence(const std::array<uint16_t, 8>& address)
{
    Optional<size_t> longest;
    size_t longestLength = 0;
    for (size_t i = 0; i < 8; i++) {
        size_t length = zeroSequenceLength(address, i);
        if (length) {
            if (length > 1 && (!longest || longestLength < length)) {
                longest = i;
                longestLength = length;
            }
            i += length;
        }
    }
    return longest;
}

void URLParser::serializeIPv6Piece(uint16_t piece)
{
    bool printed = false;
    if (auto nibble0 = piece >> 12) {
        appendToASCIIBuffer(lowerNibbleToLowercaseASCIIHexDigit(nibble0));
        printed = true;
    }
    auto nibble1 = piece >> 8 & 0xF;
    if (printed || nibble1) {
        appendToASCIIBuffer(lowerNibbleToLowercaseASCIIHexDigit(nibble1));
        printed = true;
    }
    auto nibble2 = piece >> 4 & 0xF;
    if (printed || nibble2)
        appendToASCIIBuffer(lowerNibbleToLowercaseASCIIHexDigit(nibble2));
    appendToASCIIBuffer(lowerNibbleToLowercaseASCIIHexDigit(piece & 0xF));
}

void URLParser::serializeIPv6(URLParser::IPv6Address address)
{
    appendToASCIIBuffer('[');
    auto compressPointer = findLongestZeroSequence(address);
    for (size_t piece = 0; piece < 8; piece++) {
        if (compressPointer && compressPointer.value() == piece) {
            ASSERT(!address[piece]);
            if (piece)
                appendToASCIIBuffer(':');
            else
                appendToASCIIBuffer("::", 2);
            while (piece < 8 && !address[piece])
                piece++;
            if (piece == 8)
                break;
        }
        serializeIPv6Piece(address[piece]);
        if (piece < 7)
            appendToASCIIBuffer(':');
    }
    appendToASCIIBuffer(']');
}

template<typename CharacterType>
inline static Optional<uint32_t> parseIPv4Number(CodePointIterator<CharacterType>& iterator)
{
    // FIXME: Check for overflow.
    enum class State : uint8_t {
        UnknownBase,
        Decimal,
        OctalOrHex,
        Octal,
        Hex,
    };
    State state = State::UnknownBase;
    uint32_t value = 0;
    while (!iterator.atEnd()) {
        if (*iterator == '.') {
            ++iterator;
            return value;
        }
        switch (state) {
        case State::UnknownBase:
            if (*iterator == '0') {
                ++iterator;
                state = State::OctalOrHex;
                break;
            }
            state = State::Decimal;
            break;
        case State::OctalOrHex:
            if (*iterator == 'x' || *iterator == 'X') {
                ++iterator;
                state = State::Hex;
                break;
            }
            state = State::Octal;
            break;
        case State::Decimal:
            if (*iterator < '0' || *iterator > '9')
                return Nullopt;
            value *= 10;
            value += *iterator - '0';
            ++iterator;
            break;
        case State::Octal:
            if (*iterator < '0' || *iterator > '7')
                return Nullopt;
            value *= 8;
            value += *iterator - '0';
            ++iterator;
            break;
        case State::Hex:
            if (!isASCIIHexDigit(*iterator))
                return Nullopt;
            value *= 16;
            value += toASCIIHexValue(*iterator);
            ++iterator;
            break;
        }
    }
    return value;
}

inline static uint64_t pow256(size_t exponent)
{
    RELEASE_ASSERT(exponent <= 4);
    uint64_t values[5] = {1, 256, 256 * 256, 256 * 256 * 256, 256ull * 256 * 256 * 256 };
    return values[exponent];
}

template<typename CharacterType>
Optional<URLParser::IPv4Address> URLParser::parseIPv4Host(CodePointIterator<CharacterType> iterator)
{
    Vector<uint32_t, 4> items;
    items.reserveInitialCapacity(4);
    while (!iterator.atEnd()) {
        if (items.size() >= 4)
            return Nullopt;
        if (auto item = parseIPv4Number(iterator))
            items.append(item.value());
        else
            return Nullopt;
    }
    if (!items.size() || items.size() > 4)
        return Nullopt;
    if (items.size() > 2) {
        for (size_t i = 0; i < items.size() - 2; i++) {
            if (items[i] > 255)
                return Nullopt;
        }
    }
    if (items[items.size() - 1] >= pow256(5 - items.size()))
        return Nullopt;
    for (auto item : items) {
        if (item > 255)
            return Nullopt;
    }
    IPv4Address ipv4 = items.takeLast();
    for (size_t counter = 0; counter < items.size(); ++counter)
        ipv4 += items[counter] * pow256(3 - counter);
    return ipv4;
}
    
template<typename CharacterType>
Optional<URLParser::IPv6Address> URLParser::parseIPv6Host(CodePointIterator<CharacterType> c)
{
    if (c.atEnd())
        return Nullopt;

    IPv6Address address = {{0, 0, 0, 0, 0, 0, 0, 0}};
    size_t piecePointer = 0;
    Optional<size_t> compressPointer;

    if (*c == ':') {
        ++c;
        if (c.atEnd())
            return Nullopt;
        if (*c != ':')
            return Nullopt;
        ++c;
        ++piecePointer;
        compressPointer = piecePointer;
    }
    
    while (!c.atEnd()) {
        if (piecePointer == 8)
            return Nullopt;
        if (*c == ':') {
            if (compressPointer)
                return Nullopt;
            ++c;
            ++piecePointer;
            compressPointer = piecePointer;
            continue;
        }
        uint16_t value = 0;
        for (size_t length = 0; length < 4; length++) {
            if (c.atEnd())
                break;
            if (!isASCIIHexDigit(*c))
                break;
            value = value * 0x10 + toASCIIHexValue(*c);
            ++c;
        }
        address[piecePointer++] = value;
        if (c.atEnd())
            break;
        if (*c != ':')
            return Nullopt;
        ++c;
    }
    
    if (!c.atEnd()) {
        if (piecePointer > 6)
            return Nullopt;
        size_t dotsSeen = 0;
        while (!c.atEnd()) {
            Optional<uint16_t> value;
            if (!isASCIIDigit(*c))
                return Nullopt;
            while (isASCIIDigit(*c)) {
                auto number = *c - '0';
                if (!value)
                    value = number;
                else if (!value.value())
                    return Nullopt;
                else
                    value = value.value() * 10 + number;
                ++c;
                if (c.atEnd())
                    return Nullopt;
                if (value.value() > 255)
                    return Nullopt;
            }
            if (dotsSeen < 3 && *c != '.')
                return Nullopt;
            address[piecePointer] = address[piecePointer] * 0x100 + value.valueOr(0);
            if (dotsSeen == 1 || dotsSeen == 3)
                piecePointer++;
            if (!c.atEnd())
                ++c;
            if (dotsSeen == 3 && !c.atEnd())
                return Nullopt;
            dotsSeen++;
        }
    }
    if (compressPointer) {
        size_t swaps = piecePointer - compressPointer.value();
        piecePointer = 7;
        while (swaps)
            std::swap(address[piecePointer--], address[compressPointer.value() + swaps-- - 1]);
    } else if (piecePointer != 8)
        return Nullopt;
    return address;
}

const size_t defaultInlineBufferSize = 2048;

inline static Vector<LChar, defaultInlineBufferSize> percentDecode(const LChar* input, size_t length)
{
    Vector<LChar, defaultInlineBufferSize> output;
    output.reserveInitialCapacity(length);
    
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = input[i];
        if (byte != '%')
            output.uncheckedAppend(byte);
        else if (i < length - 2) {
            if (isASCIIHexDigit(input[i + 1]) && isASCIIHexDigit(input[i + 2])) {
                output.uncheckedAppend(toASCIIHexValue(input[i + 1], input[i + 2]));
                i += 2;
            } else
                output.uncheckedAppend(byte);
        } else
            output.uncheckedAppend(byte);
    }
    return output;
}

inline static bool containsOnlyASCII(const String& string)
{
    if (string.is8Bit())
        return charactersAreAllASCII(string.characters8(), string.length());
    return charactersAreAllASCII(string.characters16(), string.length());
}

inline static Optional<Vector<LChar, defaultInlineBufferSize>> domainToASCII(const String& domain)
{
    Vector<LChar, defaultInlineBufferSize> ascii;
    if (containsOnlyASCII(domain)) {
        size_t length = domain.length();
        if (domain.is8Bit()) {
            const LChar* characters = domain.characters8();
            ascii.reserveInitialCapacity(length);
            for (size_t i = 0; i < length; ++i)
                ascii.uncheckedAppend(toASCIILower(characters[i]));
        } else {
            const UChar* characters = domain.characters16();
            ascii.reserveInitialCapacity(length);
            for (size_t i = 0; i < length; ++i)
                ascii.uncheckedAppend(toASCIILower(characters[i]));
        }
        return ascii;
    }
    
    UChar hostnameBuffer[defaultInlineBufferSize];
    UErrorCode error = U_ZERO_ERROR;

#if COMPILER(GCC) || COMPILER(CLANG)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    // FIXME: This should use uidna_openUTS46 / uidna_close instead
    int32_t numCharactersConverted = uidna_IDNToASCII(StringView(domain).upconvertedCharacters(), domain.length(), hostnameBuffer, defaultInlineBufferSize, UIDNA_ALLOW_UNASSIGNED, nullptr, &error);
#if COMPILER(GCC) || COMPILER(CLANG)
#pragma GCC diagnostic pop
#endif
    ASSERT(numCharactersConverted <= static_cast<int32_t>(defaultInlineBufferSize));

    if (error == U_ZERO_ERROR) {
        for (int32_t i = 0; i < numCharactersConverted; ++i) {
            ASSERT(isASCII(hostnameBuffer[i]));
            ASSERT(!isASCIIUpper(hostnameBuffer[i]));
        }
        ascii.append(hostnameBuffer, numCharactersConverted);
        return ascii;
    }

    // FIXME: Check for U_BUFFER_OVERFLOW_ERROR and retry with an allocated buffer.
    return Nullopt;
}

inline static bool hasInvalidDomainCharacter(const Vector<LChar, defaultInlineBufferSize>& asciiDomain)
{
    for (size_t i = 0; i < asciiDomain.size(); ++i) {
        if (isInvalidDomainCharacter(asciiDomain[i]))
            return true;
    }
    return false;
}

template<typename CharacterType>
bool URLParser::parsePort(CodePointIterator<CharacterType>& iterator)
{
    uint32_t port = 0;
    if (iterator.atEnd()) {
        m_url.m_portEnd = currentPosition(iterator);
        return true;
    }
    appendToASCIIBuffer(':');
    for (; !iterator.atEnd(); ++iterator) {
        if (isTabOrNewline(*iterator))
            continue;
        if (isASCIIDigit(*iterator)) {
            port = port * 10 + *iterator - '0';
            if (port > std::numeric_limits<uint16_t>::max())
                return false;
        } else
            return false;
    }

    if (isDefaultPort(StringView(m_asciiBuffer.data(), m_url.m_schemeEnd), port)) {
        ASSERT(m_asciiBuffer.last() == ':');
        m_asciiBuffer.shrink(currentPosition(iterator) - 1);
    } else {
        ASSERT(port <= std::numeric_limits<uint16_t>::max());
        appendNumberToASCIIBuffer<uint16_t>(static_cast<uint16_t>(port));
    }

    m_url.m_portEnd = currentPosition(iterator);
    return true;
}

template<typename CharacterType>
bool URLParser::parseHostAndPort(CodePointIterator<CharacterType> iterator)
{
    if (iterator.atEnd())
        return false;
    if (*iterator == '[') {
        ++iterator;
        auto ipv6End = iterator;
        while (!ipv6End.atEnd() && *ipv6End != ']')
            ++ipv6End;
        if (auto address = parseIPv6Host(CodePointIterator<CharacterType>(iterator, ipv6End))) {
            serializeIPv6(address.value());
            m_url.m_hostEnd = currentPosition(iterator);
            if (!ipv6End.atEnd()) {
                ++ipv6End;
                if (!ipv6End.atEnd() && *ipv6End == ':') {
                    ++ipv6End;
                    return parsePort(ipv6End);
                }
                m_url.m_portEnd = currentPosition(iterator);
                return true;
            }
            return true;
        }
    }
    
    if (!m_hostHasPercentOrNonASCII) {
        auto hostIterator = iterator;
        for (; !iterator.atEnd(); ++iterator) {
            if (isTabOrNewline(*iterator))
                continue;
            if (*iterator == ':')
                break;
            if (isInvalidDomainCharacter(*iterator))
                return false;
        }
        if (auto address = parseIPv4Host(CodePointIterator<CharacterType>(hostIterator, iterator))) {
            serializeIPv4(address.value());
            m_url.m_hostEnd = currentPosition(iterator);
            if (iterator.atEnd()) {
                m_url.m_portEnd = currentPosition(iterator);
                return true;
            }
            ++iterator;
            return parsePort(iterator);
        }
        for (; hostIterator != iterator; ++hostIterator) {
            if (!isTabOrNewline(*hostIterator))
                appendToASCIIBuffer(toASCIILower(*hostIterator));
        }
        m_url.m_hostEnd = currentPosition(iterator);
        if (!hostIterator.atEnd()) {
            ASSERT(*hostIterator == ':');
            incrementIteratorSkippingTabAndNewLine(hostIterator);
            return parsePort(hostIterator);
        }
        m_url.m_portEnd = currentPosition(iterator);
        return true;
    }
    
    Vector<LChar, defaultInlineBufferSize> utf8Encoded;
    for (; !iterator.atEnd(); ++iterator) {
        if (isTabOrNewline(*iterator))
            continue;
        if (*iterator == ':')
            break;
        uint8_t buffer[U8_MAX_LENGTH];
        int32_t offset = 0;
        UBool error = false;
        U8_APPEND(buffer, offset, U8_MAX_LENGTH, *iterator, error);
        ASSERT_WITH_SECURITY_IMPLICATION(offset <= static_cast<int32_t>(sizeof(buffer)));
        // FIXME: Check error.
        utf8Encoded.append(buffer, offset);
    }
    Vector<LChar, defaultInlineBufferSize> percentDecoded = percentDecode(utf8Encoded.data(), utf8Encoded.size());
    String domain = String::fromUTF8(percentDecoded.data(), percentDecoded.size());
    auto asciiDomain = domainToASCII(domain);
    if (!asciiDomain || hasInvalidDomainCharacter(asciiDomain.value()))
        return false;
    Vector<LChar, defaultInlineBufferSize>& asciiDomainValue = asciiDomain.value();
    const LChar* asciiDomainCharacters = asciiDomainValue.data();

    if (auto address = parseIPv4Host(CodePointIterator<LChar>(asciiDomainValue.begin(), asciiDomainValue.end()))) {
        serializeIPv4(address.value());
        m_url.m_hostEnd = currentPosition(iterator);
        if (iterator.atEnd()) {
            m_url.m_portEnd = currentPosition(iterator);
            return true;
        }
        ++iterator;
        return parsePort(iterator);
    }

    appendToASCIIBuffer(asciiDomainCharacters, asciiDomainValue.size());
    m_url.m_hostEnd = currentPosition(iterator);
    if (!iterator.atEnd()) {
        ASSERT(*iterator == ':');
        incrementIteratorSkippingTabAndNewLine(iterator);
        return parsePort(iterator);
    }
    m_url.m_portEnd = currentPosition(iterator);
    return true;
}

inline static Optional<String> formURLDecode(StringView input)
{
    auto utf8 = input.utf8(StrictConversion);
    if (utf8.isNull())
        return Nullopt;
    auto percentDecoded = percentDecode(reinterpret_cast<const LChar*>(utf8.data()), utf8.length());
    return String::fromUTF8(percentDecoded.data(), percentDecoded.size());
}

auto URLParser::parseURLEncodedForm(StringView input) -> URLEncodedForm
{
    Vector<StringView> sequences = input.split('&');

    URLEncodedForm output;
    for (auto& bytes : sequences) {
        auto valueStart = bytes.find('=');
        if (valueStart == notFound) {
            if (auto name = formURLDecode(bytes))
                output.append({name.value().replace('+', 0x20), emptyString()});
        } else {
            auto name = formURLDecode(bytes.substring(0, valueStart));
            auto value = formURLDecode(bytes.substring(valueStart + 1));
            if (name && value)
                output.append(std::make_pair(name.value().replace('+', 0x20), value.value().replace('+', 0x20)));
        }
    }
    return output;
}

inline static void serializeURLEncodedForm(const String& input, Vector<LChar>& output)
{
    auto utf8 = input.utf8(StrictConversion);
    const char* data = utf8.data();
    for (size_t i = 0; i < utf8.length(); ++i) {
        const char byte = data[i];
        if (byte == 0x20)
            output.append(0x2B);
        else if (byte == 0x2A
            || byte == 0x2D
            || byte == 0x2E
            || (byte >= 0x30 && byte <= 0x39)
            || (byte >= 0x41 && byte <= 0x5A)
            || byte == 0x5F
            || (byte >= 0x61 && byte <= 0x7A))
            output.append(byte);
        else
            percentEncodeByte(byte, output);
    }
}
    
String URLParser::serialize(const URLEncodedForm& tuples)
{
    Vector<LChar> output;
    for (auto& tuple : tuples) {
        if (!output.isEmpty())
            output.append('&');
        serializeURLEncodedForm(tuple.first, output);
        output.append('=');
        serializeURLEncodedForm(tuple.second, output);
    }
    return String::adopt(WTFMove(output));
}

bool URLParser::allValuesEqual(const URL& a, const URL& b)
{
    // FIXME: m_cannotBeABaseURL is not compared because the old URL::parse did not use it,
    // but once we get rid of URL::parse its value should be tested.
    LOG(URLParser, "%d %d %d %d %d %d %d %d %d %d %d %d %s\n%d %d %d %d %d %d %d %d %d %d %d %d %s",
        a.m_isValid,
        a.m_protocolIsInHTTPFamily,
        a.m_schemeEnd,
        a.m_userStart,
        a.m_userEnd,
        a.m_passwordEnd,
        a.m_hostEnd,
        a.m_portEnd,
        a.m_pathAfterLastSlash,
        a.m_pathEnd,
        a.m_queryEnd,
        a.m_fragmentEnd,
        a.m_string.utf8().data(),
        b.m_isValid,
        b.m_protocolIsInHTTPFamily,
        b.m_schemeEnd,
        b.m_userStart,
        b.m_userEnd,
        b.m_passwordEnd,
        b.m_hostEnd,
        b.m_portEnd,
        b.m_pathAfterLastSlash,
        b.m_pathEnd,
        b.m_queryEnd,
        b.m_fragmentEnd,
        b.m_string.utf8().data());

    return a.m_string == b.m_string
        && a.m_isValid == b.m_isValid
        && a.m_protocolIsInHTTPFamily == b.m_protocolIsInHTTPFamily
        && a.m_schemeEnd == b.m_schemeEnd
        && a.m_userStart == b.m_userStart
        && a.m_userEnd == b.m_userEnd
        && a.m_passwordEnd == b.m_passwordEnd
        && a.m_hostEnd == b.m_hostEnd
        && a.m_portEnd == b.m_portEnd
        && a.m_pathAfterLastSlash == b.m_pathAfterLastSlash
        && a.m_pathEnd == b.m_pathEnd
        && a.m_queryEnd == b.m_queryEnd
        && a.m_fragmentEnd == b.m_fragmentEnd;
}

bool URLParser::internalValuesConsistent(const URL& url)
{    
    return url.m_schemeEnd <= url.m_userStart
        && url.m_userStart <= url.m_userEnd
        && url.m_userEnd <= url.m_passwordEnd
        && url.m_passwordEnd <= url.m_hostEnd
        && url.m_hostEnd <= url.m_portEnd
        && url.m_portEnd <= url.m_pathAfterLastSlash
        && url.m_pathAfterLastSlash <= url.m_pathEnd
        && url.m_pathEnd <= url.m_queryEnd
        && url.m_queryEnd <= url.m_fragmentEnd
        && (url.m_isValid ? url.m_fragmentEnd == url.m_string.length() : !url.m_fragmentEnd);
    // FIXME: Why do we even store m_fragmentEnd?
    // It should be able to be deduced from m_isValid and m_string.length() to save memory.
}

static bool urlParserEnabled = false;

void URLParser::setEnabled(bool enabled)
{
    urlParserEnabled = enabled;
}

bool URLParser::enabled()
{
    return urlParserEnabled;
}

} // namespace WebCore
