// Copyright 2015 The Chromium Authors. All rights reserved.
// Copyright (C) 2016 Apple Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "config.h"
#include "CSSPropertyParser.h"

#include "CSSAspectRatioValue.h"
#include "CSSBasicShapes.h"
#include "CSSBorderImage.h"
#include "CSSBorderImageSliceValue.h"
#include "CSSContentDistributionValue.h"
#include "CSSCursorImageValue.h"
#include "CSSCustomIdentValue.h"
#include "CSSFontFaceSrcValue.h"
#include "CSSFontFeatureValue.h"
#if ENABLE(VARIATION_FONTS)
#include "CSSFontVariationValue.h"
#endif
#include "CSSFunctionValue.h"
#include "CSSGridAutoRepeatValue.h"
#include "CSSGridLineNamesValue.h"
#include "CSSGridTemplateAreasValue.h"
#include "CSSInheritedValue.h"
#include "CSSInitialValue.h"
#include "CSSLineBoxContainValue.h"
#include "CSSParserFastPaths.h"
#include "CSSParserIdioms.h"
#include "CSSPendingSubstitutionValue.h"
#include "CSSPrimitiveValueMappings.h"
#include "CSSPropertyParserHelpers.h"
#include "CSSReflectValue.h"
#include "CSSRevertValue.h"
#include "CSSShadowValue.h"
#include "CSSTimingFunctionValue.h"
#include "CSSUnicodeRangeValue.h"
#include "CSSUnsetValue.h"
#include "CSSVariableParser.h"
#include "CSSVariableReferenceValue.h"
#include "Counter.h"
#include "FontFace.h"
#include "HashTools.h"
// FIXME-NEWPARSER: Replace Pair and Rect with actual CSSValue subclasses (CSSValuePair and CSSQuadValue).
#include "Pair.h"
#include "Rect.h"
#include "RenderTheme.h"
#include "RuntimeEnabledFeatures.h"
#include "SVGPathByteStream.h"
#include "SVGPathUtilities.h"
#include "StylePropertyShorthand.h"
#include "StylePropertyShorthandFunctions.h"
#include "WebKitCSSTransformValue.h"
#include <bitset>
#include <memory>
#include <wtf/text/StringBuilder.h>

using namespace WTF;

namespace WebCore {

static bool hasPrefix(const char* string, unsigned length, const char* prefix)
{
    for (unsigned i = 0; i < length; ++i) {
        if (!prefix[i])
            return true;
        if (string[i] != prefix[i])
            return false;
    }
    return false;
}

#if PLATFORM(IOS)
static void cssPropertyNameIOSAliasing(const char* propertyName, const char*& propertyNameAlias, unsigned& newLength)
{
    if (!strcmp(propertyName, "-webkit-hyphenate-locale")) {
        // Worked in iOS 4.2.
        static const char webkitLocale[] = "-webkit-locale";
        propertyNameAlias = webkitLocale;
        newLength = strlen(webkitLocale);
    }
}
#endif

template <typename CharacterType>
static CSSPropertyID cssPropertyID(const CharacterType* propertyName, unsigned length)
{
    char buffer[maxCSSPropertyNameLength + 1 + 1]; // 1 to turn "apple"/"khtml" into "webkit", 1 for null character
    
    for (unsigned i = 0; i != length; ++i) {
        CharacterType c = propertyName[i];
        if (!c || c >= 0x7F)
            return CSSPropertyInvalid; // illegal character
        buffer[i] = toASCIILower(c);
    }
    buffer[length] = '\0';
    
    const char* name = buffer;
    if (buffer[0] == '-') {
#if ENABLE(LEGACY_CSS_VENDOR_PREFIXES)
        // If the prefix is -apple- or -khtml-, change it to -webkit-.
        // This makes the string one character longer.
        if (RuntimeEnabledFeatures::sharedFeatures().legacyCSSVendorPrefixesEnabled()
            && (hasPrefix(buffer, length, "-apple-") || hasPrefix(buffer, length, "-khtml-"))) {
            memmove(buffer + 7, buffer + 6, length + 1 - 6);
            memcpy(buffer, "-webkit", 7);
            ++length;
        }
#endif
#if PLATFORM(IOS)
        cssPropertyNameIOSAliasing(buffer, name, length);
#endif
    }
    
    const Property* hashTableEntry = findProperty(name, length);
    return hashTableEntry ? static_cast<CSSPropertyID>(hashTableEntry->id) : CSSPropertyInvalid;
}

static bool isAppleLegacyCssValueKeyword(const char* valueKeyword, unsigned length)
{
    static const char applePrefix[] = "-apple-";
    static const char appleSystemPrefix[] = "-apple-system";
    static const char* appleWirelessPlaybackTargetActive = getValueName(CSSValueAppleWirelessPlaybackTargetActive);
    
    return hasPrefix(valueKeyword, length, applePrefix)
    && !hasPrefix(valueKeyword, length, appleSystemPrefix)
    && !WTF::equal(reinterpret_cast<const LChar*>(valueKeyword), reinterpret_cast<const LChar*>(appleWirelessPlaybackTargetActive), length);
}

template <typename CharacterType>
static CSSValueID cssValueKeywordID(const CharacterType* valueKeyword, unsigned length)
{
    char buffer[maxCSSValueKeywordLength + 1 + 1]; // 1 to turn "apple"/"khtml" into "webkit", 1 for null character
    
    for (unsigned i = 0; i != length; ++i) {
        CharacterType c = valueKeyword[i];
        if (!c || c >= 0x7F)
            return CSSValueInvalid; // illegal keyword.
        buffer[i] = WTF::toASCIILower(c);
    }
    buffer[length] = '\0';
    
    if (buffer[0] == '-') {
        // If the prefix is -apple- or -khtml-, change it to -webkit-.
        // This makes the string one character longer.
        // On iOS we don't want to change values starting with -apple-system to -webkit-system.
        // FIXME: Remove this mangling without breaking the web.
        if (isAppleLegacyCssValueKeyword(buffer, length) || hasPrefix(buffer, length, "-khtml-")) {
            memmove(buffer + 7, buffer + 6, length + 1 - 6);
            memcpy(buffer, "-webkit", 7);
            ++length;
        }
    }
    
    const Value* hashTableEntry = findValue(buffer, length);
    return hashTableEntry ? static_cast<CSSValueID>(hashTableEntry->id) : CSSValueInvalid;
}

CSSValueID cssValueKeywordID(StringView string)
{
    unsigned length = string.length();
    if (!length)
        return CSSValueInvalid;
    if (length > maxCSSValueKeywordLength)
        return CSSValueInvalid;
    
    return string.is8Bit() ? cssValueKeywordID(string.characters8(), length) : cssValueKeywordID(string.characters16(), length);
}

CSSPropertyID cssPropertyID(StringView string)
{
    unsigned length = string.length();
    
    if (!length)
        return CSSPropertyInvalid;
    if (length > maxCSSPropertyNameLength)
        return CSSPropertyInvalid;
    
    return string.is8Bit() ? cssPropertyID(string.characters8(), length) : cssPropertyID(string.characters16(), length);
}
    
using namespace CSSPropertyParserHelpers;

CSSPropertyParser::CSSPropertyParser(const CSSParserTokenRange& range,
    const CSSParserContext& context, Vector<CSSProperty, 256>* parsedProperties)
    : m_range(range)
    , m_context(context)
    , m_parsedProperties(parsedProperties)
{
    m_range.consumeWhitespace();
}

void CSSPropertyParser::addProperty(CSSPropertyID property, CSSPropertyID currentShorthand, Ref<CSSValue>&& value, bool important, bool implicit)
{
    int shorthandIndex = 0;
    bool setFromShorthand = false;

    if (currentShorthand) {
        auto shorthands = matchingShorthandsForLonghand(property);
        setFromShorthand = true;
        if (shorthands.size() > 1)
            shorthandIndex = indexOfShorthandForLonghand(currentShorthand, shorthands);
    }

    m_parsedProperties->append(CSSProperty(property, WTFMove(value), important, setFromShorthand, shorthandIndex, implicit));
}

void CSSPropertyParser::addExpandedPropertyForValue(CSSPropertyID property, Ref<CSSValue>&& value, bool important)
{
    const StylePropertyShorthand& shorthand = shorthandForProperty(property);
    unsigned shorthandLength = shorthand.length();
    ASSERT(shorthandLength);
    const CSSPropertyID* longhands = shorthand.properties();
    for (unsigned i = 0; i < shorthandLength; ++i)
        addProperty(longhands[i], property, value.copyRef(), important);
}
    
bool CSSPropertyParser::parseValue(CSSPropertyID propertyID, bool important,
    const CSSParserTokenRange& range, const CSSParserContext& context,
    ParsedPropertyVector& parsedProperties, StyleRule::Type ruleType)
{
    int parsedPropertiesSize = parsedProperties.size();

    CSSPropertyParser parser(range, context, &parsedProperties);
    bool parseSuccess;

#if ENABLE(CSS_DEVICE_ADAPTATION)
    if (ruleType == StyleRule::Viewport)
        parseSuccess = parser.parseViewportDescriptor(propertyID, important);
    else
#endif
    if (ruleType == StyleRule::FontFace)
        parseSuccess = parser.parseFontFaceDescriptor(propertyID);
    else
        parseSuccess = parser.parseValueStart(propertyID, important);

    if (!parseSuccess)
        parsedProperties.shrink(parsedPropertiesSize);

    return parseSuccess;
}

RefPtr<CSSValue> CSSPropertyParser::parseSingleValue(CSSPropertyID property, const CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSPropertyParser parser(range, context, nullptr);
    RefPtr<CSSValue> value = parser.parseSingleValue(property);
    if (!value || !parser.m_range.atEnd())
        return nullptr;
    return value;
}

static bool isLegacyBreakProperty(CSSPropertyID propertyID)
{
    switch (propertyID) {
    case CSSPropertyPageBreakAfter:
    case CSSPropertyPageBreakBefore:
    case CSSPropertyPageBreakInside:
    case CSSPropertyWebkitColumnBreakAfter:
    case CSSPropertyWebkitColumnBreakBefore:
    case CSSPropertyWebkitColumnBreakInside:
#if ENABLE(CSS_REGIONS)
    case CSSPropertyWebkitRegionBreakAfter:
    case CSSPropertyWebkitRegionBreakBefore:
    case CSSPropertyWebkitRegionBreakInside:
#endif
        return true;
    default:
        break;
    }
    return false;
}

bool CSSPropertyParser::parseValueStart(CSSPropertyID propertyID, bool important)
{
    if (consumeCSSWideKeyword(propertyID, important))
        return true;

    CSSParserTokenRange originalRange = m_range;
    bool isShorthand = isShorthandCSSProperty(propertyID);

    if (isShorthand) {
        // Variable references will fail to parse here and will fall out to the variable ref parser below.
        if (parseShorthand(propertyID, important))
            return true;
    } else if (isLegacyBreakProperty(propertyID)) {
        // FIXME-NEWPARSER: Can turn this into a shorthand once old parser is gone, and then
        // we don't need the special case.
        if (consumeLegacyBreakProperty(propertyID, important))
            return true;
    } else {
        RefPtr<CSSValue> parsedValue = parseSingleValue(propertyID);
        if (parsedValue && m_range.atEnd()) {
            addProperty(propertyID, CSSPropertyInvalid, *parsedValue, important);
            return true;
        }
    }

    if (CSSVariableParser::containsValidVariableReferences(originalRange)) {
        RefPtr<CSSVariableReferenceValue> variable = CSSVariableReferenceValue::create(CSSVariableData::create(originalRange));

        if (isShorthand) {
            RefPtr<CSSPendingSubstitutionValue> pendingValue = CSSPendingSubstitutionValue::create(propertyID, variable.releaseNonNull());
            addExpandedPropertyForValue(propertyID, pendingValue.releaseNonNull(), important);
        } else
            addProperty(propertyID, CSSPropertyInvalid, variable.releaseNonNull(), important);
        return true;
    }

    return false;
}
 
bool CSSPropertyParser::consumeCSSWideKeyword(CSSPropertyID propertyID, bool important)
{
    CSSParserTokenRange rangeCopy = m_range;
    CSSValueID valueID = rangeCopy.consumeIncludingWhitespace().id();
    if (!rangeCopy.atEnd())
        return false;

    RefPtr<CSSValue> value;
    if (valueID == CSSValueInherit)
        value = CSSValuePool::singleton().createInheritedValue();
    else if (valueID == CSSValueInitial)
        value = CSSValuePool::singleton().createExplicitInitialValue();
    else if (valueID == CSSValueUnset)
        value = CSSValuePool::singleton().createUnsetValue();
    else if (valueID == CSSValueRevert)
        value = CSSValuePool::singleton().createRevertValue();
    else
        return false;
    
    const StylePropertyShorthand& shorthand = shorthandForProperty(propertyID);
    if (!shorthand.length()) {
        if (CSSProperty::isDescriptorOnly(propertyID))
            return false;
        addProperty(propertyID, CSSPropertyInvalid, value.releaseNonNull(), important);
    } else
        addExpandedPropertyForValue(propertyID, value.releaseNonNull(), important);
    m_range = rangeCopy;
    return true;
}

bool CSSPropertyParser::consumeTransformOrigin(bool important)
{
    RefPtr<CSSPrimitiveValue> resultX;
    RefPtr<CSSPrimitiveValue> resultY;
    if (consumeOneOrTwoValuedPosition(m_range, m_context.mode, UnitlessQuirk::Forbid, resultX, resultY)) {
        m_range.consumeWhitespace();
        bool atEnd = m_range.atEnd();
        RefPtr<CSSPrimitiveValue> resultZ = consumeLength(m_range, m_context.mode, ValueRangeAll);
        if (!resultZ && !atEnd)
            return false;
        if (!resultZ)
            resultZ = CSSValuePool::singleton().createValue(0, CSSPrimitiveValue::UnitTypes::CSS_PX);
        addProperty(CSSPropertyTransformOriginX, CSSPropertyTransformOrigin, resultX.releaseNonNull(), important);
        addProperty(CSSPropertyTransformOriginY, CSSPropertyTransformOrigin, resultY.releaseNonNull(), important);
        addProperty(CSSPropertyTransformOriginZ, CSSPropertyTransformOrigin, resultZ.releaseNonNull(), important);
        
        return true;
    }
    return false;
}

bool CSSPropertyParser::consumePerspectiveOrigin(bool important)
{
    RefPtr<CSSPrimitiveValue> resultX;
    RefPtr<CSSPrimitiveValue> resultY;
    if (consumePosition(m_range, m_context.mode, UnitlessQuirk::Forbid, resultX, resultY)) {
        addProperty(CSSPropertyPerspectiveOriginX, CSSPropertyPerspectiveOrigin, resultX.releaseNonNull(), important);
        addProperty(CSSPropertyPerspectiveOriginY, CSSPropertyPerspectiveOrigin, resultY.releaseNonNull(), important);
        return true;
    }
    return false;
}

// Methods for consuming non-shorthand properties starts here.
static RefPtr<CSSValue> consumeWillChange(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);

    RefPtr<CSSValueList> values = CSSValueList::createCommaSeparated();
    // Every comma-separated list of identifiers is a valid will-change value,
    // unless the list includes an explicitly disallowed identifier.
    while (true) {
        if (range.peek().type() != IdentToken)
            return nullptr;
        CSSPropertyID propertyID = cssPropertyID(range.peek().value());
        if (propertyID != CSSPropertyInvalid) {
            // Now "all" is used by both CSSValue and CSSPropertyValue.
            // Need to return nullptr when currentValue is CSSPropertyAll.
            if (propertyID == CSSPropertyWillChange || propertyID == CSSPropertyAll)
                return nullptr;
            // FIXME-NEWPARSER: Use CSSCustomIdentValue someday.
            values->append(CSSValuePool::singleton().createIdentifierValue(propertyID));
            range.consumeIncludingWhitespace();
        } else {
            switch (range.peek().id()) {
            case CSSValueNone:
            case CSSValueAll:
            case CSSValueAuto:
            case CSSValueDefault:
            case CSSValueInitial:
            case CSSValueInherit:
                return nullptr;
            case CSSValueContents:
            case CSSValueScrollPosition:
                values->append(consumeIdent(range).releaseNonNull());
                break;
            default:
                // Append properties we don't recognize, but that are legal, as strings.
                values->append(consumeCustomIdent(range).releaseNonNull());
                break;
            }
        }

        if (range.atEnd())
            break;
        if (!consumeCommaIncludingWhitespace(range))
            return nullptr;
    }

    return values;
}

static RefPtr<CSSFontFeatureValue> consumeFontFeatureTag(CSSParserTokenRange& range)
{
    // Feature tag name consists of 4-letter characters.
    static const unsigned tagNameLength = 4;

    const CSSParserToken& token = range.consumeIncludingWhitespace();
    // Feature tag name comes first
    if (token.type() != StringToken)
        return nullptr;
    if (token.value().length() != tagNameLength)
        return nullptr;
    
    FontTag tag;
    for (unsigned i = 0; i < tag.size(); ++i) {
        // Limits the range of characters to 0x20-0x7E, following the tag name rules defiend in the OpenType specification.
        UChar character = token.value()[i];
        if (character < 0x20 || character > 0x7E)
            return nullptr;
        tag[i] = toASCIILower(character);
    }

    int tagValue = 1;
    // Feature tag values could follow: <integer> | on | off
    if (range.peek().type() == NumberToken && range.peek().numericValueType() == IntegerValueType && range.peek().numericValue() >= 0) {
        tagValue = clampTo<int>(range.consumeIncludingWhitespace().numericValue());
        if (tagValue < 0)
            return nullptr;
    } else if (range.peek().id() == CSSValueOn || range.peek().id() == CSSValueOff) {
        tagValue = range.consumeIncludingWhitespace().id() == CSSValueOn;
    }
    return CSSFontFeatureValue::create(WTFMove(tag), tagValue);
}

static RefPtr<CSSValue> consumeFontFeatureSettings(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);
    RefPtr<CSSValueList> settings = CSSValueList::createCommaSeparated();
    do {
        RefPtr<CSSFontFeatureValue> fontFeatureValue = consumeFontFeatureTag(range);
        if (!fontFeatureValue)
            return nullptr;
        settings->append(fontFeatureValue.releaseNonNull());
    } while (consumeCommaIncludingWhitespace(range));
    return settings;
}

#if ENABLE(VARIATION_FONTS)
static RefPtr<CSSValue> consumeFontVariationTag(CSSParserTokenRange& range)
{
    if (range.peek().type() != StringToken)
        return nullptr;
    
    auto string = range.consumeIncludingWhitespace().value().toString();
    
    FontTag tag;
    if (string.length() != tag.size())
        return nullptr;
    for (unsigned i = 0; i < tag.size(); ++i) {
        // Limits the range of characters to 0x20-0x7E, following the tag name rules defiend in the OpenType specification.
        UChar character = string[i];
        if (character < 0x20 || character > 0x7E)
            return nullptr;
        tag[i] = character;
    }
    
    if (range.atEnd() || range.peek().type() != NumberToken)
        return nullptr;

    float tagValue = range.consumeIncludingWhitespace().numericValue();
    
    return CSSFontVariationValue::create(tag, tagValue);
}
    
static RefPtr<CSSValue> consumeFontVariationSettings(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);
    
    auto settings = CSSValueList::createCommaSeparated();
    do {
        RefPtr<CSSValue> variationValue = consumeFontVariationTag(range);
        if (!variationValue)
            return nullptr;
        settings->append(variationValue.releaseNonNull());
    } while (consumeCommaIncludingWhitespace(range));
    
    if (!settings->length())
        return nullptr;

    return WTFMove(settings);
}
#endif // ENABLE(VARIATION_FONTS)

static RefPtr<CSSValue> consumePage(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeCustomIdent(range);
}

static RefPtr<CSSValue> consumeQuotes(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    RefPtr<CSSValueList> values = CSSValueList::createSpaceSeparated();
    while (!range.atEnd()) {
        RefPtr<CSSPrimitiveValue> parsedValue = consumeString(range);
        if (!parsedValue)
            return nullptr;
        values->append(parsedValue.releaseNonNull());
    }
    if (values->length() && values->length() % 2 == 0)
        return values;
    return nullptr;
}

class FontVariantLigaturesParser {
public:
    FontVariantLigaturesParser()
        : m_sawCommonLigaturesValue(false)
        , m_sawDiscretionaryLigaturesValue(false)
        , m_sawHistoricalLigaturesValue(false)
        , m_sawContextualLigaturesValue(false)
        , m_result(CSSValueList::createSpaceSeparated())
    {
    }

    enum class ParseResult {
        ConsumedValue,
        DisallowedValue,
        UnknownValue
    };

    ParseResult consumeLigature(CSSParserTokenRange& range)
    {
        CSSValueID valueID = range.peek().id();
        switch (valueID) {
        case CSSValueNoCommonLigatures:
        case CSSValueCommonLigatures:
            if (m_sawCommonLigaturesValue)
                return ParseResult::DisallowedValue;
            m_sawCommonLigaturesValue = true;
            break;
        case CSSValueNoDiscretionaryLigatures:
        case CSSValueDiscretionaryLigatures:
            if (m_sawDiscretionaryLigaturesValue)
                return ParseResult::DisallowedValue;
            m_sawDiscretionaryLigaturesValue = true;
            break;
        case CSSValueNoHistoricalLigatures:
        case CSSValueHistoricalLigatures:
            if (m_sawHistoricalLigaturesValue)
                return ParseResult::DisallowedValue;
            m_sawHistoricalLigaturesValue = true;
            break;
        case CSSValueNoContextual:
        case CSSValueContextual:
            if (m_sawContextualLigaturesValue)
                return ParseResult::DisallowedValue;
            m_sawContextualLigaturesValue = true;
            break;
        default:
            return ParseResult::UnknownValue;
        }
        m_result->append(consumeIdent(range).releaseNonNull());
        return ParseResult::ConsumedValue;
    }

    RefPtr<CSSValue> finalizeValue()
    {
        if (!m_result->length())
            return CSSValuePool::singleton().createIdentifierValue(CSSValueNormal);
        return m_result.release();
    }

private:
    bool m_sawCommonLigaturesValue;
    bool m_sawDiscretionaryLigaturesValue;
    bool m_sawHistoricalLigaturesValue;
    bool m_sawContextualLigaturesValue;
    RefPtr<CSSValueList> m_result;
};

static RefPtr<CSSValue> consumeFontVariantLigatures(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNormal || range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    FontVariantLigaturesParser ligaturesParser;
    do {
        if (ligaturesParser.consumeLigature(range) !=
            FontVariantLigaturesParser::ParseResult::ConsumedValue)
            return nullptr;
    } while (!range.atEnd());

    return ligaturesParser.finalizeValue();
}

static RefPtr<CSSPrimitiveValue> consumeFontVariantCaps(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueNormal, CSSValueSmallCaps, CSSValueAllSmallCaps,
        CSSValuePetiteCaps, CSSValueAllPetiteCaps,
        CSSValueUnicase, CSSValueTitlingCaps>(range);
}

class FontVariantNumericParser {
public:
    FontVariantNumericParser()
        : m_sawNumericFigureValue(false)
        , m_sawNumericSpacingValue(false)
        , m_sawNumericFractionValue(false)
        , m_sawOrdinalValue(false)
        , m_sawSlashedZeroValue(false)
        , m_result(CSSValueList::createSpaceSeparated())
    {
    }

    enum class ParseResult {
        ConsumedValue,
        DisallowedValue,
        UnknownValue
    };

    ParseResult consumeNumeric(CSSParserTokenRange& range)
    {
        CSSValueID valueID = range.peek().id();
        switch (valueID) {
        case CSSValueLiningNums:
        case CSSValueOldstyleNums:
            if (m_sawNumericFigureValue)
                return ParseResult::DisallowedValue;
            m_sawNumericFigureValue = true;
            break;
        case CSSValueProportionalNums:
        case CSSValueTabularNums:
            if (m_sawNumericSpacingValue)
                return ParseResult::DisallowedValue;
            m_sawNumericSpacingValue = true;
            break;
        case CSSValueDiagonalFractions:
        case CSSValueStackedFractions:
            if (m_sawNumericFractionValue)
                return ParseResult::DisallowedValue;
            m_sawNumericFractionValue = true;
            break;
        case CSSValueOrdinal:
            if (m_sawOrdinalValue)
                return ParseResult::DisallowedValue;
            m_sawOrdinalValue = true;
            break;
        case CSSValueSlashedZero:
            if (m_sawSlashedZeroValue)
                return ParseResult::DisallowedValue;
            m_sawSlashedZeroValue = true;
            break;
        default:
            return ParseResult::UnknownValue;
        }
        m_result->append(consumeIdent(range).releaseNonNull());
        return ParseResult::ConsumedValue;
    }

    RefPtr<CSSValue> finalizeValue()
    {
        if (!m_result->length())
            return CSSValuePool::singleton().createIdentifierValue(CSSValueNormal);
        return m_result.release();
    }


private:
    bool m_sawNumericFigureValue;
    bool m_sawNumericSpacingValue;
    bool m_sawNumericFractionValue;
    bool m_sawOrdinalValue;
    bool m_sawSlashedZeroValue;
    RefPtr<CSSValueList> m_result;
};

static RefPtr<CSSValue> consumeFontVariantNumeric(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);

    FontVariantNumericParser numericParser;
    do {
        if (numericParser.consumeNumeric(range) !=
            FontVariantNumericParser::ParseResult::ConsumedValue)
            return nullptr;
    } while (!range.atEnd());

    return numericParser.finalizeValue();
}

static RefPtr<CSSPrimitiveValue> consumeFontVariantCSS21(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueNormal, CSSValueSmallCaps>(range);
}

static RefPtr<CSSValue> consumeFontVariantList(CSSParserTokenRange& range)
{
    RefPtr<CSSValueList> values = CSSValueList::createCommaSeparated();
    do {
        if (range.peek().id() == CSSValueAll) {
            // FIXME: CSSPropertyParser::parseFontVariant() implements
            // the old css3 draft:
            // http://www.w3.org/TR/2002/WD-css3-webfonts-20020802/#font-variant
            // 'all' is only allowed in @font-face and with no other values.
            if (values->length())
                return nullptr;
            return consumeIdent(range);
        }
        RefPtr<CSSPrimitiveValue> fontVariant = consumeFontVariantCSS21(range);
        if (fontVariant)
            values->append(fontVariant.releaseNonNull());
    } while (consumeCommaIncludingWhitespace(range));

    if (values->length())
        return values;

    return nullptr;
}

static RefPtr<CSSPrimitiveValue> consumeFontWeight(CSSParserTokenRange& range)
{
    const CSSParserToken& token = range.peek();
    if (token.id() >= CSSValueNormal && token.id() <= CSSValueLighter)
        return consumeIdent(range);
    if (token.type() != NumberToken || token.numericValueType() != IntegerValueType)
        return nullptr;
    int weight = static_cast<int>(token.numericValue());
    if ((weight % 100) || weight < 100 || weight > 900)
        return nullptr;
    range.consumeIncludingWhitespace();
    return CSSValuePool::singleton().createIdentifierValue(static_cast<CSSValueID>(CSSValue100 + weight / 100 - 1));
}

static String concatenateFamilyName(CSSParserTokenRange& range)
{
    StringBuilder builder;
    bool addedSpace = false;
    const CSSParserToken& firstToken = range.peek();
    while (range.peek().type() == IdentToken) {
        if (!builder.isEmpty()) {
            builder.append(' ');
            addedSpace = true;
        }
        builder.append(range.consumeIncludingWhitespace().value());
    }
    if (!addedSpace && isCSSWideKeyword(firstToken.id()))
        return String();
    return builder.toString();
}

static RefPtr<CSSValue> consumeFamilyName(CSSParserTokenRange& range)
{
    if (range.peek().type() == StringToken)
        return CSSValuePool::singleton().createFontFamilyValue(range.consumeIncludingWhitespace().value().toString());
    if (range.peek().type() != IdentToken)
        return nullptr;
    String familyName = concatenateFamilyName(range);
    if (familyName.isNull())
        return nullptr;
    return CSSValuePool::singleton().createFontFamilyValue(familyName);
}

static RefPtr<CSSValue> consumeGenericFamily(CSSParserTokenRange& range)
{
    return consumeIdentRange(range, CSSValueSerif, CSSValueWebkitBody);
}

static RefPtr<CSSValueList> consumeFontFamily(CSSParserTokenRange& range)
{
    RefPtr<CSSValueList> list = CSSValueList::createCommaSeparated();
    do {
        RefPtr<CSSValue> parsedValue = consumeGenericFamily(range);
        if (parsedValue) {
            list->append(parsedValue.releaseNonNull());
        } else {
            parsedValue = consumeFamilyName(range);
            if (parsedValue) {
                list->append(parsedValue.releaseNonNull());
            } else {
                return nullptr;
            }
        }
    } while (consumeCommaIncludingWhitespace(range));
    return list;
}

static RefPtr<CSSValue> consumeFontSynthesis(CSSParserTokenRange& range)
{
    // none | [ weight || style ]
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);
    
    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    while (true) {
        auto ident = consumeIdent<CSSValueWeight, CSSValueStyle>(range);
        if (!ident)
            break;
        if (list->hasValue(ident.get()))
            return nullptr;
        list->append(ident.releaseNonNull());
    }
    
    if (!list->length())
        return nullptr;
    return list;
}

static RefPtr<CSSValue> consumeLetterSpacing(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);
    
    return consumeLength(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow);
}

static RefPtr<CSSValue> consumeWordSpacing(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);
    
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow);
}
    
static RefPtr<CSSValue> consumeTabSize(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    RefPtr<CSSPrimitiveValue> parsedValue = consumeInteger(range, 0);
    if (parsedValue)
        return parsedValue;
    return consumeLength(range, cssParserMode, ValueRangeNonNegative);
}

#if ENABLE(TEXT_AUTOSIZING)
static RefPtr<CSSValue> consumeTextSizeAdjust(CSSParserTokenRange& range, CSSParserMode /* cssParserMode */)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumePercent(range, ValueRangeNonNegative);
}
#endif

static RefPtr<CSSValue> consumeFontSize(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless = UnitlessQuirk::Forbid)
{
    if (range.peek().id() >= CSSValueXxSmall && range.peek().id() <= CSSValueLarger)
        return consumeIdent(range);
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative, unitless);
}

static RefPtr<CSSPrimitiveValue> consumeLineHeight(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);

    RefPtr<CSSPrimitiveValue> lineHeight = consumeNumber(range, ValueRangeNonNegative);
    if (lineHeight)
        return lineHeight;
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
}

template<typename... Args>
static Ref<CSSPrimitiveValue> createPrimitiveValuePair(Args&&... args)
{
    return CSSValuePool::singleton().createValue(Pair::create(std::forward<Args>(args)...));
}

static RefPtr<CSSValue> consumeCounter(CSSParserTokenRange& range, int defaultValue)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    do {
        RefPtr<CSSPrimitiveValue> counterName = consumeCustomIdent(range);
        if (!counterName)
            return nullptr;
        int i = defaultValue;
        if (RefPtr<CSSPrimitiveValue> counterValue = consumeInteger(range))
            i = counterValue->intValue();
        list->append(createPrimitiveValuePair(counterName.releaseNonNull(), CSSPrimitiveValue::create(i, CSSPrimitiveValue::UnitTypes::CSS_NUMBER), Pair::IdenticalValueEncoding::Coalesce));
    } while (!range.atEnd());
    return list;
}

static RefPtr<CSSValue> consumePageSize(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueA3, CSSValueA4, CSSValueA5, CSSValueB4, CSSValueB5, CSSValueLedger, CSSValueLegal, CSSValueLetter>(range);
}

static RefPtr<CSSValueList> consumeSize(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    RefPtr<CSSValueList> result = CSSValueList::createSpaceSeparated();

    if (range.peek().id() == CSSValueAuto) {
        result->append(consumeIdent(range).releaseNonNull());
        return result;
    }

    if (RefPtr<CSSValue> width = consumeLength(range, cssParserMode, ValueRangeNonNegative)) {
        RefPtr<CSSValue> height = consumeLength(range, cssParserMode, ValueRangeNonNegative);
        result->append(width.releaseNonNull());
        if (height)
            result->append(height.releaseNonNull());
        return result;
    }

    RefPtr<CSSValue> pageSize = consumePageSize(range);
    RefPtr<CSSValue> orientation = consumeIdent<CSSValuePortrait, CSSValueLandscape>(range);
    if (!pageSize)
        pageSize = consumePageSize(range);

    if (!orientation && !pageSize)
        return nullptr;
    if (pageSize)
        result->append(pageSize.releaseNonNull());
    if (orientation)
        result->append(orientation.releaseNonNull());
    return result;
}

static RefPtr<CSSValue> consumeTextIndent(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    // [ <length> | <percentage> ] && hanging? && each-line?
    // Keywords only allowed when css3Text is enabled.
    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();

    bool hasLengthOrPercentage = false;
//    bool hasEachLine = false;
    bool hasHanging = false;

    do {
        if (!hasLengthOrPercentage) {
            if (RefPtr<CSSValue> textIndent = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow)) {
                list->append(*textIndent);
                hasLengthOrPercentage = true;
                continue;
            }
        }

        CSSValueID id = range.peek().id();
 /* FIXME-NEWPARSER: We don't support this yet.
        if (!hasEachLine && id == CSSValueEachLine) {
            list->append(*consumeIdent(range));
            hasEachLine = true;
            continue;
        }
*/
        
        if (!hasHanging && id == CSSValueHanging) {
            list->append(consumeIdent(range).releaseNonNull());
            hasHanging = true;
            continue;
        }
        
        return nullptr;
    } while (!range.atEnd());

    if (!hasLengthOrPercentage)
        return nullptr;

    return list;
}

// FIXME-NEWPARSER: Drop the prefix on min-content, max-content and fit-content.
static bool validWidthOrHeightKeyword(CSSValueID id, const CSSParserContext& /*context*/)
{
    if (id == CSSValueIntrinsic || id == CSSValueMinIntrinsic || id == CSSValueWebkitMinContent || id == CSSValueWebkitMaxContent || id == CSSValueWebkitFillAvailable || id == CSSValueWebkitFitContent) {
        return true;
    }
    return false;
}

static RefPtr<CSSValue> consumeMaxWidthOrHeight(CSSParserTokenRange& range, const CSSParserContext& context, UnitlessQuirk unitless = UnitlessQuirk::Forbid)
{
    if (range.peek().id() == CSSValueNone || validWidthOrHeightKeyword(range.peek().id(), context))
        return consumeIdent(range);
    return consumeLengthOrPercent(range, context.mode, ValueRangeNonNegative, unitless);
}

static RefPtr<CSSValue> consumeWidthOrHeight(CSSParserTokenRange& range, const CSSParserContext& context, UnitlessQuirk unitless = UnitlessQuirk::Forbid)
{
    if (range.peek().id() == CSSValueAuto || validWidthOrHeightKeyword(range.peek().id(), context))
        return consumeIdent(range);
    return consumeLengthOrPercent(range, context.mode, ValueRangeNonNegative, unitless);
}

static RefPtr<CSSValue> consumeMarginOrOffset(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, unitless);
}

static RefPtr<CSSPrimitiveValue> consumeClipComponent(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeLength(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow);
}

static RefPtr<CSSValue> consumeClip(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);

    if (range.peek().functionId() != CSSValueRect)
        return nullptr;

    CSSParserTokenRange args = consumeFunction(range);
    // rect(t, r, b, l) || rect(t r b l)
    RefPtr<CSSPrimitiveValue> top = consumeClipComponent(args, cssParserMode);
    if (!top)
        return nullptr;
    bool needsComma = consumeCommaIncludingWhitespace(args);
    RefPtr<CSSPrimitiveValue> right = consumeClipComponent(args, cssParserMode);
    if (!right || (needsComma && !consumeCommaIncludingWhitespace(args)))
        return nullptr;
    RefPtr<CSSPrimitiveValue> bottom = consumeClipComponent(args, cssParserMode);
    if (!bottom || (needsComma && !consumeCommaIncludingWhitespace(args)))
        return nullptr;
    RefPtr<CSSPrimitiveValue> left = consumeClipComponent(args, cssParserMode);
    if (!left || !args.atEnd())
        return nullptr;
    
    auto rect = Rect::create();
    rect->setLeft(left.releaseNonNull());
    rect->setTop(top.releaseNonNull());
    rect->setRight(right.releaseNonNull());
    rect->setBottom(bottom.releaseNonNull());
    return CSSValuePool::singleton().createValue(WTFMove(rect));
}

#if ENABLE(TOUCH_EVENTS)
static RefPtr<CSSValue> consumeTouchAction(CSSParserTokenRange& range)
{
    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    CSSValueID id = range.peek().id();
    if (id == CSSValueAuto || id == CSSValueNone || id == CSSValueManipulation) {
        list->append(consumeIdent(range).releaseNonNull());
        return list;
    }
    // FIXME-NEWPARSER: Support pan.
    return nullptr;
}
#endif

static RefPtr<CSSPrimitiveValue> consumeLineClamp(CSSParserTokenRange& range)
{
    if (range.peek().type() != PercentageToken && range.peek().type() != NumberToken)
        return nullptr;
    RefPtr<CSSPrimitiveValue> clampValue = consumePercent(range, ValueRangeNonNegative);
    if (clampValue)
        return clampValue;
    // When specifying number of lines, don't allow 0 as a valid value.
    return consumePositiveInteger(range);
}

static RefPtr<CSSValue> consumeLocale(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeString(range);
}

static RefPtr<CSSValue> consumeHyphenateLimit(CSSParserTokenRange& range, CSSValueID valueID)
{
    if (range.peek().id() == valueID)
        return consumeIdent(range);
    return consumeNumber(range, ValueRangeNonNegative);
}

static RefPtr<CSSValue> consumeColumnWidth(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    // Always parse lengths in strict mode here, since it would be ambiguous otherwise when used in
    // the 'columns' shorthand property.
    RefPtr<CSSPrimitiveValue> columnWidth = consumeLength(range, HTMLStandardMode, ValueRangeNonNegative);
    if (!columnWidth || (!columnWidth->isCalculated() && columnWidth->doubleValue() == 0))
        return nullptr;
    return columnWidth;
}

static RefPtr<CSSValue> consumeColumnCount(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumePositiveInteger(range);
}

static RefPtr<CSSValue> consumeColumnGap(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);
    return consumeLength(range, cssParserMode, ValueRangeNonNegative);
}

static RefPtr<CSSValue> consumeColumnSpan(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueAll, CSSValueNone>(range);
}

static RefPtr<CSSValue> consumeZoom(CSSParserTokenRange& range, const CSSParserContext& /*context*/)
{
    const CSSParserToken& token = range.peek();
    RefPtr<CSSPrimitiveValue> zoom;
    if (token.type() == IdentToken)
        zoom = consumeIdent<CSSValueNormal, CSSValueReset, CSSValueDocument>(range);
    else {
        zoom = consumePercent(range, ValueRangeNonNegative);
        if (!zoom)
            zoom = consumeNumber(range, ValueRangeNonNegative);
    }
    return zoom;
}

static RefPtr<CSSValue> consumeAnimationIterationCount(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueInfinite)
        return consumeIdent(range);
    return consumeNumber(range, ValueRangeNonNegative);
}

static RefPtr<CSSValue> consumeAnimationName(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    if (range.peek().type() == StringToken) {
        const CSSParserToken& token = range.consumeIncludingWhitespace();
        if (equalIgnoringASCIICase(token.value(), "none"))
            return CSSValuePool::singleton().createIdentifierValue(CSSValueNone);
        // FIXME-NEWPARSER: Want to use a CSSCustomIdentValue here eventually.
        return CSSValuePool::singleton().createValue(token.value().toString(), CSSPrimitiveValue::UnitTypes::CSS_STRING);
    }

    return consumeCustomIdent(range);
}

static RefPtr<CSSValue> consumeTransitionProperty(CSSParserTokenRange& range)
{
    const CSSParserToken& token = range.peek();
    if (token.type() != IdentToken)
        return nullptr;
    if (token.id() == CSSValueNone)
        return consumeIdent(range);

    if (CSSPropertyID property = token.parseAsCSSPropertyID()) {
        range.consumeIncludingWhitespace();
        
        // FIXME-NEWPARSER: Want to use a CSSCustomIdentValue here eventually.
        return CSSValuePool::singleton().createIdentifierValue(property);
    }
    return consumeCustomIdent(range);
}

    
static RefPtr<CSSValue> consumeSteps(CSSParserTokenRange& range) {
    ASSERT(range.peek().functionId() == CSSValueSteps);
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);
    
    RefPtr<CSSPrimitiveValue> steps = consumePositiveInteger(args);
    if (!steps)
        return nullptr;
    
    // FIXME-NEWPARSER: Support the middle value and change from a boolean to an enum.
    bool stepAtStart = false;
    if (consumeCommaIncludingWhitespace(args)) {
        switch (args.consumeIncludingWhitespace().id()) {
            case CSSValueStart:
                stepAtStart = true;
            break;
            case CSSValueEnd:
                stepAtStart = false;
                break;
            default:
                return nullptr;
        }
    }
    
    if (!args.atEnd())
        return nullptr;
    
    range = rangeCopy;
    return CSSStepsTimingFunctionValue::create(steps->intValue(), stepAtStart);
}

static RefPtr<CSSValue> consumeCubicBezier(CSSParserTokenRange& range)
{
    ASSERT(range.peek().functionId() == CSSValueCubicBezier);
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);

    double x1, y1, x2, y2;
    if (consumeNumberRaw(args, x1)
        && x1 >= 0 && x1 <= 1
        && consumeCommaIncludingWhitespace(args)
        && consumeNumberRaw(args, y1)
        && consumeCommaIncludingWhitespace(args)
        && consumeNumberRaw(args, x2)
        && x2 >= 0 && x2 <= 1
        && consumeCommaIncludingWhitespace(args)
        && consumeNumberRaw(args, y2)
        && args.atEnd()) {
        range = rangeCopy;
        return CSSCubicBezierTimingFunctionValue::create(x1, y1, x2, y2);
    }

    return nullptr;
}

static RefPtr<CSSValue> consumeSpringFunction(CSSParserTokenRange& range)
{
    ASSERT(range.peek().functionId() == CSSValueSpring);
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);

    // Mass must be greater than 0.
    double mass;
    if (!consumeNumberRaw(args, mass) || mass <= 0)
        return nullptr;
    
    // Stiffness must be greater than 0.
    double stiffness;
    if (!consumeNumberRaw(args, stiffness) || stiffness <= 0)
        return nullptr;
    
    // Damping coefficient must be greater than or equal to 0.
    double damping;
    if (!consumeNumberRaw(args, damping) || damping < 0)
        return nullptr;
    
    // Initial velocity may have any value.
    double initialVelocity;
    if (!consumeNumberRaw(args, initialVelocity))
        return nullptr;

    if (!args.atEnd())
        return nullptr;

    range = rangeCopy;

    return CSSSpringTimingFunctionValue::create(mass, stiffness, damping, initialVelocity);
}

static RefPtr<CSSValue> consumeAnimationTimingFunction(CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueEase || id == CSSValueLinear || id == CSSValueEaseIn
        || id == CSSValueEaseOut || id == CSSValueEaseInOut || id == CSSValueStepStart || id == CSSValueStepEnd)
        return consumeIdent(range);

    CSSValueID function = range.peek().functionId();
    if (function == CSSValueCubicBezier)
        return consumeCubicBezier(range);
    if (function == CSSValueSteps)
        return consumeSteps(range);
    if (context.springTimingFunctionEnabled && function == CSSValueSpring)
        return consumeSpringFunction(range);
    return nullptr;
}

static RefPtr<CSSValue> consumeAnimationValue(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context)
{
    switch (property) {
    case CSSPropertyAnimationDelay:
    case CSSPropertyTransitionDelay:
        return consumeTime(range, context.mode, ValueRangeAll, UnitlessQuirk::Forbid);
    case CSSPropertyAnimationDirection:
        return consumeIdent<CSSValueNormal, CSSValueAlternate, CSSValueReverse, CSSValueAlternateReverse>(range);
    case CSSPropertyAnimationDuration:
    case CSSPropertyTransitionDuration:
        return consumeTime(range, context.mode, ValueRangeNonNegative, UnitlessQuirk::Forbid);
    case CSSPropertyAnimationFillMode:
        return consumeIdent<CSSValueNone, CSSValueForwards, CSSValueBackwards, CSSValueBoth>(range);
    case CSSPropertyAnimationIterationCount:
        return consumeAnimationIterationCount(range);
    case CSSPropertyAnimationName:
        return consumeAnimationName(range);
    case CSSPropertyAnimationPlayState:
        return consumeIdent<CSSValueRunning, CSSValuePaused>(range);
    case CSSPropertyTransitionProperty:
        return consumeTransitionProperty(range);
    case CSSPropertyAnimationTimingFunction:
    case CSSPropertyTransitionTimingFunction:
        return consumeAnimationTimingFunction(range, context);
    default:
        ASSERT_NOT_REACHED();
        return nullptr;
    }
}

static bool isValidAnimationPropertyList(CSSPropertyID property, const CSSValueList& valueList)
{
    if (property != CSSPropertyTransitionProperty || valueList.length() < 2)
        return true;
    for (auto& value : valueList) {
        if (value->isPrimitiveValue() && downcast<CSSPrimitiveValue>(value.get()).isValueID()
            && downcast<CSSPrimitiveValue>(value.get()).valueID() == CSSValueNone)
            return false;
    }
    return true;
}

static RefPtr<CSSValueList> consumeAnimationPropertyList(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context)
{
    RefPtr<CSSValueList> list = CSSValueList::createCommaSeparated();
    do {
        RefPtr<CSSValue> value = consumeAnimationValue(property, range, context);
        if (!value)
            return nullptr;
        list->append(value.releaseNonNull());
    } while (consumeCommaIncludingWhitespace(range));
    if (!isValidAnimationPropertyList(property, *list))
        return nullptr;
    ASSERT(list->length());
    return list;
}

bool CSSPropertyParser::consumeAnimationShorthand(const StylePropertyShorthand& shorthand, bool important)
{
    const unsigned longhandCount = shorthand.length();
    RefPtr<CSSValueList> longhands[8];
    ASSERT(longhandCount <= 8);
    for (size_t i = 0; i < longhandCount; ++i)
        longhands[i] = CSSValueList::createCommaSeparated();

    do {
        bool parsedLonghand[8] = { false };
        do {
            bool foundProperty = false;
            for (size_t i = 0; i < longhandCount; ++i) {
                if (parsedLonghand[i])
                    continue;

                if (RefPtr<CSSValue> value = consumeAnimationValue(shorthand.properties()[i], m_range, m_context)) {
                    parsedLonghand[i] = true;
                    foundProperty = true;
                    longhands[i]->append(*value);
                    break;
                }
            }
            if (!foundProperty)
                return false;
        } while (!m_range.atEnd() && m_range.peek().type() != CommaToken);

        // FIXME: This will make invalid longhands, see crbug.com/386459
        for (size_t i = 0; i < longhandCount; ++i) {
            if (!parsedLonghand[i])
                longhands[i]->append(CSSValuePool::singleton().createImplicitInitialValue());
            parsedLonghand[i] = false;
        }
    } while (consumeCommaIncludingWhitespace(m_range));

    for (size_t i = 0; i < longhandCount; ++i) {
        if (!isValidAnimationPropertyList(shorthand.properties()[i], *longhands[i]))
            return false;
    }

    for (size_t i = 0; i < longhandCount; ++i)
        addProperty(shorthand.properties()[i], shorthand.id(), *longhands[i], important);

    return m_range.atEnd();
}

static RefPtr<CSSValue> consumeZIndex(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeInteger(range);
}

static RefPtr<CSSValue> consumeShadow(CSSParserTokenRange& range, CSSParserMode cssParserMode, bool isBoxShadowProperty)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    RefPtr<CSSValueList> shadowValueList = CSSValueList::createCommaSeparated();
    do {
        if (RefPtr<CSSShadowValue> shadowValue = consumeSingleShadow(range, cssParserMode, isBoxShadowProperty, isBoxShadowProperty))
            shadowValueList->append(*shadowValue);
        else
            return nullptr;
    } while (consumeCommaIncludingWhitespace(range));
    return shadowValueList;
}

static RefPtr<CSSValue> consumeTextDecorationLine(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);

    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    while (true) {
        RefPtr<CSSPrimitiveValue> ident = consumeIdent<CSSValueBlink, CSSValueUnderline, CSSValueOverline, CSSValueLineThrough>(range);
        if (!ident)
            break;
        if (list->hasValue(ident.get()))
            return nullptr;
        list->append(ident.releaseNonNull());
    }

    if (!list->length())
        return nullptr;
    return list;
}

static RefPtr<CSSValue> consumeTextDecorationSkip(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);

    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    while (true) {
        auto ident = consumeIdent<CSSValueAuto, CSSValueInk, CSSValueObjects>(range);
        if (!ident)
            break;
        if (list->hasValue(ident.get()))
            return nullptr;
        list->append(ident.releaseNonNull());
    }

    if (!list->length())
        return nullptr;
    return list;
}

static RefPtr<CSSValue> consumeTextEmphasisStyle(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);

    if (RefPtr<CSSValue> textEmphasisStyle = consumeString(range))
        return textEmphasisStyle;

    RefPtr<CSSPrimitiveValue> fill = consumeIdent<CSSValueFilled, CSSValueOpen>(range);
    RefPtr<CSSPrimitiveValue> shape = consumeIdent<CSSValueDot, CSSValueCircle, CSSValueDoubleCircle, CSSValueTriangle, CSSValueSesame>(range);
    if (!fill)
        fill = consumeIdent<CSSValueFilled, CSSValueOpen>(range);
    if (fill && shape) {
        RefPtr<CSSValueList> parsedValues = CSSValueList::createSpaceSeparated();
        parsedValues->append(fill.releaseNonNull());
        parsedValues->append(shape.releaseNonNull());
        return parsedValues;
    }
    if (fill)
        return fill;
    if (shape)
        return shape;
    return nullptr;
}

static RefPtr<CSSValue> consumeOutlineColor(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    // Allow the special focus color even in HTML Standard parsing mode.
    if (range.peek().id() == CSSValueWebkitFocusRingColor)
        return consumeIdent(range);
    return consumeColor(range, cssParserMode);
}

static RefPtr<CSSPrimitiveValue> consumeLineWidth(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueThin || id == CSSValueMedium || id == CSSValueThick)
        return consumeIdent(range);
    return consumeLength(range, cssParserMode, ValueRangeNonNegative, unitless);
}

static RefPtr<CSSPrimitiveValue> consumeBorderWidth(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    return consumeLineWidth(range, cssParserMode, unitless);
}

static RefPtr<CSSPrimitiveValue> consumeTextStrokeWidth(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    return consumeLineWidth(range, cssParserMode, UnitlessQuirk::Forbid);
}

static RefPtr<CSSPrimitiveValue> consumeColumnRuleWidth(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    return consumeLineWidth(range, cssParserMode, UnitlessQuirk::Forbid);
}

static bool consumeTranslate3d(CSSParserTokenRange& args, CSSParserMode cssParserMode, RefPtr<WebKitCSSTransformValue>& transformValue)
{
    unsigned numberOfArguments = 2;
    RefPtr<CSSValue> parsedValue;
    do {
        parsedValue = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
        if (!parsedValue)
            return false;
        transformValue->append(*parsedValue);
        if (!consumeCommaIncludingWhitespace(args))
            return false;
    } while (--numberOfArguments);
    parsedValue = consumeLength(args, cssParserMode, ValueRangeAll);
    if (!parsedValue)
        return false;
    transformValue->append(*parsedValue);
    return true;
}

static bool consumeNumbers(CSSParserTokenRange& args, RefPtr<WebKitCSSTransformValue>& transformValue, unsigned numberOfArguments)
{
    do {
        RefPtr<CSSPrimitiveValue> parsedValue = consumeNumber(args, ValueRangeAll);
        if (!parsedValue)
            return false;
        transformValue->append(parsedValue.releaseNonNull());
        if (--numberOfArguments && !consumeCommaIncludingWhitespace(args))
            return false;
    } while (numberOfArguments);
    return true;
}

static bool consumePerspective(CSSParserTokenRange& args, CSSParserMode cssParserMode, RefPtr<WebKitCSSTransformValue>& transformValue)
{
    RefPtr<CSSPrimitiveValue> parsedValue = consumeLength(args, cssParserMode, ValueRangeNonNegative);
    if (!parsedValue) {
        double perspective;
        if (!consumeNumberRaw(args, perspective) || perspective < 0)
            return false;
        parsedValue = CSSPrimitiveValue::create(perspective, CSSPrimitiveValue::UnitTypes::CSS_PX);
    }
    if (!parsedValue)
        return false;
    transformValue->append(parsedValue.releaseNonNull());
    return true;
}

// FIXME-NEWPARSER: This has no reason to exist once we eliminate WebkitCSSTransformValue in favor
// of CSSFunctionValue.
static WebKitCSSTransformValue::TransformOperationType transformOperationForCSSValueID(CSSValueID functionId)
{
    switch (functionId) {
    case CSSValueRotate:
        return WebKitCSSTransformValue::RotateTransformOperation;
    case CSSValueRotatex:
        return WebKitCSSTransformValue::RotateXTransformOperation;
    case CSSValueRotatey:
        return WebKitCSSTransformValue::RotateYTransformOperation;
    case CSSValueRotatez:
        return WebKitCSSTransformValue::RotateZTransformOperation;
    case CSSValueSkewx:
        return WebKitCSSTransformValue::SkewXTransformOperation;
    case CSSValueSkewy:
        return WebKitCSSTransformValue::SkewYTransformOperation;
    case CSSValueSkew:
        return WebKitCSSTransformValue::SkewTransformOperation;
    case CSSValueScalex:
        return WebKitCSSTransformValue::ScaleXTransformOperation;
    case CSSValueScaley:
        return WebKitCSSTransformValue::ScaleYTransformOperation;
    case CSSValueScalez:
        return WebKitCSSTransformValue::ScaleZTransformOperation;
    case CSSValueScale:
        return WebKitCSSTransformValue::ScaleTransformOperation;
    case CSSValuePerspective:
        return WebKitCSSTransformValue::PerspectiveTransformOperation;
    case CSSValueTranslatex:
        return WebKitCSSTransformValue::TranslateXTransformOperation;
    case CSSValueTranslatey:
        return WebKitCSSTransformValue::TranslateYTransformOperation;
    case CSSValueTranslate:
        return WebKitCSSTransformValue::TranslateTransformOperation;
    case CSSValueTranslatez:
        return WebKitCSSTransformValue::TranslateZTransformOperation;
    case CSSValueMatrix:
        return WebKitCSSTransformValue::MatrixTransformOperation;
    case CSSValueMatrix3d:
        return WebKitCSSTransformValue::Matrix3DTransformOperation;
    case CSSValueScale3d:
        return WebKitCSSTransformValue::Scale3DTransformOperation;
    case CSSValueRotate3d:
        return WebKitCSSTransformValue::Rotate3DTransformOperation;
    case CSSValueTranslate3d:
        return WebKitCSSTransformValue::Translate3DTransformOperation;
    default:
        return WebKitCSSTransformValue::UnknownTransformOperation;
    }
}

static RefPtr<CSSValue> consumeTransformValue(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSValueID functionId = range.peek().functionId();
    if (functionId == CSSValueInvalid)
        return nullptr;
    CSSParserTokenRange args = consumeFunction(range);
    if (args.atEnd())
        return nullptr;
    
    // FIXME-NEWPARSER: Do we really need WebkitCSSTransformValue? A CSSFunctionValue is good
    // enough and has the CSSValueID as the operation type. Blink has eliminated it.
    RefPtr<WebKitCSSTransformValue> transformValue = WebKitCSSTransformValue::create(transformOperationForCSSValueID(functionId));
    RefPtr<CSSValue> parsedValue;
    switch (functionId) {
    case CSSValueRotate:
    case CSSValueRotatex:
    case CSSValueRotatey:
    case CSSValueRotatez:
    case CSSValueSkewx:
    case CSSValueSkewy:
    case CSSValueSkew:
        parsedValue = consumeAngle(args, cssParserMode, UnitlessQuirk::Forbid);
        if (!parsedValue)
            return nullptr;
        if (functionId == CSSValueSkew && consumeCommaIncludingWhitespace(args)) {
            transformValue->append(*parsedValue);
            parsedValue = consumeAngle(args, cssParserMode, UnitlessQuirk::Forbid);
            if (!parsedValue)
                return nullptr;
        }
        break;
    case CSSValueScalex:
    case CSSValueScaley:
    case CSSValueScalez:
    case CSSValueScale:
        parsedValue = consumeNumber(args, ValueRangeAll);
        if (!parsedValue)
            return nullptr;
        if (functionId == CSSValueScale && consumeCommaIncludingWhitespace(args)) {
            transformValue->append(*parsedValue);
            parsedValue = consumeNumber(args, ValueRangeAll);
            if (!parsedValue)
                return nullptr;
        }
        break;
    case CSSValuePerspective:
        if (!consumePerspective(args, cssParserMode, transformValue))
            return nullptr;
        break;
    case CSSValueTranslatex:
    case CSSValueTranslatey:
    case CSSValueTranslate:
        parsedValue = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
        if (!parsedValue)
            return nullptr;
        if (functionId == CSSValueTranslate && consumeCommaIncludingWhitespace(args)) {
            transformValue->append(*parsedValue);
            parsedValue = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
            if (!parsedValue)
                return nullptr;
        }
        break;
    case CSSValueTranslatez:
        parsedValue = consumeLength(args, cssParserMode, ValueRangeAll);
        break;
    case CSSValueMatrix:
    case CSSValueMatrix3d:
        if (!consumeNumbers(args, transformValue, (functionId == CSSValueMatrix3d) ? 16 : 6))
            return nullptr;
        break;
    case CSSValueScale3d:
        if (!consumeNumbers(args, transformValue, 3))
            return nullptr;
        break;
    case CSSValueRotate3d:
        if (!consumeNumbers(args, transformValue, 3) || !consumeCommaIncludingWhitespace(args))
            return nullptr;
        parsedValue = consumeAngle(args, cssParserMode, UnitlessQuirk::Forbid);
        if (!parsedValue)
            return nullptr;
        break;
    case CSSValueTranslate3d:
        if (!consumeTranslate3d(args, cssParserMode, transformValue))
            return nullptr;
        break;
    default:
        return nullptr;
    }
    if (parsedValue)
        transformValue->append(*parsedValue);
    if (!args.atEnd())
        return nullptr;
    return transformValue;
}

static RefPtr<CSSValue> consumeTransform(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    do {
        RefPtr<CSSValue> parsedTransformValue = consumeTransformValue(range, cssParserMode);
        if (!parsedTransformValue)
            return nullptr;
        list->append(parsedTransformValue.releaseNonNull());
    } while (!range.atEnd());

    return list;
}

template <CSSValueID start, CSSValueID end>
static RefPtr<CSSPrimitiveValue> consumePositionLonghand(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().type() == IdentToken) {
        CSSValueID id = range.peek().id();
        int percent;
        if (id == start)
            percent = 0;
        else if (id == CSSValueCenter)
            percent = 50;
        else if (id == end)
            percent = 100;
        else
            return nullptr;
        range.consumeIncludingWhitespace();
        return CSSPrimitiveValue::create(percent, CSSPrimitiveValue::UnitTypes::CSS_PERCENTAGE);
    }
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeAll);
}

static RefPtr<CSSPrimitiveValue> consumePositionX(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    return consumePositionLonghand<CSSValueLeft, CSSValueRight>(range, cssParserMode);
}

static RefPtr<CSSPrimitiveValue> consumePositionY(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    return consumePositionLonghand<CSSValueTop, CSSValueBottom>(range, cssParserMode);
}

static RefPtr<CSSValue> consumePaintStroke(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    RefPtr<CSSPrimitiveValue> url = consumeUrl(range);
    if (url) {
        RefPtr<CSSValue> parsedValue;
        if (range.peek().id() == CSSValueNone)
            parsedValue = consumeIdent(range);
        else
            parsedValue = consumeColor(range, cssParserMode);
        if (parsedValue) {
            RefPtr<CSSValueList> values = CSSValueList::createSpaceSeparated();
            values->append(url.releaseNonNull());
            values->append(parsedValue.releaseNonNull());
            return values;
        }
        return url;
    }
    return consumeColor(range, cssParserMode);
}

static RefPtr<CSSValue> consumePaintOrder(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNormal)
        return consumeIdent(range);

    Vector<CSSValueID, 3> paintTypeList;
    RefPtr<CSSPrimitiveValue> fill;
    RefPtr<CSSPrimitiveValue> stroke;
    RefPtr<CSSPrimitiveValue> markers;
    do {
        CSSValueID id = range.peek().id();
        if (id == CSSValueFill && !fill)
            fill = consumeIdent(range);
        else if (id == CSSValueStroke && !stroke)
            stroke = consumeIdent(range);
        else if (id == CSSValueMarkers && !markers)
            markers = consumeIdent(range);
        else
            return nullptr;
        paintTypeList.append(id);
    } while (!range.atEnd());

    // After parsing we serialize the paint-order list. Since it is not possible to
    // pop a last list items from CSSValueList without bigger cost, we create the
    // list after parsing.
    CSSValueID firstPaintOrderType = paintTypeList.at(0);
    RefPtr<CSSValueList> paintOrderList = CSSValueList::createSpaceSeparated();
    switch (firstPaintOrderType) {
    case CSSValueFill:
    case CSSValueStroke:
        paintOrderList->append(firstPaintOrderType == CSSValueFill ? fill.releaseNonNull() : stroke.releaseNonNull());
        if (paintTypeList.size() > 1) {
            if (paintTypeList.at(1) == CSSValueMarkers)
                paintOrderList->append(markers.releaseNonNull());
        }
        break;
    case CSSValueMarkers:
        paintOrderList->append(markers.releaseNonNull());
        if (paintTypeList.size() > 1) {
            if (paintTypeList.at(1) == CSSValueStroke)
                paintOrderList->append(stroke.releaseNonNull());
        }
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    return paintOrderList;
}

static RefPtr<CSSValue> consumeNoneOrURI(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumeUrl(range);
}

static RefPtr<CSSValue> consumeFlexBasis(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    // FIXME: Support intrinsic dimensions too.
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
}

static RefPtr<CSSValue> consumeStrokeDasharray(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNone)
        return consumeIdent(range);

    RefPtr<CSSValueList> dashes = CSSValueList::createCommaSeparated();
    do {
        RefPtr<CSSPrimitiveValue> dash = consumeLengthOrPercent(range, SVGAttributeMode, ValueRangeNonNegative);
        if (!dash || (consumeCommaIncludingWhitespace(range) && range.atEnd()))
            return nullptr;
        dashes->append(dash.releaseNonNull());
    } while (!range.atEnd());
    return dashes;
}

static RefPtr<CSSPrimitiveValue> consumeBaselineShift(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueBaseline || id == CSSValueSub || id == CSSValueSuper)
        return consumeIdent(range);
    return consumeLengthOrPercent(range, SVGAttributeMode, ValueRangeAll);
}

static RefPtr<CSSPrimitiveValue> consumeRxOrRy(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);
    return consumeLengthOrPercent(range, SVGAttributeMode, ValueRangeAll, UnitlessQuirk::Forbid);
}

static RefPtr<CSSValue> consumeCursor(CSSParserTokenRange& range, const CSSParserContext& context, bool inQuirksMode)
{
    RefPtr<CSSValueList> list;
    while (RefPtr<CSSValue> image = consumeImage(range, context, ConsumeGeneratedImage::Forbid)) {
        double num;
        IntPoint hotSpot(-1, -1);
        bool hotSpotSpecified = false;
        if (consumeNumberRaw(range, num)) {
            hotSpot.setX(int(num));
            if (!consumeNumberRaw(range, num))
                return nullptr;
            hotSpot.setY(int(num));
            hotSpotSpecified = true;
        }

        if (!list)
            list = CSSValueList::createCommaSeparated();

        list->append(CSSCursorImageValue::create(image.releaseNonNull(), hotSpotSpecified, hotSpot));
        if (!consumeCommaIncludingWhitespace(range))
            return nullptr;
    }

    CSSValueID id = range.peek().id();
    RefPtr<CSSValue> cursorType;
    if (id == CSSValueHand) {
        if (!inQuirksMode) // Non-standard behavior
            return nullptr;
        cursorType = CSSValuePool::singleton().createIdentifierValue(CSSValuePointer);
        range.consumeIncludingWhitespace();
    } else if ((id >= CSSValueAuto && id <= CSSValueWebkitZoomOut) || id == CSSValueCopy || id == CSSValueNone) {
        cursorType = consumeIdent(range);
    } else {
        return nullptr;
    }

    if (!list)
        return cursorType;
    list->append(cursorType.releaseNonNull());
    return list;
}

static RefPtr<CSSValue> consumeAttr(CSSParserTokenRange args, CSSParserContext context)
{
    if (args.peek().type() != IdentToken)
        return nullptr;
    
    CSSParserToken token = args.consumeIncludingWhitespace();
    if (context.isHTMLDocument)
        token.convertToASCIILowercaseInPlace();

    String attrName = token.value().toString();
    if (!args.atEnd())
        return nullptr;

    // FIXME-NEWPARSER: We want to use a CSSCustomIdentValue here eventually for the attrName.
    // FIXME-NEWPARSER: We want to use a CSSFunctionValue rather than relying on a custom
    // attr() primitive value.
    return CSSValuePool::singleton().createValue(attrName, CSSPrimitiveValue::CSS_ATTR);
}

static RefPtr<CSSValue> consumeCounterContent(CSSParserTokenRange args, bool counters)
{
    RefPtr<CSSPrimitiveValue> identifier = consumeCustomIdent(args);
    if (!identifier)
        return nullptr;

    RefPtr<CSSPrimitiveValue> separator;
    if (!counters)
        separator = CSSPrimitiveValue::create(String(), CSSPrimitiveValue::UnitTypes::CSS_STRING);
    else {
        if (!consumeCommaIncludingWhitespace(args) || args.peek().type() != StringToken)
            return nullptr;
        separator = CSSPrimitiveValue::create(args.consumeIncludingWhitespace().value().toString(), CSSPrimitiveValue::UnitTypes::CSS_STRING);
    }

    RefPtr<CSSPrimitiveValue> listStyle;
    if (consumeCommaIncludingWhitespace(args)) {
        CSSValueID id = args.peek().id();
        if ((id != CSSValueNone && (id < CSSValueDisc || id > CSSValueKatakanaIroha)))
            return nullptr;
        listStyle = consumeIdent(args);
    } else
        listStyle = CSSValuePool::singleton().createIdentifierValue(CSSValueDecimal);

    if (!args.atEnd())
        return nullptr;
    
    // FIXME-NEWPARSER: Should just have a CSSCounterValue.
    return CSSValuePool::singleton().createValue(Counter::create(identifier.releaseNonNull(), listStyle.releaseNonNull(), separator.releaseNonNull()));
}

static RefPtr<CSSValue> consumeContent(CSSParserTokenRange& range, CSSParserContext context)
{
    if (identMatches<CSSValueNone, CSSValueNormal>(range.peek().id()))
        return consumeIdent(range);

    RefPtr<CSSValueList> values = CSSValueList::createSpaceSeparated();

    do {
        RefPtr<CSSValue> parsedValue = consumeImage(range, context);
        if (!parsedValue)
            parsedValue = consumeIdent<CSSValueOpenQuote, CSSValueCloseQuote, CSSValueNoOpenQuote, CSSValueNoCloseQuote>(range);
        if (!parsedValue)
            parsedValue = consumeString(range);
        if (!parsedValue) {
            if (range.peek().functionId() == CSSValueAttr)
                parsedValue = consumeAttr(consumeFunction(range), context);
            else if (range.peek().functionId() == CSSValueCounter)
                parsedValue = consumeCounterContent(consumeFunction(range), false);
            else if (range.peek().functionId() == CSSValueCounters)
                parsedValue = consumeCounterContent(consumeFunction(range), true);
            if (!parsedValue)
                return nullptr;
        }
        values->append(parsedValue.releaseNonNull());
    } while (!range.atEnd());

    return values;
}

static RefPtr<CSSPrimitiveValue> consumePerspective(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    RefPtr<CSSPrimitiveValue> parsedValue = consumeLength(range, cssParserMode, ValueRangeAll);
    if (!parsedValue) {
        // FIXME: Make this quirk only apply to the webkit prefixed version of the property.
        double perspective;
        if (!consumeNumberRaw(range, perspective))
            return nullptr;
        parsedValue = CSSPrimitiveValue::create(perspective, CSSPrimitiveValue::UnitTypes::CSS_PX);
    }
    if (parsedValue && (parsedValue->isCalculated() || parsedValue->doubleValue() > 0))
        return parsedValue;
    return nullptr;
}

#if ENABLE(CSS_SCROLL_SNAP)

static RefPtr<CSSValueList> consumeSnapPointCoordinateList(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    RefPtr<CSSValueList> positions = CSSValueList::createSpaceSeparated();
    do {
        
        RefPtr<CSSPrimitiveValue> first = consumePositionX(range, cssParserMode);
        if (!first || range.atEnd())
            return nullptr;
        RefPtr<CSSPrimitiveValue> second = consumePositionY(range, cssParserMode);
        if (!second)
            return nullptr;
        RefPtr<CSSValue> position = createPrimitiveValuePair(first.releaseNonNull(), second.releaseNonNull(), Pair::IdenticalValueEncoding::DoNotCoalesce);
        positions->append(position.releaseNonNull());
    } while (!range.atEnd());
    return positions;
}

static RefPtr<CSSValue> consumeScrollSnapCoordinate(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumeSnapPointCoordinateList(range, cssParserMode);
}

static RefPtr<CSSValue> consumeScrollSnapDestination(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    RefPtr<CSSPrimitiveValue> first = consumePositionX(range, cssParserMode);
    if (!first || range.atEnd())
        return nullptr;
    RefPtr<CSSPrimitiveValue> second = consumePositionY(range, cssParserMode);
    if (!second)
        return nullptr;
    return createPrimitiveValuePair(first.releaseNonNull(), second.releaseNonNull(), Pair::IdenticalValueEncoding::DoNotCoalesce);
}

static RefPtr<CSSValue> consumeScrollSnapPoints(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone || range.peek().id() == CSSValueElements)
        return consumeIdent(range);
    
    RefPtr<CSSValueList> points = CSSValueList::createSpaceSeparated();
    do {
        if (range.peek().functionId() == CSSValueRepeat) {
            CSSParserTokenRange args = consumeFunction(range);
            RefPtr<CSSPrimitiveValue> parsedValue = consumeLengthOrPercent(args, cssParserMode, ValueRangeNonNegative);
            if (args.atEnd() && parsedValue && (parsedValue->isCalculated() || parsedValue->doubleValue() > 0)) {
                Ref<CSSFunctionValue> result = CSSFunctionValue::create(CSSValueRepeat);
                result->append(parsedValue.releaseNonNull());
                points->append(WTFMove(result));
            }
        } else {
            RefPtr<CSSValue> length = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Forbid);
            if (!length)
                return nullptr;
            points->append(length.releaseNonNull());
        }
    } while (!range.atEnd());
    
    
    return points;
}

#endif

static RefPtr<CSSValue> consumeBorderRadiusCorner(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    RefPtr<CSSPrimitiveValue> parsedValue1 = consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
    if (!parsedValue1)
        return nullptr;
    RefPtr<CSSPrimitiveValue> parsedValue2 = consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
    if (!parsedValue2)
        parsedValue2 = parsedValue1;
    return createPrimitiveValuePair(parsedValue1.releaseNonNull(), parsedValue2.releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce);
}

static RefPtr<CSSPrimitiveValue> consumeVerticalAlign(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    RefPtr<CSSPrimitiveValue> parsedValue = consumeIdentRange(range, CSSValueBaseline, CSSValueWebkitBaselineMiddle);
    if (!parsedValue)
        parsedValue = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow);
    return parsedValue;
}

static RefPtr<CSSPrimitiveValue> consumeShapeRadius(CSSParserTokenRange& args, CSSParserMode cssParserMode)
{
    if (identMatches<CSSValueClosestSide, CSSValueFarthestSide>(args.peek().id()))
        return consumeIdent(args);
    return consumeLengthOrPercent(args, cssParserMode, ValueRangeNonNegative);
}

static RefPtr<CSSBasicShapeCircle> consumeBasicShapeCircle(CSSParserTokenRange& args, const CSSParserContext& context)
{
    // spec: https://drafts.csswg.org/css-shapes/#supported-basic-shapes
    // circle( [<shape-radius>]? [at <position>]? )
    RefPtr<CSSBasicShapeCircle> shape = CSSBasicShapeCircle::create();
    if (RefPtr<CSSPrimitiveValue> radius = consumeShapeRadius(args, context.mode))
        shape->setRadius(radius.releaseNonNull());
    if (consumeIdent<CSSValueAt>(args)) {
        RefPtr<CSSPrimitiveValue> centerX;
        RefPtr<CSSPrimitiveValue> centerY;
        if (!consumePosition(args, context.mode, UnitlessQuirk::Forbid, centerX, centerY))
            return nullptr;
        shape->setCenterX(centerX.releaseNonNull());
        shape->setCenterY(centerY.releaseNonNull());
    }
    return shape;
}

static RefPtr<CSSBasicShapeEllipse> consumeBasicShapeEllipse(CSSParserTokenRange& args, const CSSParserContext& context)
{
    // spec: https://drafts.csswg.org/css-shapes/#supported-basic-shapes
    // ellipse( [<shape-radius>{2}]? [at <position>]? )
    RefPtr<CSSBasicShapeEllipse> shape = CSSBasicShapeEllipse::create();
    if (RefPtr<CSSPrimitiveValue> radiusX = consumeShapeRadius(args, context.mode)) {
        shape->setRadiusX(radiusX.releaseNonNull());
        if (RefPtr<CSSPrimitiveValue> radiusY = consumeShapeRadius(args, context.mode))
            shape->setRadiusY(radiusY.releaseNonNull());
    }
    if (consumeIdent<CSSValueAt>(args)) {
        RefPtr<CSSPrimitiveValue> centerX;
        RefPtr<CSSPrimitiveValue> centerY;
        if (!consumePosition(args, context.mode, UnitlessQuirk::Forbid, centerX, centerY))
            return nullptr;
        shape->setCenterX(centerX.releaseNonNull());
        shape->setCenterY(centerY.releaseNonNull());
    }
    return shape;
}

static RefPtr<CSSBasicShapePolygon> consumeBasicShapePolygon(CSSParserTokenRange& args, const CSSParserContext& context)
{
    RefPtr<CSSBasicShapePolygon> shape = CSSBasicShapePolygon::create();
    if (identMatches<CSSValueEvenodd, CSSValueNonzero>(args.peek().id())) {
        shape->setWindRule(args.consumeIncludingWhitespace().id() == CSSValueEvenodd ? RULE_EVENODD : RULE_NONZERO);
        if (!consumeCommaIncludingWhitespace(args))
            return nullptr;
    }

    do {
        RefPtr<CSSPrimitiveValue> xLength = consumeLengthOrPercent(args, context.mode, ValueRangeAll);
        if (!xLength)
            return nullptr;
        RefPtr<CSSPrimitiveValue> yLength = consumeLengthOrPercent(args, context.mode, ValueRangeAll);
        if (!yLength)
            return nullptr;
        shape->appendPoint(xLength.releaseNonNull(), yLength.releaseNonNull());
    } while (consumeCommaIncludingWhitespace(args));
    return shape;
}

static RefPtr<CSSBasicShapePath> consumeBasicShapePath(CSSParserTokenRange& args)
{
    WindRule windRule = RULE_NONZERO;
    if (identMatches<CSSValueEvenodd, CSSValueNonzero>(args.peek().id())) {
        windRule = args.consumeIncludingWhitespace().id() == CSSValueEvenodd ? RULE_EVENODD : RULE_NONZERO;
        if (!consumeCommaIncludingWhitespace(args))
            return nullptr;
    }

    if (args.peek().type() != StringToken)
        return nullptr;
    
    auto byteStream = std::make_unique<SVGPathByteStream>();
    if (!buildSVGPathByteStreamFromString(args.consumeIncludingWhitespace().value().toString(), *byteStream, UnalteredParsing))
        return nullptr;
    
    auto shape = CSSBasicShapePath::create(WTFMove(byteStream));
    shape->setWindRule(windRule);
    
    return WTFMove(shape);
}

static void complete4Sides(RefPtr<CSSPrimitiveValue> side[4])
{
    if (side[3])
        return;
    if (!side[2]) {
        if (!side[1])
            side[1] = side[0];
        side[2] = side[0];
    }
    side[3] = side[1];
}

static bool consumeRadii(RefPtr<CSSPrimitiveValue> horizontalRadii[4], RefPtr<CSSPrimitiveValue> verticalRadii[4], CSSParserTokenRange& range, CSSParserMode cssParserMode, bool useLegacyParsing)
{
    unsigned i = 0;
    for (; i < 4 && !range.atEnd() && range.peek().type() != DelimiterToken; ++i) {
        horizontalRadii[i] = consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
        if (!horizontalRadii[i])
            return false;
    }
    if (!horizontalRadii[0])
        return false;
    if (range.atEnd()) {
        // Legacy syntax: -webkit-border-radius: l1 l2; is equivalent to border-radius: l1 / l2;
        if (useLegacyParsing && i == 2) {
            verticalRadii[0] = horizontalRadii[1];
            horizontalRadii[1] = nullptr;
        } else {
            complete4Sides(horizontalRadii);
            for (unsigned i = 0; i < 4; ++i)
                verticalRadii[i] = horizontalRadii[i];
            return true;
        }
    } else {
        if (!consumeSlashIncludingWhitespace(range))
            return false;
        for (i = 0; i < 4 && !range.atEnd(); ++i) {
            verticalRadii[i] = consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
            if (!verticalRadii[i])
                return false;
        }
        if (!verticalRadii[0] || !range.atEnd())
            return false;
    }
    complete4Sides(horizontalRadii);
    complete4Sides(verticalRadii);
    return true;
}

static RefPtr<CSSBasicShapeInset> consumeBasicShapeInset(CSSParserTokenRange& args, const CSSParserContext& context)
{
    RefPtr<CSSBasicShapeInset> shape = CSSBasicShapeInset::create();
    RefPtr<CSSPrimitiveValue> top = consumeLengthOrPercent(args, context.mode, ValueRangeAll);
    if (!top)
        return nullptr;
    RefPtr<CSSPrimitiveValue> right = consumeLengthOrPercent(args, context.mode, ValueRangeAll);
    RefPtr<CSSPrimitiveValue> bottom;
    RefPtr<CSSPrimitiveValue> left;
    if (right) {
        bottom = consumeLengthOrPercent(args, context.mode, ValueRangeAll);
        if (bottom)
            left = consumeLengthOrPercent(args, context.mode, ValueRangeAll);
    }
    if (left)
        shape->updateShapeSize4Values(top.releaseNonNull(), right.releaseNonNull(), bottom.releaseNonNull(), left.releaseNonNull());
    else if (bottom)
        shape->updateShapeSize3Values(top.releaseNonNull(), right.releaseNonNull(), bottom.releaseNonNull());
    else if (right)
        shape->updateShapeSize2Values(top.releaseNonNull(), right.releaseNonNull());
    else
        shape->updateShapeSize1Value(top.releaseNonNull());

    if (consumeIdent<CSSValueRound>(args)) {
        RefPtr<CSSPrimitiveValue> horizontalRadii[4] = { 0 };
        RefPtr<CSSPrimitiveValue> verticalRadii[4] = { 0 };
        if (!consumeRadii(horizontalRadii, verticalRadii, args, context.mode, false))
            return nullptr;
        shape->setTopLeftRadius(createPrimitiveValuePair(horizontalRadii[0].releaseNonNull(), verticalRadii[0].releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce));
        shape->setTopRightRadius(createPrimitiveValuePair(horizontalRadii[1].releaseNonNull(), verticalRadii[1].releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce));
        shape->setBottomRightRadius(createPrimitiveValuePair(horizontalRadii[2].releaseNonNull(), verticalRadii[2].releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce));
        shape->setBottomLeftRadius(createPrimitiveValuePair(horizontalRadii[3].releaseNonNull(), verticalRadii[3].releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce));
    }
    return shape;
}

static RefPtr<CSSValue> consumeBasicShape(CSSParserTokenRange& range, const CSSParserContext& context)
{
    RefPtr<CSSValue> result;
    if (range.peek().type() != FunctionToken)
        return nullptr;
    CSSValueID id = range.peek().functionId();
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);
    
    // FIXME-NEWPARSER: CSSBasicShape should be a CSSValue, and shapes should not be primitive values.
    RefPtr<CSSBasicShape> shape;
    if (id == CSSValueCircle)
        shape = consumeBasicShapeCircle(args, context);
    else if (id == CSSValueEllipse)
        shape = consumeBasicShapeEllipse(args, context);
    else if (id == CSSValuePolygon)
        shape = consumeBasicShapePolygon(args, context);
    else if (id == CSSValueInset)
        shape = consumeBasicShapeInset(args, context);
    else if (id == CSSValuePath)
        shape = consumeBasicShapePath(args);
    if (!shape)
        return nullptr;
    range = rangeCopy;
    
    if (!args.atEnd())
        return nullptr;

    return CSSValuePool::singleton().createValue(shape.releaseNonNull());
}

static RefPtr<CSSValue> consumeBasicShapeOrBox(CSSParserTokenRange& range, const CSSParserContext& context)
{
    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    bool shapeFound = false;
    bool boxFound = false;
    while (!range.atEnd() && !(shapeFound && boxFound)) {
        RefPtr<CSSValue> componentValue;
        if (range.peek().type() == FunctionToken && !shapeFound) {
            componentValue = consumeBasicShape(range, context);
            shapeFound = true;
        } else if (range.peek().type() == IdentToken && !boxFound) {
            componentValue = consumeIdent<CSSValueContentBox, CSSValuePaddingBox, CSSValueBorderBox, CSSValueMarginBox, CSSValueFill, CSSValueStroke, CSSValueViewBox>(range);
            boxFound = true;
        }
        if (!componentValue)
            return nullptr;
        list->append(componentValue.releaseNonNull());
    }
    
    if (!range.atEnd())
        return nullptr;
    
    return list;
}
    
static RefPtr<CSSValue> consumeWebkitClipPath(CSSParserTokenRange& range, const CSSParserContext& context)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    if (RefPtr<CSSPrimitiveValue> url = consumeUrl(range))
        return url;
    return consumeBasicShapeOrBox(range, context);
}

static RefPtr<CSSValue> consumeShapeOutside(CSSParserTokenRange& range, const CSSParserContext& context)
{
    if (RefPtr<CSSValue> imageValue = consumeImageOrNone(range, context))
        return imageValue;
    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    if (RefPtr<CSSValue> boxValue = consumeIdent<CSSValueContentBox, CSSValuePaddingBox, CSSValueBorderBox, CSSValueMarginBox>(range))
        list->append(boxValue.releaseNonNull());
    if (RefPtr<CSSValue> shapeValue = consumeBasicShape(range, context)) {
        list->append(shapeValue.releaseNonNull());
        if (list->length() < 2) {
            if (RefPtr<CSSValue> boxValue = consumeIdent<CSSValueContentBox, CSSValuePaddingBox, CSSValueBorderBox, CSSValueMarginBox>(range))
                list->append(boxValue.releaseNonNull());
        }
    }
    if (!list->length())
        return nullptr;
    return list;
}

static RefPtr<CSSValue> consumeContentDistributionOverflowPosition(CSSParserTokenRange& range)
{
    if (identMatches<CSSValueNormal, CSSValueBaseline, CSSValueLastBaseline>(range.peek().id()))
        return CSSContentDistributionValue::create(CSSValueInvalid, range.consumeIncludingWhitespace().id(), CSSValueInvalid);

    CSSValueID distribution = CSSValueInvalid;
    CSSValueID position = CSSValueInvalid;
    CSSValueID overflow = CSSValueInvalid;
    do {
        CSSValueID id = range.peek().id();
        if (identMatches<CSSValueSpaceBetween, CSSValueSpaceAround, CSSValueSpaceEvenly, CSSValueStretch>(id)) {
            if (distribution != CSSValueInvalid)
                return nullptr;
            distribution = id;
        } else if (identMatches<CSSValueStart, CSSValueEnd, CSSValueCenter, CSSValueFlexStart, CSSValueFlexEnd, CSSValueLeft, CSSValueRight>(id)) {
            if (position != CSSValueInvalid)
                return nullptr;
            position = id;
        } else if (identMatches<CSSValueUnsafe, CSSValueSafe>(id)) {
            if (overflow != CSSValueInvalid)
                return nullptr;
            overflow = id;
        } else {
            return nullptr;
        }
        range.consumeIncludingWhitespace();
    } while (!range.atEnd());

    // The grammar states that we should have at least <content-distribution> or <content-position>.
    if (position == CSSValueInvalid && distribution == CSSValueInvalid)
        return nullptr;

    // The grammar states that <overflow-position> must be associated to <content-position>.
    if (overflow != CSSValueInvalid && position == CSSValueInvalid)
        return nullptr;

    return CSSContentDistributionValue::create(distribution, position, overflow);
}

static RefPtr<CSSPrimitiveValue> consumeBorderImageRepeatKeyword(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueStretch, CSSValueRepeat, CSSValueSpace, CSSValueRound>(range);
}

static RefPtr<CSSValue> consumeBorderImageRepeat(CSSParserTokenRange& range)
{
    RefPtr<CSSPrimitiveValue> horizontal = consumeBorderImageRepeatKeyword(range);
    if (!horizontal)
        return nullptr;
    RefPtr<CSSPrimitiveValue> vertical = consumeBorderImageRepeatKeyword(range);
    if (!vertical)
        vertical = horizontal;
    return createPrimitiveValuePair(horizontal.releaseNonNull(), vertical.releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce);
}

static RefPtr<CSSValue> consumeBorderImageSlice(CSSPropertyID property, CSSParserTokenRange& range)
{
    bool fill = consumeIdent<CSSValueFill>(range);
    RefPtr<CSSPrimitiveValue> slices[4] = { 0 };

    for (size_t index = 0; index < 4; ++index) {
        RefPtr<CSSPrimitiveValue> value = consumePercent(range, ValueRangeNonNegative);
        if (!value)
            value = consumeNumber(range, ValueRangeNonNegative);
        if (!value)
            break;
        slices[index] = value;
    }
    if (!slices[0])
        return nullptr;
    if (consumeIdent<CSSValueFill>(range)) {
        if (fill)
            return nullptr;
        fill = true;
    }
    complete4Sides(slices);
    // FIXME: For backwards compatibility, -webkit-border-image, -webkit-mask-box-image and -webkit-box-reflect have to do a fill by default.
    // FIXME: What do we do with -webkit-box-reflect and -webkit-mask-box-image? Probably just have to leave them filling...
    if (property == CSSPropertyWebkitBorderImage || property == CSSPropertyWebkitMaskBoxImage || property == CSSPropertyWebkitBoxReflect)
        fill = true;
    
    // Now build a rect value to hold all four of our primitive values.
    // FIXME-NEWPARSER: Should just have a CSSQuadValue.
    auto quad = Quad::create();
    quad->setTop(slices[0].releaseNonNull());
    quad->setRight(slices[1].releaseNonNull());
    quad->setBottom(slices[2].releaseNonNull());
    quad->setLeft(slices[3].releaseNonNull());
    
    // Make our new border image value now.
    return CSSBorderImageSliceValue::create(CSSValuePool::singleton().createValue(WTFMove(quad)), fill);
}

static RefPtr<CSSValue> consumeBorderImageOutset(CSSParserTokenRange& range)
{
    RefPtr<CSSPrimitiveValue> outsets[4] = { 0 };

    RefPtr<CSSPrimitiveValue> value;
    for (size_t index = 0; index < 4; ++index) {
        value = consumeNumber(range, ValueRangeNonNegative);
        if (!value)
            value = consumeLength(range, HTMLStandardMode, ValueRangeNonNegative);
        if (!value)
            break;
        outsets[index] = value;
    }
    if (!outsets[0])
        return nullptr;
    complete4Sides(outsets);
    
    // FIXME-NEWPARSER: Should just have a CSSQuadValue.
    auto quad = Quad::create();
    quad->setTop(outsets[0].releaseNonNull());
    quad->setRight(outsets[1].releaseNonNull());
    quad->setBottom(outsets[2].releaseNonNull());
    quad->setLeft(outsets[3].releaseNonNull());
    
    return CSSValuePool::singleton().createValue(WTFMove(quad));
}

static RefPtr<CSSValue> consumeBorderImageWidth(CSSParserTokenRange& range)
{
    RefPtr<CSSPrimitiveValue> widths[4];

    RefPtr<CSSPrimitiveValue> value;
    for (size_t index = 0; index < 4; ++index) {
        value = consumeNumber(range, ValueRangeNonNegative);
        if (!value)
            value = consumeLengthOrPercent(range, HTMLStandardMode, ValueRangeNonNegative, UnitlessQuirk::Forbid);
        if (!value)
            value = consumeIdent<CSSValueAuto>(range);
        if (!value)
            break;
        widths[index] = value;
    }
    if (!widths[0])
        return nullptr;
    complete4Sides(widths);
    
    // FIXME-NEWPARSER: Should just have a CSSQuadValue.
    auto quad = Quad::create();
    quad->setTop(widths[0].releaseNonNull());
    quad->setRight(widths[1].releaseNonNull());
    quad->setBottom(widths[2].releaseNonNull());
    quad->setLeft(widths[3].releaseNonNull());
    
    return CSSValuePool::singleton().createValue(WTFMove(quad));
}

static bool consumeBorderImageComponents(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context, RefPtr<CSSValue>& source,
    RefPtr<CSSValue>& slice, RefPtr<CSSValue>& width, RefPtr<CSSValue>& outset, RefPtr<CSSValue>& repeat)
{
    do {
        if (!source) {
            source = consumeImageOrNone(range, context);
            if (source)
                continue;
        }
        if (!repeat) {
            repeat = consumeBorderImageRepeat(range);
            if (repeat)
                continue;
        }
        if (!slice) {
            slice = consumeBorderImageSlice(property, range);
            if (slice) {
                ASSERT(!width && !outset);
                if (consumeSlashIncludingWhitespace(range)) {
                    width = consumeBorderImageWidth(range);
                    if (consumeSlashIncludingWhitespace(range)) {
                        outset = consumeBorderImageOutset(range);
                        if (!outset)
                            return false;
                    } else if (!width) {
                        return false;
                    }
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    } while (!range.atEnd());
    return true;
}

static RefPtr<CSSValue> consumeWebkitBorderImage(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context)
{
    RefPtr<CSSValue> source;
    RefPtr<CSSValue> slice;
    RefPtr<CSSValue> width;
    RefPtr<CSSValue> outset;
    RefPtr<CSSValue> repeat;
    if (consumeBorderImageComponents(property, range, context, source, slice, width, outset, repeat))
        return createBorderImageValue(WTFMove(source), WTFMove(slice), WTFMove(width), WTFMove(outset), WTFMove(repeat));
    return nullptr;
}

static RefPtr<CSSValue> consumeReflect(CSSParserTokenRange& range, const CSSParserContext& context)
{
    RefPtr<CSSPrimitiveValue> direction = consumeIdent<CSSValueAbove, CSSValueBelow, CSSValueLeft, CSSValueRight>(range);
    if (!direction)
        return nullptr;

    RefPtr<CSSPrimitiveValue> offset;
    if (range.atEnd())
        offset = CSSValuePool::singleton().createValue(0, CSSPrimitiveValue::UnitTypes::CSS_PX);
    else {
        offset = consumeLengthOrPercent(range, context.mode, ValueRangeAll, UnitlessQuirk::Forbid);
        if (!offset)
            return nullptr;
    }

    RefPtr<CSSValue> mask;
    if (!range.atEnd()) {
        mask = consumeWebkitBorderImage(CSSPropertyWebkitBoxReflect, range, context);
        if (!mask)
            return nullptr;
    }
    return CSSReflectValue::create(direction.releaseNonNull(), offset.releaseNonNull(), WTFMove(mask));
}

#if ENABLE(CSS_IMAGE_ORIENTATION)
static RefPtr<CSSValue> consumeImageOrientation(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless = UnitlessQuirk::Forbid)
{
    if (range.peek().type() != NumberToken) {
        RefPtr<CSSPrimitiveValue> angle = consumeAngle(range, cssParserMode, unitless);
        if (angle && angle->doubleValue() == 0)
            return angle;
    }
    return nullptr;
}
#endif

static RefPtr<CSSPrimitiveValue> consumeBackgroundBlendMode(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueNormal || id == CSSValueOverlay || (id >= CSSValueMultiply && id <= CSSValueLuminosity))
        return consumeIdent(range);
    return nullptr;
}

static RefPtr<CSSPrimitiveValue> consumeBackgroundAttachment(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueScroll, CSSValueFixed, CSSValueLocal>(range);
}

static RefPtr<CSSPrimitiveValue> consumeBackgroundBox(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueBorderBox, CSSValuePaddingBox, CSSValueContentBox, CSSValueWebkitText>(range);
}

static RefPtr<CSSPrimitiveValue> consumeBackgroundComposite(CSSParserTokenRange& range)
{
    return consumeIdentRange(range, CSSValueClear, CSSValuePlusLighter);
}

static RefPtr<CSSPrimitiveValue> consumeWebkitMaskSourceType(CSSParserTokenRange& range)
{
    return consumeIdent<CSSValueAuto, CSSValueAlpha, CSSValueLuminance>(range);
}

static RefPtr<CSSPrimitiveValue> consumePrefixedBackgroundBox(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& /*context*/)
{
    // The values 'border', 'padding' and 'content' are deprecated and do not apply to the version of the property that has the -webkit- prefix removed.
    if (RefPtr<CSSPrimitiveValue> value = consumeIdentRange(range, CSSValueBorder, CSSValuePaddingBox))
        return value;
    if (range.peek().id() == CSSValueWebkitText || ((property == CSSPropertyWebkitBackgroundClip || property == CSSPropertyWebkitMaskClip) && range.peek().id() == CSSValueText))
        return consumeIdent(range);
    return nullptr;
}

static RefPtr<CSSPrimitiveValue> consumeBackgroundSize(CSSPropertyID property, CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (identMatches<CSSValueContain, CSSValueCover>(range.peek().id()))
        return consumeIdent(range);

    // FIXME: We're allowing the unitless quirk on this property because our
    // tests assume that. Other browser engines don't allow it though.
    RefPtr<CSSPrimitiveValue> horizontal = consumeIdent<CSSValueAuto>(range);
    if (!horizontal)
        horizontal = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow);

    RefPtr<CSSPrimitiveValue> vertical;
    if (!range.atEnd()) {
        if (range.peek().id() == CSSValueAuto) // `auto' is the default
            range.consumeIncludingWhitespace();
        else
            vertical = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, UnitlessQuirk::Allow);
    } else if (!vertical && property == CSSPropertyWebkitBackgroundSize) {
        // Legacy syntax: "-webkit-background-size: 10px" is equivalent to "background-size: 10px 10px".
        vertical = horizontal;
    }
    if (!vertical)
        return horizontal;
    return createPrimitiveValuePair(horizontal.releaseNonNull(), vertical.releaseNonNull(), Pair::IdenticalValueEncoding::DoNotCoalesce);
}

#if ENABLE(CSS_GRID_LAYOUT)
static RefPtr<CSSValueList> consumeGridAutoFlow(CSSParserTokenRange& range)
{
    RefPtr<CSSPrimitiveValue> rowOrColumnValue = consumeIdent<CSSValueRow, CSSValueColumn>(range);
    RefPtr<CSSPrimitiveValue> denseAlgorithm = consumeIdent<CSSValueDense>(range);
    if (!rowOrColumnValue) {
        rowOrColumnValue = consumeIdent<CSSValueRow, CSSValueColumn>(range);
        if (!rowOrColumnValue && !denseAlgorithm)
            return nullptr;
    }
    RefPtr<CSSValueList> parsedValues = CSSValueList::createSpaceSeparated();
    if (rowOrColumnValue)
        parsedValues->append(rowOrColumnValue.releaseNonNull());
    if (denseAlgorithm)
        parsedValues->append(denseAlgorithm.releaseNonNull());
    return parsedValues;
}
#endif

static RefPtr<CSSValue> consumeBackgroundComponent(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context)
{
    switch (property) {
    case CSSPropertyBackgroundClip:
        return consumeBackgroundBox(range);
    case CSSPropertyBackgroundBlendMode:
        return consumeBackgroundBlendMode(range);
    case CSSPropertyBackgroundAttachment:
        return consumeBackgroundAttachment(range);
    case CSSPropertyBackgroundOrigin:
        return consumeBackgroundBox(range);
    case CSSPropertyWebkitMaskComposite:
    case CSSPropertyWebkitBackgroundComposite:
        return consumeBackgroundComposite(range);
    case CSSPropertyWebkitBackgroundClip:
    case CSSPropertyWebkitBackgroundOrigin:
    case CSSPropertyWebkitMaskClip:
    case CSSPropertyWebkitMaskOrigin:
        return consumePrefixedBackgroundBox(property, range, context);
    case CSSPropertyBackgroundImage:
    case CSSPropertyWebkitMaskImage:
        return consumeImageOrNone(range, context);
    case CSSPropertyWebkitMaskSourceType:
        return consumeWebkitMaskSourceType(range);
    case CSSPropertyBackgroundPositionX:
    case CSSPropertyWebkitMaskPositionX:
        return consumePositionX(range, context.mode);
    case CSSPropertyBackgroundPositionY:
    case CSSPropertyWebkitMaskPositionY:
        return consumePositionY(range, context.mode);
    case CSSPropertyBackgroundSize:
    case CSSPropertyWebkitBackgroundSize:
    case CSSPropertyWebkitMaskSize:
        return consumeBackgroundSize(property, range, context.mode);
    case CSSPropertyBackgroundColor:
        return consumeColor(range, context.mode);
    default:
        break;
    };
    return nullptr;
}

static void addBackgroundValue(RefPtr<CSSValue>& list, Ref<CSSValue>&& value)
{
    if (list) {
        if (!list->isBaseValueList()) {
            RefPtr<CSSValue> firstValue = list;
            list = CSSValueList::createCommaSeparated();
            downcast<CSSValueList>(*list).append(firstValue.releaseNonNull());
        }
        downcast<CSSValueList>(*list).append(WTFMove(value));
    } else {
        // To conserve memory we don't actually wrap a single value in a list.
        list = WTFMove(value);
    }
}

static RefPtr<CSSValue> consumeCommaSeparatedBackgroundComponent(CSSPropertyID property, CSSParserTokenRange& range, const CSSParserContext& context)
{
    RefPtr<CSSValue> result;
    do {
        RefPtr<CSSValue> value = consumeBackgroundComponent(property, range, context);
        if (!value)
            return nullptr;
        addBackgroundValue(result, value.releaseNonNull());
    } while (consumeCommaIncludingWhitespace(range));
    return result;
}

#if ENABLE(CSS_GRID_LAYOUT)
static RefPtr<CSSPrimitiveValue> consumeSelfPositionKeyword(CSSParserTokenRange& range)
{
    CSSValueID id = range.peek().id();
    if (id == CSSValueStart || id == CSSValueEnd || id == CSSValueCenter
        || id == CSSValueSelfStart || id == CSSValueSelfEnd || id == CSSValueFlexStart
        || id == CSSValueFlexEnd || id == CSSValueLeft || id == CSSValueRight)
        return consumeIdent(range);
    return nullptr;
}

static RefPtr<CSSValue> consumeSelfPositionOverflowPosition(CSSParserTokenRange& range)
{
    if (identMatches<CSSValueAuto, CSSValueNormal, CSSValueStretch, CSSValueBaseline, CSSValueLastBaseline>(range.peek().id()))
        return consumeIdent(range);

    RefPtr<CSSPrimitiveValue> overflowPosition = consumeIdent<CSSValueUnsafe, CSSValueSafe>(range);
    RefPtr<CSSPrimitiveValue> selfPosition = consumeSelfPositionKeyword(range);
    if (!selfPosition)
        return nullptr;
    if (!overflowPosition)
        overflowPosition = consumeIdent<CSSValueUnsafe, CSSValueSafe>(range);
    if (overflowPosition)
        return createPrimitiveValuePair(selfPosition.releaseNonNull(), overflowPosition.releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce);
    return selfPosition;
}

static RefPtr<CSSValue> consumeAlignItems(CSSParserTokenRange& range)
{
    // align-items property does not allow the 'auto' value.
    if (identMatches<CSSValueAuto>(range.peek().id()))
        return nullptr;
    return consumeSelfPositionOverflowPosition(range);
}

static RefPtr<CSSValue> consumeJustifyItems(CSSParserTokenRange& range)
{
    CSSParserTokenRange rangeCopy = range;
    RefPtr<CSSPrimitiveValue> legacy = consumeIdent<CSSValueLegacy>(rangeCopy);
    RefPtr<CSSPrimitiveValue> positionKeyword = consumeIdent<CSSValueCenter, CSSValueLeft, CSSValueRight>(rangeCopy);
    if (!legacy)
        legacy = consumeIdent<CSSValueLegacy>(rangeCopy);
    if (legacy && positionKeyword) {
        range = rangeCopy;
        return createPrimitiveValuePair(legacy.releaseNonNull(), positionKeyword.releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce);
    }
    return consumeSelfPositionOverflowPosition(range);
}

static RefPtr<CSSValue> consumeFitContent(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);
    RefPtr<CSSPrimitiveValue> length = consumeLengthOrPercent(args, cssParserMode, ValueRangeNonNegative, UnitlessQuirk::Allow);
    if (!length || !args.atEnd())
        return nullptr;
    range = rangeCopy;
    RefPtr<CSSFunctionValue> result = CSSFunctionValue::create(CSSValueFitContent);
    result->append(length.releaseNonNull());
    return result;
}

static RefPtr<CSSPrimitiveValue> consumeCustomIdentForGridLine(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto || range.peek().id() == CSSValueSpan)
        return nullptr;
    return consumeCustomIdent(range);
}

static RefPtr<CSSValue> consumeGridLine(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueAuto)
        return consumeIdent(range);

    RefPtr<CSSPrimitiveValue> spanValue;
    RefPtr<CSSPrimitiveValue> gridLineName;
    RefPtr<CSSPrimitiveValue> numericValue = consumeInteger(range);
    if (numericValue) {
        gridLineName = consumeCustomIdentForGridLine(range);
        spanValue = consumeIdent<CSSValueSpan>(range);
    } else {
        spanValue = consumeIdent<CSSValueSpan>(range);
        if (spanValue) {
            numericValue = consumeInteger(range);
            gridLineName = consumeCustomIdentForGridLine(range);
            if (!numericValue)
                numericValue = consumeInteger(range);
        } else {
            gridLineName = consumeCustomIdentForGridLine(range);
            if (gridLineName) {
                numericValue = consumeInteger(range);
                spanValue = consumeIdent<CSSValueSpan>(range);
                if (!spanValue && !numericValue)
                    return gridLineName;
            } else {
                return nullptr;
            }
        }
    }

    if (spanValue && !numericValue && !gridLineName)
        return nullptr; // "span" keyword alone is invalid.
    if (spanValue && numericValue && numericValue->intValue() < 0)
        return nullptr; // Negative numbers are not allowed for span.
    if (numericValue && numericValue->intValue() == 0)
        return nullptr; // An <integer> value of zero makes the declaration invalid.

    RefPtr<CSSValueList> values = CSSValueList::createSpaceSeparated();
    if (spanValue)
        values->append(spanValue.releaseNonNull());
    if (numericValue)
        values->append(numericValue.releaseNonNull());
    if (gridLineName)
        values->append(gridLineName.releaseNonNull());
    ASSERT(values->length());
    return values;
}

static bool isGridTrackFixedSized(const CSSPrimitiveValue& primitiveValue)
{
    CSSValueID valueID = primitiveValue.valueID();
    if (valueID == CSSValueWebkitMinContent || valueID == CSSValueWebkitMaxContent || valueID == CSSValueAuto || primitiveValue.isFlex())
        return false;

    return true;
}

static bool isGridTrackFixedSized(const CSSValue& value)
{
    if (value.isPrimitiveValue())
        return isGridTrackFixedSized(downcast<CSSPrimitiveValue>(value));

    ASSERT(value.isFunctionValue());
    auto& function = downcast<CSSFunctionValue>(value);
    if (function.name() == CSSValueFitContent || function.arguments() == nullptr || function.arguments()->length() < 2)
        return false;

    CSSValue* minPrimitiveValue = downcast<CSSPrimitiveValue>(function.arguments()->item(0));
    CSSValue* maxPrimitiveValue = downcast<CSSPrimitiveValue>(function.arguments()->item(1));
    return isGridTrackFixedSized(*minPrimitiveValue) || isGridTrackFixedSized(*maxPrimitiveValue);
}

static Vector<String> parseGridTemplateAreasColumnNames(const String& gridRowNames)
{
    ASSERT(!gridRowNames.isEmpty());
    Vector<String> columnNames;
    // Using StringImpl to avoid checks and indirection in every call to String::operator[].
    StringImpl& text = *gridRowNames.impl();

    StringBuilder areaName;
    for (unsigned i = 0; i < text.length(); ++i) {
        if (isCSSSpace(text[i])) {
            if (!areaName.isEmpty()) {
                columnNames.append(areaName.toString());
                areaName.clear();
            }
            continue;
        }
        if (text[i] == '.') {
            if (areaName == ".")
                continue;
            if (!areaName.isEmpty()) {
                columnNames.append(areaName.toString());
                areaName.clear();
            }
        } else {
            if (!isNameCodePoint(text[i]))
                return Vector<String>();
            if (areaName == ".") {
                columnNames.append(areaName.toString());
                areaName.clear();
            }
        }

        areaName.append(text[i]);
    }

    if (!areaName.isEmpty())
        columnNames.append(areaName.toString());

    return columnNames;
}

static bool parseGridTemplateAreasRow(const String& gridRowNames, NamedGridAreaMap& gridAreaMap, const size_t rowCount, size_t& columnCount)
{
    if (gridRowNames.isEmpty() || gridRowNames.containsOnlyWhitespace())
        return false;

    Vector<String> columnNames = parseGridTemplateAreasColumnNames(gridRowNames);
    if (rowCount == 0) {
        columnCount = columnNames.size();
        if (columnCount == 0)
            return false;
    } else if (columnCount != columnNames.size()) {
        // The declaration is invalid if all the rows don't have the number of columns.
        return false;
    }

    for (size_t currentColumn = 0; currentColumn < columnCount; ++currentColumn) {
        const String& gridAreaName = columnNames[currentColumn];

        // Unamed areas are always valid (we consider them to be 1x1).
        if (gridAreaName == ".")
            continue;

        size_t lookAheadColumn = currentColumn + 1;
        while (lookAheadColumn < columnCount && columnNames[lookAheadColumn] == gridAreaName)
            lookAheadColumn++;

        NamedGridAreaMap::iterator gridAreaIt = gridAreaMap.find(gridAreaName);
        if (gridAreaIt == gridAreaMap.end()) {
            gridAreaMap.add(gridAreaName, GridArea(GridSpan::translatedDefiniteGridSpan(rowCount, rowCount + 1), GridSpan::translatedDefiniteGridSpan(currentColumn, lookAheadColumn)));
        } else {
            GridArea& gridArea = gridAreaIt->value;

            // The following checks test that the grid area is a single filled-in rectangle.
            // 1. The new row is adjacent to the previously parsed row.
            if (rowCount != gridArea.rows.endLine())
                return false;

            // 2. The new area starts at the same position as the previously parsed area.
            if (currentColumn != gridArea.columns.startLine())
                return false;

            // 3. The new area ends at the same position as the previously parsed area.
            if (lookAheadColumn != gridArea.columns.endLine())
                return false;

            gridArea.rows = GridSpan::translatedDefiniteGridSpan(gridArea.rows.startLine(), gridArea.rows.endLine() + 1);
        }
        currentColumn = lookAheadColumn - 1;
    }

    return true;
}

static RefPtr<CSSPrimitiveValue> consumeGridBreadth(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    const CSSParserToken& token = range.peek();
    if (identMatches<CSSValueWebkitMinContent, CSSValueWebkitMaxContent, CSSValueAuto>(token.id()))
        return consumeIdent(range);
    if (token.type() == DimensionToken && token.unitType() == CSSPrimitiveValue::UnitTypes::CSS_FR) {
        if (range.peek().numericValue() < 0)
            return nullptr;
        return CSSPrimitiveValue::create(range.consumeIncludingWhitespace().numericValue(), CSSPrimitiveValue::UnitTypes::CSS_FR);
    }
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative, UnitlessQuirk::Allow);
}

static RefPtr<CSSValue> consumeGridTrackSize(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    const CSSParserToken& token = range.peek();
    if (identMatches<CSSValueAuto>(token.id()))
        return consumeIdent(range);

    if (token.functionId() == CSSValueMinmax) {
        CSSParserTokenRange rangeCopy = range;
        CSSParserTokenRange args = consumeFunction(rangeCopy);
        RefPtr<CSSPrimitiveValue> minTrackBreadth = consumeGridBreadth(args, cssParserMode);
        if (!minTrackBreadth || minTrackBreadth->isFlex() || !consumeCommaIncludingWhitespace(args))
            return nullptr;
        RefPtr<CSSPrimitiveValue> maxTrackBreadth = consumeGridBreadth(args, cssParserMode);
        if (!maxTrackBreadth || !args.atEnd())
            return nullptr;
        range = rangeCopy;
        RefPtr<CSSFunctionValue> result = CSSFunctionValue::create(CSSValueMinmax);
        result->append(minTrackBreadth.releaseNonNull());
        result->append(maxTrackBreadth.releaseNonNull());
        return result;
    }

    if (token.functionId() == CSSValueFitContent)
        return consumeFitContent(range, cssParserMode);

    return consumeGridBreadth(range, cssParserMode);
}

// Appends to the passed in CSSGridLineNamesValue if any, otherwise creates a new one.
static RefPtr<CSSGridLineNamesValue> consumeGridLineNames(CSSParserTokenRange& range, CSSGridLineNamesValue* lineNames = nullptr)
{
    CSSParserTokenRange rangeCopy = range;
    if (rangeCopy.consumeIncludingWhitespace().type() != LeftBracketToken)
        return nullptr;
    
    RefPtr<CSSGridLineNamesValue> result = lineNames;
    if (!result)
        result = CSSGridLineNamesValue::create();
    while (RefPtr<CSSPrimitiveValue> lineName = consumeCustomIdentForGridLine(rangeCopy))
        result->append(lineName.releaseNonNull());
    if (rangeCopy.consumeIncludingWhitespace().type() != RightBracketToken)
        return nullptr;
    range = rangeCopy;
    return result;
}

static bool consumeGridTrackRepeatFunction(CSSParserTokenRange& range, CSSParserMode cssParserMode, CSSValueList& list, bool& isAutoRepeat, bool& allTracksAreFixedSized)
{
    CSSParserTokenRange args = consumeFunction(range);
    // The number of repetitions for <auto-repeat> is not important at parsing level
    // because it will be computed later, let's set it to 1.
    size_t repetitions = 1;
    isAutoRepeat = identMatches<CSSValueAutoFill, CSSValueAutoFit>(args.peek().id());
    RefPtr<CSSValueList> repeatedValues;
    if (isAutoRepeat)
        repeatedValues = CSSGridAutoRepeatValue::create(args.consumeIncludingWhitespace().id());
    else {
        // FIXME: a consumeIntegerRaw would be more efficient here.
        RefPtr<CSSPrimitiveValue> repetition = consumePositiveInteger(args);
        if (!repetition)
            return false;
        repetitions = clampTo<size_t>(repetition->doubleValue(), 0, kGridMaxTracks);
        repeatedValues = CSSValueList::createSpaceSeparated();
    }
    if (!consumeCommaIncludingWhitespace(args))
        return false;
    RefPtr<CSSGridLineNamesValue> lineNames = consumeGridLineNames(args);
    if (lineNames)
        repeatedValues->append(lineNames.releaseNonNull());

    size_t numberOfTracks = 0;
    while (!args.atEnd()) {
        RefPtr<CSSValue> trackSize = consumeGridTrackSize(args, cssParserMode);
        if (!trackSize)
            return false;
        if (allTracksAreFixedSized)
            allTracksAreFixedSized = isGridTrackFixedSized(*trackSize);
        repeatedValues->append(trackSize.releaseNonNull());
        ++numberOfTracks;
        lineNames = consumeGridLineNames(args);
        if (lineNames)
            repeatedValues->append(lineNames.releaseNonNull());
    }
    // We should have found at least one <track-size> or else it is not a valid <track-list>.
    if (!numberOfTracks)
        return false;

    if (isAutoRepeat)
        list.append(repeatedValues.releaseNonNull());
    else {
        // We clamp the repetitions to a multiple of the repeat() track list's size, while staying below the max grid size.
        repetitions = std::min(repetitions, kGridMaxTracks / numberOfTracks);
        for (size_t i = 0; i < repetitions; ++i) {
            for (size_t j = 0; j < repeatedValues->length(); ++j)
                list.append(*repeatedValues->itemWithoutBoundsCheck(j));
        }
    }
    return true;
}

enum TrackListType { GridTemplate, GridTemplateNoRepeat, GridAuto };

static RefPtr<CSSValue> consumeGridTrackList(CSSParserTokenRange& range, CSSParserMode cssParserMode, TrackListType trackListType)
{
    bool allowGridLineNames = trackListType != GridAuto;
    RefPtr<CSSValueList> values = CSSValueList::createSpaceSeparated();
    RefPtr<CSSGridLineNamesValue> lineNames = consumeGridLineNames(range);
    if (lineNames) {
        if (!allowGridLineNames)
            return nullptr;
        values->append(lineNames.releaseNonNull());
    }
    
    bool allowRepeat = trackListType == GridTemplate;
    bool seenAutoRepeat = false;
    bool allTracksAreFixedSized = true;
    do {
        bool isAutoRepeat;
        if (range.peek().functionId() == CSSValueRepeat) {
            if (!allowRepeat)
                return nullptr;
            if (!consumeGridTrackRepeatFunction(range, cssParserMode, *values, isAutoRepeat, allTracksAreFixedSized))
                return nullptr;
            if (isAutoRepeat && seenAutoRepeat)
                return nullptr;
            seenAutoRepeat = seenAutoRepeat || isAutoRepeat;
        } else if (RefPtr<CSSValue> value = consumeGridTrackSize(range, cssParserMode)) {
            if (allTracksAreFixedSized)
                allTracksAreFixedSized = isGridTrackFixedSized(*value);
            values->append(value.releaseNonNull());
        } else {
            return nullptr;
        }
        if (seenAutoRepeat && !allTracksAreFixedSized)
            return nullptr;
        lineNames = consumeGridLineNames(range);
        if (lineNames) {
            if (!allowGridLineNames)
                return nullptr;
            values->append(lineNames.releaseNonNull());
        }
    } while (!range.atEnd() && range.peek().type() != DelimiterToken);
    return values;
}

static RefPtr<CSSValue> consumeGridTemplatesRowsOrColumns(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumeGridTrackList(range, cssParserMode, GridTemplate);
}

static RefPtr<CSSValue> consumeGridTemplateAreas(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    NamedGridAreaMap gridAreaMap;
    size_t rowCount = 0;
    size_t columnCount = 0;

    while (range.peek().type() == StringToken) {
        if (!parseGridTemplateAreasRow(range.consumeIncludingWhitespace().value().toString(), gridAreaMap, rowCount, columnCount))
            return nullptr;
        ++rowCount;
    }

    if (rowCount == 0)
        return nullptr;
    ASSERT(columnCount);
    return CSSGridTemplateAreasValue::create(gridAreaMap, rowCount, columnCount);
}
#endif

#if ENABLE(CSS_REGIONS)
static RefPtr<CSSValue> consumeFlowProperty(CSSParserTokenRange& range)
{
    RefPtr<CSSValue> result;
    if (range.peek().id() == CSSValueNone)
        result = consumeIdent(range);
    else
        result = consumeCustomIdent(range);
    return result;
}
#endif

static RefPtr<CSSValue> consumeLineBoxContain(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);

    LineBoxContain lineBoxContain = LineBoxContainNone;
    
    while (range.peek().type() == IdentToken) {
        auto id = range.peek().id();
        if (id == CSSValueBlock) {
            if (lineBoxContain & LineBoxContainBlock)
                return nullptr;
            lineBoxContain |= LineBoxContainBlock;
        } else if (id == CSSValueInline) {
            if (lineBoxContain & LineBoxContainInline)
                return nullptr;
            lineBoxContain |= LineBoxContainInline;
        } else if (id == CSSValueFont) {
            if (lineBoxContain & LineBoxContainFont)
                return nullptr;
            lineBoxContain |= LineBoxContainFont;
        } else if (id == CSSValueGlyphs) {
            if (lineBoxContain & LineBoxContainGlyphs)
                return nullptr;
            lineBoxContain |= LineBoxContainGlyphs;
        } else if (id == CSSValueReplaced) {
            if (lineBoxContain & LineBoxContainReplaced)
                return nullptr;
            lineBoxContain |= LineBoxContainReplaced;
        } else if (id == CSSValueInlineBox) {
            if (lineBoxContain & LineBoxContainInlineBox)
                return nullptr;
            lineBoxContain |= LineBoxContainInlineBox;
        } else if (id == CSSValueInitialLetter) {
            if (lineBoxContain & LineBoxContainInitialLetter)
                return nullptr;
            lineBoxContain |= LineBoxContainInitialLetter;
        } else
            return nullptr;
        range.consumeIncludingWhitespace();
    }
    
    if (!lineBoxContain)
        return nullptr;
    
    return CSSLineBoxContainValue::create(lineBoxContain);
}

static RefPtr<CSSValue> consumeLineGrid(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumeCustomIdent(range);
}

static RefPtr<CSSValue> consumeInitialLetter(CSSParserTokenRange& range)
{
    RefPtr<CSSValue> ident = consumeIdent<CSSValueNormal>(range);
    if (ident)
        return ident;
    
    RefPtr<CSSPrimitiveValue> height = consumeNumber(range, ValueRangeNonNegative);
    if (!height)
        return nullptr;
    
    RefPtr<CSSPrimitiveValue> position;
    if (!range.atEnd()) {
        position = consumeNumber(range, ValueRangeNonNegative);
        if (!position || !range.atEnd())
            return nullptr;
    } else
        position = height.copyRef();
    
    return createPrimitiveValuePair(position.releaseNonNull(), WTFMove(height));
}

static RefPtr<CSSValue> consumeHangingPunctuation(CSSParserTokenRange& range)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    
    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    std::bitset<numCSSValueKeywords> seenValues;

    bool seenForceEnd = false;
    bool seenAllowEnd = false;
    bool seenFirst = false;
    bool seenLast = false;

    while (!range.atEnd()) {
        CSSValueID valueID = range.peek().id();
        if ((valueID == CSSValueFirst && seenFirst)
            || (valueID == CSSValueLast && seenLast)
            || (valueID == CSSValueAllowEnd && (seenAllowEnd || seenForceEnd))
            || (valueID == CSSValueForceEnd && (seenAllowEnd || seenForceEnd)))
            return nullptr;
        RefPtr<CSSValue> ident = consumeIdent<CSSValueAllowEnd, CSSValueForceEnd, CSSValueFirst, CSSValueLast>(range);
        if (!ident)
            return nullptr;
        switch (valueID) {
        case CSSValueAllowEnd:
            seenAllowEnd = true;
            break;
        case CSSValueForceEnd:
            seenForceEnd = true;
            break;
        case CSSValueFirst:
            seenFirst = true;
            break;
        case CSSValueLast:
            seenLast = true;
            break;
        default:
            break;
        }
        list->append(ident.releaseNonNull());
    }
    
    return list->length() ? list : nullptr;
}

static RefPtr<CSSValue> consumeWebkitMarqueeIncrement(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().type() == IdentToken)
        return consumeIdent<CSSValueSmall, CSSValueMedium, CSSValueLarge>(range);
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeAll);
}

static RefPtr<CSSValue> consumeWebkitMarqueeRepetition(CSSParserTokenRange& range)
{
    if (range.peek().type() == IdentToken)
        return consumeIdent<CSSValueInfinite>(range);
    return consumeNumber(range, ValueRangeNonNegative);
}

static RefPtr<CSSValue> consumeWebkitMarqueeSpeed(CSSParserTokenRange& range, CSSParserMode cssParserMode)
{
    if (range.peek().type() == IdentToken)
        return consumeIdent<CSSValueSlow, CSSValueNormal, CSSValueFast>(range);
    return consumeTime(range, cssParserMode, ValueRangeNonNegative, UnitlessQuirk::Allow);
}

static RefPtr<CSSValue> consumeAlt(CSSParserTokenRange& range, const CSSParserContext& context)
{
    if (range.peek().type() == StringToken)
        return consumeString(range);
    
    if (range.peek().functionId() != CSSValueAttr)
        return nullptr;
    
    return consumeAttr(consumeFunction(range), context);
}

static RefPtr<CSSValue> consumeWebkitAspectRatio(CSSParserTokenRange& range)
{
    if (range.peek().type() == IdentToken)
        return consumeIdent<CSSValueAuto, CSSValueFromDimensions, CSSValueFromIntrinsic>(range);
    
    RefPtr<CSSPrimitiveValue> leftValue = consumeNumber(range, ValueRangeNonNegative);
    if (!leftValue || range.atEnd() || !consumeSlashIncludingWhitespace(range))
        return nullptr;
    RefPtr<CSSPrimitiveValue> rightValue = consumeNumber(range, ValueRangeNonNegative);
    if (!rightValue)
        return nullptr;
    
    return CSSAspectRatioValue::create(leftValue->floatValue(), rightValue->floatValue());
}

static RefPtr<CSSValue> consumeTextEmphasisPosition(CSSParserTokenRange& range)
{
    bool foundOverOrUnder = false;
    CSSValueID overUnderValueID = CSSValueOver;
    bool foundLeftOrRight = false;
    CSSValueID leftRightValueID = CSSValueRight;
    while (!range.atEnd()) {
        switch (range.peek().id()) {
        case CSSValueOver:
            if (foundOverOrUnder)
                return nullptr;
            foundOverOrUnder = true;
            overUnderValueID = CSSValueOver;
            break;
        case CSSValueUnder:
            if (foundOverOrUnder)
                return nullptr;
            foundOverOrUnder = true;
            overUnderValueID = CSSValueUnder;
            break;
        case CSSValueLeft:
            if (foundLeftOrRight)
                return nullptr;
            foundLeftOrRight = true;
            leftRightValueID = CSSValueLeft;
            break;
        case CSSValueRight:
            if (foundLeftOrRight)
                return nullptr;
            foundLeftOrRight = true;
            leftRightValueID = CSSValueRight;
            break;
        default:
            return nullptr;
        }
        
        range.consumeIncludingWhitespace();
    }
    if (!foundOverOrUnder)
        return nullptr;
    RefPtr<CSSValueList> list = CSSValueList::createSpaceSeparated();
    list->append(CSSValuePool::singleton().createIdentifierValue(overUnderValueID));
    if (foundLeftOrRight)
        list->append(CSSValuePool::singleton().createIdentifierValue(leftRightValueID));
    return list;
}
    
RefPtr<CSSValue> CSSPropertyParser::parseSingleValue(CSSPropertyID property, CSSPropertyID currentShorthand)
{
    if (CSSParserFastPaths::isKeywordPropertyID(property)) {
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(property, m_range.peek().id(), m_context.mode))
            return nullptr;
        return consumeIdent(m_range);
    }
    switch (property) {
    case CSSPropertyWillChange:
        return consumeWillChange(m_range);
    case CSSPropertyPage:
        return consumePage(m_range);
    case CSSPropertyQuotes:
        return consumeQuotes(m_range);
    case CSSPropertyFontVariantCaps:
        return consumeFontVariantCaps(m_range);
    case CSSPropertyFontVariantLigatures:
        return consumeFontVariantLigatures(m_range);
    case CSSPropertyFontVariantNumeric:
        return consumeFontVariantNumeric(m_range);
    case CSSPropertyFontFeatureSettings:
        return consumeFontFeatureSettings(m_range);
    case CSSPropertyFontFamily:
        return consumeFontFamily(m_range);
    case CSSPropertyFontWeight:
        return consumeFontWeight(m_range);
    case CSSPropertyFontSynthesis:
        return consumeFontSynthesis(m_range);
#if ENABLE(VARIATION_FONTS)
    case CSSPropertyFontVariationSettings:
        if (m_context.variationFontsEnabled)
            return consumeFontVariationSettings(m_range);
        return nullptr;
#endif
    case CSSPropertyLetterSpacing:
        return consumeLetterSpacing(m_range, m_context.mode);
    case CSSPropertyWordSpacing:
        return consumeWordSpacing(m_range, m_context.mode);
    case CSSPropertyTabSize:
        return consumeTabSize(m_range, m_context.mode);
#if ENABLE(TEXT_AUTOSIZING)
    case CSSPropertyWebkitTextSizeAdjust:
        // FIXME: Support toggling the validation of this property via a runtime setting that is independent of
        // whether isTextAutosizingEnabled() is true. We want to enable this property on iOS, when simulating
        // a iOS device in Safari's responsive design mode and when optionally enabled in DRT/WTR. Otherwise,
        // this property should be disabled by default.
#if !PLATFORM(IOS)
        if (!m_context.textAutosizingEnabled)
            return nullptr;
#endif
        return consumeTextSizeAdjust(m_range, m_context.mode);
#endif
    case CSSPropertyFontSize:
        return consumeFontSize(m_range, m_context.mode, UnitlessQuirk::Allow);
    case CSSPropertyLineHeight:
        return consumeLineHeight(m_range, m_context.mode);
    case CSSPropertyWebkitBorderHorizontalSpacing:
    case CSSPropertyWebkitBorderVerticalSpacing:
        return consumeLength(m_range, m_context.mode, ValueRangeNonNegative);
    case CSSPropertyCounterIncrement:
    case CSSPropertyCounterReset:
        return consumeCounter(m_range, property == CSSPropertyCounterIncrement ? 1 : 0);
    case CSSPropertySize:
        return consumeSize(m_range, m_context.mode);
    case CSSPropertyTextIndent:
        return consumeTextIndent(m_range, m_context.mode);
    case CSSPropertyMaxWidth:
    case CSSPropertyMaxHeight:
        return consumeMaxWidthOrHeight(m_range, m_context, UnitlessQuirk::Allow);
    case CSSPropertyWebkitMaxLogicalWidth:
    case CSSPropertyWebkitMaxLogicalHeight:
        return consumeMaxWidthOrHeight(m_range, m_context);
    case CSSPropertyMinWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyWidth:
    case CSSPropertyHeight:
        return consumeWidthOrHeight(m_range, m_context, UnitlessQuirk::Allow);
    case CSSPropertyWebkitMinLogicalWidth:
    case CSSPropertyWebkitMinLogicalHeight:
    case CSSPropertyWebkitLogicalWidth:
    case CSSPropertyWebkitLogicalHeight:
        return consumeWidthOrHeight(m_range, m_context);
    case CSSPropertyMarginTop:
    case CSSPropertyMarginRight:
    case CSSPropertyMarginBottom:
    case CSSPropertyMarginLeft:
    case CSSPropertyBottom:
    case CSSPropertyLeft:
    case CSSPropertyRight:
    case CSSPropertyTop:
        return consumeMarginOrOffset(m_range, m_context.mode, UnitlessQuirk::Allow);
    case CSSPropertyWebkitMarginStart:
    case CSSPropertyWebkitMarginEnd:
    case CSSPropertyWebkitMarginBefore:
    case CSSPropertyWebkitMarginAfter:
        return consumeMarginOrOffset(m_range, m_context.mode, UnitlessQuirk::Forbid);
    case CSSPropertyPaddingTop:
    case CSSPropertyPaddingRight:
    case CSSPropertyPaddingBottom:
    case CSSPropertyPaddingLeft:
        return consumeLengthOrPercent(m_range, m_context.mode, ValueRangeNonNegative, UnitlessQuirk::Allow);
    case CSSPropertyWebkitPaddingStart:
    case CSSPropertyWebkitPaddingEnd:
    case CSSPropertyWebkitPaddingBefore:
    case CSSPropertyWebkitPaddingAfter:
        return consumeLengthOrPercent(m_range, m_context.mode, ValueRangeNonNegative, UnitlessQuirk::Forbid);
    case CSSPropertyClip:
        return consumeClip(m_range, m_context.mode);
#if ENABLE(TOUCH_EVENTS)
    case CSSPropertyTouchAction:
        return consumeTouchAction(m_range);
#endif
    case CSSPropertyObjectPosition:
        return consumePosition(m_range, m_context.mode, UnitlessQuirk::Forbid);
    case CSSPropertyWebkitLineClamp:
        return consumeLineClamp(m_range);
    case CSSPropertyWebkitFontSizeDelta:
        return consumeLength(m_range, m_context.mode, ValueRangeAll, UnitlessQuirk::Allow);
    case CSSPropertyWebkitHyphenateCharacter:
    case CSSPropertyWebkitLocale:
        return consumeLocale(m_range);
    case CSSPropertyWebkitHyphenateLimitBefore:
    case CSSPropertyWebkitHyphenateLimitAfter:
        return consumeHyphenateLimit(m_range, CSSValueAuto);
    case CSSPropertyWebkitHyphenateLimitLines:
        return consumeHyphenateLimit(m_range, CSSValueNoLimit);
    case CSSPropertyColumnWidth:
        return consumeColumnWidth(m_range);
    case CSSPropertyColumnCount:
        return consumeColumnCount(m_range);
    case CSSPropertyColumnGap:
        return consumeColumnGap(m_range, m_context.mode);
    case CSSPropertyColumnSpan:
        return consumeColumnSpan(m_range);
    case CSSPropertyZoom:
        return consumeZoom(m_range, m_context);
    case CSSPropertyAnimationDelay:
    case CSSPropertyTransitionDelay:
    case CSSPropertyAnimationDirection:
    case CSSPropertyAnimationDuration:
    case CSSPropertyTransitionDuration:
    case CSSPropertyAnimationFillMode:
    case CSSPropertyAnimationIterationCount:
    case CSSPropertyAnimationName:
    case CSSPropertyAnimationPlayState:
    case CSSPropertyTransitionProperty:
    case CSSPropertyAnimationTimingFunction:
    case CSSPropertyTransitionTimingFunction:
        return consumeAnimationPropertyList(property, m_range, m_context);
#if ENABLE(CSS_GRID_LAYOUT)
    case CSSPropertyGridColumnGap:
    case CSSPropertyGridRowGap:
        return consumeLength(m_range, m_context.mode, ValueRangeNonNegative);
#endif
    case CSSPropertyShapeMargin:
        return consumeLengthOrPercent(m_range, m_context.mode, ValueRangeNonNegative);
    case CSSPropertyShapeImageThreshold:
        return consumeNumber(m_range, ValueRangeAll);
    case CSSPropertyWebkitBoxOrdinalGroup:
    case CSSPropertyOrphans:
    case CSSPropertyWidows:
        return consumePositiveInteger(m_range);
    case CSSPropertyWebkitTextDecorationColor:
        return consumeColor(m_range, m_context.mode);
    case CSSPropertyWebkitTextDecorationSkip:
        return consumeTextDecorationSkip(m_range);
    case CSSPropertyWebkitTextStrokeWidth:
        return consumeTextStrokeWidth(m_range, m_context.mode);
    case CSSPropertyWebkitTextFillColor:
#if ENABLE(TOUCH_EVENTS)
    case CSSPropertyWebkitTapHighlightColor:
#endif
    case CSSPropertyWebkitTextEmphasisColor:
    case CSSPropertyWebkitBorderStartColor:
    case CSSPropertyWebkitBorderEndColor:
    case CSSPropertyWebkitBorderBeforeColor:
    case CSSPropertyWebkitBorderAfterColor:
    case CSSPropertyWebkitTextStrokeColor:
    case CSSPropertyStopColor:
    case CSSPropertyFloodColor:
    case CSSPropertyLightingColor:
    case CSSPropertyColumnRuleColor:
        return consumeColor(m_range, m_context.mode);
    case CSSPropertyColor:
    case CSSPropertyBackgroundColor:
        return consumeColor(m_range, m_context.mode, inQuirksMode());
    case CSSPropertyWebkitBorderStartWidth:
    case CSSPropertyWebkitBorderEndWidth:
    case CSSPropertyWebkitBorderBeforeWidth:
    case CSSPropertyWebkitBorderAfterWidth:
        return consumeBorderWidth(m_range, m_context.mode, UnitlessQuirk::Forbid);
    case CSSPropertyBorderBottomColor:
    case CSSPropertyBorderLeftColor:
    case CSSPropertyBorderRightColor:
    case CSSPropertyBorderTopColor: {
        bool allowQuirkyColors = inQuirksMode()
            && (currentShorthand == CSSPropertyInvalid || currentShorthand == CSSPropertyBorderColor);
        return consumeColor(m_range, m_context.mode, allowQuirkyColors);
    }
    case CSSPropertyBorderBottomWidth:
    case CSSPropertyBorderLeftWidth:
    case CSSPropertyBorderRightWidth:
    case CSSPropertyBorderTopWidth: {
        bool allowQuirkyLengths = inQuirksMode()
            && (currentShorthand == CSSPropertyInvalid || currentShorthand == CSSPropertyBorderWidth);
        UnitlessQuirk unitless = allowQuirkyLengths ? UnitlessQuirk::Allow : UnitlessQuirk::Forbid;
        return consumeBorderWidth(m_range, m_context.mode, unitless);
    }
    case CSSPropertyZIndex:
        return consumeZIndex(m_range);
    case CSSPropertyTextShadow: // CSS2 property, dropped in CSS2.1, back in CSS3, so treat as CSS3
    case CSSPropertyBoxShadow:
    case CSSPropertyWebkitBoxShadow:
    case CSSPropertyWebkitSvgShadow:
        return consumeShadow(m_range, m_context.mode, property == CSSPropertyBoxShadow || property == CSSPropertyWebkitBoxShadow);
    case CSSPropertyFilter:
#if ENABLE(FILTERS_LEVEL_2)
    case CSSPropertyWebkitBackdropFilter:
#endif
        return consumeFilter(m_range, m_context);
    case CSSPropertyTextDecoration:
    case CSSPropertyWebkitTextDecorationsInEffect:
    case CSSPropertyWebkitTextDecorationLine:
        return consumeTextDecorationLine(m_range);
    case CSSPropertyWebkitTextEmphasisStyle:
        return consumeTextEmphasisStyle(m_range);
    case CSSPropertyOutlineColor:
        return consumeOutlineColor(m_range, m_context.mode);
    case CSSPropertyOutlineOffset:
        return consumeLength(m_range, m_context.mode, ValueRangeAll);
    case CSSPropertyOutlineWidth:
        return consumeLineWidth(m_range, m_context.mode, UnitlessQuirk::Forbid);
    case CSSPropertyTransform:
        return consumeTransform(m_range, m_context.mode);
    case CSSPropertyTransformOriginX:
    case CSSPropertyPerspectiveOriginX:
        return consumePositionX(m_range, m_context.mode);
    case CSSPropertyTransformOriginY:
    case CSSPropertyPerspectiveOriginY:
        return consumePositionY(m_range, m_context.mode);
    case CSSPropertyTransformOriginZ:
        return consumeLength(m_range, m_context.mode, ValueRangeAll);
    case CSSPropertyFill:
    case CSSPropertyStroke:
        return consumePaintStroke(m_range, m_context.mode);
    case CSSPropertyPaintOrder:
        return consumePaintOrder(m_range);
    case CSSPropertyMarkerStart:
    case CSSPropertyMarkerMid:
    case CSSPropertyMarkerEnd:
    case CSSPropertyClipPath:
    case CSSPropertyMask:
        return consumeNoneOrURI(m_range);
    case CSSPropertyFlexBasis:
        return consumeFlexBasis(m_range, m_context.mode);
    case CSSPropertyFlexGrow:
    case CSSPropertyFlexShrink:
        return consumeNumber(m_range, ValueRangeNonNegative);
    case CSSPropertyStrokeDasharray:
        return consumeStrokeDasharray(m_range);
    case CSSPropertyColumnRuleWidth:
        return consumeColumnRuleWidth(m_range, m_context.mode);
    case CSSPropertyStrokeOpacity:
    case CSSPropertyFillOpacity:
    case CSSPropertyStopOpacity:
    case CSSPropertyFloodOpacity:
    case CSSPropertyOpacity:
    case CSSPropertyWebkitBoxFlex:
        return consumeNumber(m_range, ValueRangeAll);
    case CSSPropertyBaselineShift:
        return consumeBaselineShift(m_range);
    case CSSPropertyStrokeMiterlimit:
        return consumeNumber(m_range, ValueRangeNonNegative);
    case CSSPropertyStrokeWidth:
    case CSSPropertyStrokeDashoffset:
    case CSSPropertyCx:
    case CSSPropertyCy:
    case CSSPropertyX:
    case CSSPropertyY:
    case CSSPropertyR:
        return consumeLengthOrPercent(m_range, SVGAttributeMode, ValueRangeAll, UnitlessQuirk::Forbid);
    case CSSPropertyRx:
    case CSSPropertyRy:
        return consumeRxOrRy(m_range);
    case CSSPropertyCursor:
        return consumeCursor(m_range, m_context, inQuirksMode());
    case CSSPropertyContent:
        return consumeContent(m_range, m_context);
    case CSSPropertyListStyleImage:
    case CSSPropertyBorderImageSource:
    case CSSPropertyWebkitMaskBoxImageSource:
        return consumeImageOrNone(m_range, m_context);
    case CSSPropertyPerspective:
        return consumePerspective(m_range, m_context.mode);
#if ENABLE(CSS_SCROLL_SNAP)
    case CSSPropertyWebkitScrollSnapCoordinate:
        return consumeScrollSnapCoordinate(m_range, m_context.mode);
    case CSSPropertyWebkitScrollSnapDestination:
        return consumeScrollSnapDestination(m_range, m_context.mode);
    case CSSPropertyWebkitScrollSnapPointsX:
    case CSSPropertyWebkitScrollSnapPointsY:
        return consumeScrollSnapPoints(m_range, m_context.mode);
#endif
    case CSSPropertyBorderTopRightRadius:
    case CSSPropertyBorderTopLeftRadius:
    case CSSPropertyBorderBottomLeftRadius:
    case CSSPropertyBorderBottomRightRadius:
        return consumeBorderRadiusCorner(m_range, m_context.mode);
    case CSSPropertyWebkitBoxFlexGroup:
        return consumeInteger(m_range, 0);
    case CSSPropertyOrder:
        return consumeInteger(m_range);
    case CSSPropertyWebkitTextUnderlinePosition:
        // auto | [ under || [ left | right ] ], but we only support auto | under for now
        return consumeIdent<CSSValueAuto, CSSValueUnder>(m_range);
    case CSSPropertyVerticalAlign:
        return consumeVerticalAlign(m_range, m_context.mode);
    case CSSPropertyShapeOutside:
        return consumeShapeOutside(m_range, m_context);
    case CSSPropertyWebkitClipPath:
        return consumeWebkitClipPath(m_range, m_context);
    case CSSPropertyJustifyContent:
    case CSSPropertyAlignContent:
        return consumeContentDistributionOverflowPosition(m_range);
    case CSSPropertyBorderImageRepeat:
    case CSSPropertyWebkitMaskBoxImageRepeat:
        return consumeBorderImageRepeat(m_range);
    case CSSPropertyBorderImageSlice:
    case CSSPropertyWebkitMaskBoxImageSlice:
        return consumeBorderImageSlice(property, m_range);
    case CSSPropertyBorderImageOutset:
    case CSSPropertyWebkitMaskBoxImageOutset:
        return consumeBorderImageOutset(m_range);
    case CSSPropertyBorderImageWidth:
    case CSSPropertyWebkitMaskBoxImageWidth:
        return consumeBorderImageWidth(m_range);
    case CSSPropertyWebkitBorderImage:
    case CSSPropertyWebkitMaskBoxImage:
        return consumeWebkitBorderImage(property, m_range, m_context);
    case CSSPropertyWebkitBoxReflect:
        return consumeReflect(m_range, m_context);
    case CSSPropertyWebkitLineBoxContain:
        return consumeLineBoxContain(m_range);
#if ENABLE(CSS_IMAGE_ORIENTATION)
    case CSSPropertyImageOrientation:
        return consumeImageOrientation(m_range, m_context.mode);
#endif
    case CSSPropertyBackgroundAttachment:
    case CSSPropertyBackgroundBlendMode:
    case CSSPropertyBackgroundClip:
    case CSSPropertyBackgroundImage:
    case CSSPropertyBackgroundOrigin:
    case CSSPropertyBackgroundPositionX:
    case CSSPropertyBackgroundPositionY:
    case CSSPropertyBackgroundSize:
    case CSSPropertyWebkitBackgroundClip:
    case CSSPropertyWebkitBackgroundOrigin:
    case CSSPropertyWebkitBackgroundComposite:
    case CSSPropertyWebkitBackgroundSize:
    case CSSPropertyWebkitMaskClip:
    case CSSPropertyWebkitMaskComposite:
    case CSSPropertyWebkitMaskImage:
    case CSSPropertyWebkitMaskOrigin:
    case CSSPropertyWebkitMaskPositionX:
    case CSSPropertyWebkitMaskPositionY:
    case CSSPropertyWebkitMaskSize:
    case CSSPropertyWebkitMaskSourceType:
        return consumeCommaSeparatedBackgroundComponent(property, m_range, m_context);
    case CSSPropertyWebkitMaskRepeatX:
    case CSSPropertyWebkitMaskRepeatY:
        return nullptr;
#if ENABLE(CSS_GRID_LAYOUT)
    case CSSPropertyAlignItems:
        if (!m_context.cssGridLayoutEnabled)
            return nullptr;
        return consumeAlignItems(m_range);
    case CSSPropertyJustifySelf:
    case CSSPropertyAlignSelf:
        if (!m_context.cssGridLayoutEnabled)
            return nullptr;
        return consumeSelfPositionOverflowPosition(m_range);
    case CSSPropertyJustifyItems:
        if (!m_context.cssGridLayoutEnabled)
            return nullptr;
        return consumeJustifyItems(m_range);
    case CSSPropertyGridColumnEnd:
    case CSSPropertyGridColumnStart:
    case CSSPropertyGridRowEnd:
    case CSSPropertyGridRowStart:
        if (!m_context.cssGridLayoutEnabled)
            return nullptr;
        return consumeGridLine(m_range);
    case CSSPropertyGridAutoColumns:
    case CSSPropertyGridAutoRows:
        if (!m_context.cssGridLayoutEnabled)
            return nullptr;
        return consumeGridTrackList(m_range, m_context.mode, GridAuto);
    case CSSPropertyGridTemplateColumns:
    case CSSPropertyGridTemplateRows:
        if (!m_context.cssGridLayoutEnabled)
            return nullptr;
        return consumeGridTemplatesRowsOrColumns(m_range, m_context.mode);
    case CSSPropertyGridTemplateAreas:
        if (!m_context.cssGridLayoutEnabled)
            return nullptr;
        return consumeGridTemplateAreas(m_range);
    case CSSPropertyGridAutoFlow:
        if (!m_context.cssGridLayoutEnabled)
            return nullptr;
        return consumeGridAutoFlow(m_range);
#endif
#if ENABLE(CSS_REGIONS)
    case CSSPropertyWebkitFlowInto:
    case CSSPropertyWebkitFlowFrom:
        return consumeFlowProperty(m_range);
#endif
    case CSSPropertyWebkitLineGrid:
        return consumeLineGrid(m_range);
    case CSSPropertyWebkitInitialLetter:
        return consumeInitialLetter(m_range);
    case CSSPropertyHangingPunctuation:
        return consumeHangingPunctuation(m_range);
    case CSSPropertyWebkitMarqueeIncrement:
        return consumeWebkitMarqueeIncrement(m_range, m_context.mode);
    case CSSPropertyWebkitMarqueeRepetition:
        return consumeWebkitMarqueeRepetition(m_range);
    case CSSPropertyWebkitMarqueeSpeed:
        return consumeWebkitMarqueeSpeed(m_range, m_context.mode);
    case CSSPropertyAlt:
        return consumeAlt(m_range, m_context);
    case CSSPropertyWebkitAspectRatio:
        return consumeWebkitAspectRatio(m_range);
    case CSSPropertyWebkitTextEmphasisPosition:
        return consumeTextEmphasisPosition(m_range);
    default:
        return nullptr;
    }
}

static RefPtr<CSSValueList> consumeFontFaceUnicodeRange(CSSParserTokenRange& range)
{
    RefPtr<CSSValueList> values = CSSValueList::createCommaSeparated();

    do {
        const CSSParserToken& token = range.consumeIncludingWhitespace();
        if (token.type() != UnicodeRangeToken)
            return nullptr;

        UChar32 start = token.unicodeRangeStart();
        UChar32 end = token.unicodeRangeEnd();
        if (start > end)
            return nullptr;
        values->append(CSSUnicodeRangeValue::create(start, end));
    } while (consumeCommaIncludingWhitespace(range));

    return values;
}

static RefPtr<CSSValue> consumeFontFaceSrcURI(CSSParserTokenRange& range, const CSSParserContext& context)
{
    String url = consumeUrlAsStringView(range).toString();
    if (url.isNull())
        return nullptr;
    
    RefPtr<CSSFontFaceSrcValue> uriValue = CSSFontFaceSrcValue::create(context.completeURL(url));

    if (range.peek().functionId() != CSSValueFormat)
        return uriValue;

    // FIXME: https://drafts.csswg.org/css-fonts says that format() contains a comma-separated list of strings,
    // but CSSFontFaceSrcValue stores only one format. Allowing one format for now.
    // FIXME: We're allowing the format to be an identifier as well as a string, because the old
    // parser did. It's not clear if we need to continue to support this behavior, but we have lots of
    // layout tests that rely on it.
    CSSParserTokenRange args = consumeFunction(range);
    const CSSParserToken& arg = args.consumeIncludingWhitespace();
    if ((arg.type() != StringToken && arg.type() != IdentToken) || !args.atEnd())
        return nullptr;
    uriValue->setFormat(arg.value().toString());
    return uriValue;
}

static RefPtr<CSSValue> consumeFontFaceSrcLocal(CSSParserTokenRange& range)
{
    CSSParserTokenRange args = consumeFunction(range);
    if (args.peek().type() == StringToken) {
        const CSSParserToken& arg = args.consumeIncludingWhitespace();
        if (!args.atEnd())
            return nullptr;
        return CSSFontFaceSrcValue::createLocal(arg.value().toString());
    }
    if (args.peek().type() == IdentToken) {
        String familyName = concatenateFamilyName(args);
        if (!args.atEnd())
            return nullptr;
        return CSSFontFaceSrcValue::createLocal(familyName);
    }
    return nullptr;
}

static RefPtr<CSSValueList> consumeFontFaceSrc(CSSParserTokenRange& range, const CSSParserContext& context)
{
    RefPtr<CSSValueList> values = CSSValueList::createCommaSeparated();

    do {
        const CSSParserToken& token = range.peek();
        RefPtr<CSSValue> parsedValue;
        if (token.functionId() == CSSValueLocal)
            parsedValue = consumeFontFaceSrcLocal(range);
        else
            parsedValue = consumeFontFaceSrcURI(range, context);
        if (!parsedValue)
            return nullptr;
        values->append(parsedValue.releaseNonNull());
    } while (consumeCommaIncludingWhitespace(range));
    return values;
}

bool CSSPropertyParser::parseFontFaceDescriptor(CSSPropertyID propId)
{
    RefPtr<CSSValue> parsedValue;
    switch (propId) {
    case CSSPropertyFontFamily:
        parsedValue = consumeFontFamily(m_range);
        break;
    case CSSPropertySrc: // This is a list of urls or local references.
        parsedValue = consumeFontFaceSrc(m_range, m_context);
        break;
    case CSSPropertyUnicodeRange:
        parsedValue = consumeFontFaceUnicodeRange(m_range);
        break;
    case CSSPropertyFontStretch:
    case CSSPropertyFontStyle: {
        CSSValueID id = m_range.consumeIncludingWhitespace().id();
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(propId, id, m_context.mode))
            return false;
        parsedValue = CSSValuePool::singleton().createIdentifierValue(id);
        break;
    }
    case CSSPropertyFontVariant:
        parsedValue = consumeFontVariantList(m_range);
        break;
    case CSSPropertyFontWeight:
        parsedValue = consumeFontWeight(m_range);
        break;
    case CSSPropertyFontFeatureSettings:
        parsedValue = consumeFontFeatureSettings(m_range);
        break;
    default:
        break;
    }

    if (!parsedValue || !m_range.atEnd())
        return false;

    addProperty(propId, CSSPropertyInvalid, *parsedValue, false);
    return true;
}

bool CSSPropertyParser::consumeSystemFont(bool important)
{
    CSSValueID systemFontID = m_range.consumeIncludingWhitespace().id();
    ASSERT(systemFontID >= CSSValueCaption && systemFontID <= CSSValueStatusBar);
    if (!m_range.atEnd())
        return false;
    
    FontCascadeDescription fontDescription;
    RenderTheme::defaultTheme()->systemFont(systemFontID, fontDescription);
    if (!fontDescription.isAbsoluteSize())
        return false;
    
    addProperty(CSSPropertyFontStyle, CSSPropertyFont, CSSValuePool::singleton().createIdentifierValue(fontDescription.italic() == FontItalicOn ? CSSValueItalic : CSSValueNormal), important);
    addProperty(CSSPropertyFontWeight, CSSPropertyFont, CSSValuePool::singleton().createValue(fontDescription.weight()), important);
    addProperty(CSSPropertyFontSize, CSSPropertyFont, CSSValuePool::singleton().createValue(fontDescription.specifiedSize(), CSSPrimitiveValue::CSS_PX), important);
    Ref<CSSValueList> fontFamilyList = CSSValueList::createCommaSeparated();
    fontFamilyList->append(CSSValuePool::singleton().createFontFamilyValue(fontDescription.familyAt(0), FromSystemFontID::Yes));
    addProperty(CSSPropertyFontFamily, CSSPropertyFont, WTFMove(fontFamilyList), important);
    addProperty(CSSPropertyFontVariantCaps, CSSPropertyFont, CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
    addProperty(CSSPropertyLineHeight, CSSPropertyFont, CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);

    // FIXME_NEWPARSER: What about FontVariantNumeric and FontVariantLigatures?

    return true;
}

bool CSSPropertyParser::consumeFont(bool important)
{
    // Let's check if there is an inherit or initial somewhere in the shorthand.
    CSSParserTokenRange range = m_range;
    while (!range.atEnd()) {
        CSSValueID id = range.consumeIncludingWhitespace().id();
        if (id == CSSValueInherit || id == CSSValueInitial)
            return false;
    }
    // Optional font-style, font-variant, font-stretch and font-weight.
    RefPtr<CSSPrimitiveValue> fontStyle;
    RefPtr<CSSPrimitiveValue> fontVariantCaps;
    RefPtr<CSSPrimitiveValue> fontWeight;
    // FIXME-NEWPARSER: Implement. RefPtr<CSSPrimitiveValue> fontStretch;
    while (!m_range.atEnd()) {
        CSSValueID id = m_range.peek().id();
        if (!fontStyle && CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyFontStyle, id, m_context.mode)) {
            fontStyle = consumeIdent(m_range);
            continue;
        }
        if (!fontVariantCaps && (id == CSSValueNormal || id == CSSValueSmallCaps)) {
            // Font variant in the shorthand is particular, it only accepts normal or small-caps.
            // See https://drafts.csswg.org/css-fonts/#propdef-font
            fontVariantCaps = consumeFontVariantCSS21(m_range);
            if (fontVariantCaps)
                continue;
        }
        if (!fontWeight) {
            fontWeight = consumeFontWeight(m_range);
            if (fontWeight)
                continue;
        }
        /* FIXME-NEWPARSER: Implement
         if (!fontStretch && CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyFontStretch, id, m_context.mode))
            fontStretch = consumeIdent(m_range);
        else*/
        break;
    }

    if (m_range.atEnd())
        return false;

    addProperty(CSSPropertyFontStyle, CSSPropertyFont, fontStyle ? fontStyle.releaseNonNull() : CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
    addProperty(CSSPropertyFontVariantCaps, CSSPropertyFont, fontVariantCaps ? fontVariantCaps.releaseNonNull() : CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
/*  
    // FIXME-NEWPARSER: What do we do with these? They aren't part of our fontShorthand().
    addProperty(CSSPropertyFontVariantLigatures, CSSPropertyFont, CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
    addProperty(CSSPropertyFontVariantNumeric, CSSPropertyFont, CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
*/
    
    addProperty(CSSPropertyFontWeight, CSSPropertyFont, fontWeight ? fontWeight.releaseNonNull() : CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
    /* FIXME-NEWPARSER: Implement.
    addProperty(CSSPropertyFontStretch, CSSPropertyFont, fontStretch ? fontStretch.releaseNonNull() : CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
    */

    // Now a font size _must_ come.
    RefPtr<CSSValue> fontSize = consumeFontSize(m_range, m_context.mode);
    if (!fontSize || m_range.atEnd())
        return false;

    addProperty(CSSPropertyFontSize, CSSPropertyFont, *fontSize, important);

    if (consumeSlashIncludingWhitespace(m_range)) {
        RefPtr<CSSPrimitiveValue> lineHeight = consumeLineHeight(m_range, m_context.mode);
        if (!lineHeight)
            return false;
        addProperty(CSSPropertyLineHeight, CSSPropertyFont, lineHeight.releaseNonNull(), important);
    } else {
        addProperty(CSSPropertyLineHeight, CSSPropertyFont, CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
    }

    // Font family must come now.
    RefPtr<CSSValue> parsedFamilyValue = consumeFontFamily(m_range);
    if (!parsedFamilyValue)
        return false;

    addProperty(CSSPropertyFontFamily, CSSPropertyFont, parsedFamilyValue.releaseNonNull(), important);

    // FIXME: http://www.w3.org/TR/2011/WD-css3-fonts-20110324/#font-prop requires that
    // "font-stretch", "font-size-adjust", and "font-kerning" be reset to their initial values
    // but we don't seem to support them at the moment. They should also be added here once implemented.
    return m_range.atEnd();
}

bool CSSPropertyParser::consumeFontVariantShorthand(bool important)
{
    if (identMatches<CSSValueNormal, CSSValueNone>(m_range.peek().id())) {
        addProperty(CSSPropertyFontVariantLigatures, CSSPropertyFontVariant, consumeIdent(m_range).releaseNonNull(), important);
        addProperty(CSSPropertyFontVariantCaps, CSSPropertyFontVariant, CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
        return m_range.atEnd();
    }

    RefPtr<CSSPrimitiveValue> capsValue;
    FontVariantLigaturesParser ligaturesParser;
    FontVariantNumericParser numericParser;
    do {
        FontVariantLigaturesParser::ParseResult ligaturesParseResult = ligaturesParser.consumeLigature(m_range);
        FontVariantNumericParser::ParseResult numericParseResult = numericParser.consumeNumeric(m_range);
        if (ligaturesParseResult == FontVariantLigaturesParser::ParseResult::ConsumedValue
            || numericParseResult == FontVariantNumericParser::ParseResult::ConsumedValue)
            continue;

        if (ligaturesParseResult == FontVariantLigaturesParser::ParseResult::DisallowedValue
            || numericParseResult == FontVariantNumericParser::ParseResult::DisallowedValue)
            return false;

        CSSValueID id = m_range.peek().id();
        switch (id) {
        case CSSValueSmallCaps:
        case CSSValueAllSmallCaps:
        case CSSValuePetiteCaps:
        case CSSValueAllPetiteCaps:
        case CSSValueUnicase:
        case CSSValueTitlingCaps:
            // Only one caps value permitted in font-variant grammar.
            if (capsValue)
                return false;
            capsValue = consumeIdent(m_range);
            break;
        default:
            return false;
        }
    } while (!m_range.atEnd());

    addProperty(CSSPropertyFontVariantLigatures, CSSPropertyFontVariant, ligaturesParser.finalizeValue().releaseNonNull(), important);
    addProperty(CSSPropertyFontVariantNumeric, CSSPropertyFontVariant, numericParser.finalizeValue().releaseNonNull(), important);
    addProperty(CSSPropertyFontVariantCaps, CSSPropertyFontVariant, capsValue ? capsValue.releaseNonNull() : CSSValuePool::singleton().createIdentifierValue(CSSValueNormal), important);
    return true;
}

bool CSSPropertyParser::consumeBorderSpacing(bool important)
{
    RefPtr<CSSValue> horizontalSpacing = consumeLength(m_range, m_context.mode, ValueRangeNonNegative, UnitlessQuirk::Allow);
    if (!horizontalSpacing)
        return false;
    RefPtr<CSSValue> verticalSpacing = horizontalSpacing;
    if (!m_range.atEnd())
        verticalSpacing = consumeLength(m_range, m_context.mode, ValueRangeNonNegative, UnitlessQuirk::Allow);
    if (!verticalSpacing || !m_range.atEnd())
        return false;
    addProperty(CSSPropertyWebkitBorderHorizontalSpacing, CSSPropertyBorderSpacing, horizontalSpacing.releaseNonNull(), important);
    addProperty(CSSPropertyWebkitBorderVerticalSpacing, CSSPropertyBorderSpacing, verticalSpacing.releaseNonNull(), important);
    return true;
}

#if ENABLE(CSS_DEVICE_ADAPTATION)

static RefPtr<CSSValue> consumeSingleViewportDescriptor(CSSParserTokenRange& range, CSSPropertyID propId, CSSParserMode cssParserMode)
{
    CSSValueID id = range.peek().id();
    switch (propId) {
    case CSSPropertyMinWidth:
    case CSSPropertyMaxWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyMaxHeight:
        if (id == CSSValueAuto)
            return consumeIdent(range);
        return consumeLengthOrPercent(range, cssParserMode, ValueRangeNonNegative);
    case CSSPropertyMinZoom:
    case CSSPropertyMaxZoom:
    case CSSPropertyZoom: {
        if (id == CSSValueAuto)
            return consumeIdent(range);
        RefPtr<CSSValue> parsedValue = consumeNumber(range, ValueRangeNonNegative);
        if (parsedValue)
            return parsedValue;
        return consumePercent(range, ValueRangeNonNegative);
    }
    case CSSPropertyUserZoom:
        return consumeIdent<CSSValueZoom, CSSValueFixed>(range);
    case CSSPropertyOrientation:
        return consumeIdent<CSSValueAuto, CSSValuePortrait, CSSValueLandscape>(range);
    default:
        ASSERT_NOT_REACHED();
        break;
    }

    ASSERT_NOT_REACHED();
    return nullptr;
}

bool CSSPropertyParser::parseViewportDescriptor(CSSPropertyID propId, bool important)
{
    switch (propId) {
    case CSSPropertyWidth: {
        RefPtr<CSSValue> minWidth = consumeSingleViewportDescriptor(m_range, CSSPropertyMinWidth, m_context.mode);
        if (!minWidth)
            return false;
        RefPtr<CSSValue> maxWidth = minWidth;
        if (!m_range.atEnd())
            maxWidth = consumeSingleViewportDescriptor(m_range, CSSPropertyMaxWidth, m_context.mode);
        if (!maxWidth || !m_range.atEnd())
            return false;
        addProperty(CSSPropertyMinWidth, CSSPropertyInvalid, *minWidth, important);
        addProperty(CSSPropertyMaxWidth, CSSPropertyInvalid, *maxWidth, important);
        return true;
    }
    case CSSPropertyHeight: {
        RefPtr<CSSValue> minHeight = consumeSingleViewportDescriptor(m_range, CSSPropertyMinHeight, m_context.mode);
        if (!minHeight)
            return false;
        RefPtr<CSSValue> maxHeight = minHeight;
        if (!m_range.atEnd())
            maxHeight = consumeSingleViewportDescriptor(m_range, CSSPropertyMaxHeight, m_context.mode);
        if (!maxHeight || !m_range.atEnd())
            return false;
        addProperty(CSSPropertyMinHeight, CSSPropertyInvalid, *minHeight, important);
        addProperty(CSSPropertyMaxHeight, CSSPropertyInvalid, *maxHeight, important);
        return true;
    }
    case CSSPropertyMinWidth:
    case CSSPropertyMaxWidth:
    case CSSPropertyMinHeight:
    case CSSPropertyMaxHeight:
    case CSSPropertyMinZoom:
    case CSSPropertyMaxZoom:
    case CSSPropertyZoom:
    case CSSPropertyUserZoom:
    case CSSPropertyOrientation: {
        RefPtr<CSSValue> parsedValue = consumeSingleViewportDescriptor(m_range, propId, m_context.mode);
        if (!parsedValue || !m_range.atEnd())
            return false;
        addProperty(propId, CSSPropertyInvalid, parsedValue.releaseNonNull(), important);
        return true;
    }
    default:
        return false;
    }
}

#endif

static bool consumeColumnWidthOrCount(CSSParserTokenRange& range, RefPtr<CSSValue>& columnWidth, RefPtr<CSSValue>& columnCount)
{
    if (range.peek().id() == CSSValueAuto) {
        consumeIdent(range);
        return true;
    }
    if (!columnWidth) {
        columnWidth = consumeColumnWidth(range);
        if (columnWidth)
            return true;
    }
    if (!columnCount)
        columnCount = consumeColumnCount(range);
    return columnCount;
}

bool CSSPropertyParser::consumeColumns(bool important)
{
    RefPtr<CSSValue> columnWidth;
    RefPtr<CSSValue> columnCount;
    if (!consumeColumnWidthOrCount(m_range, columnWidth, columnCount))
        return false;
    consumeColumnWidthOrCount(m_range, columnWidth, columnCount);
    if (!m_range.atEnd())
        return false;
    if (!columnWidth)
        columnWidth = CSSValuePool::singleton().createIdentifierValue(CSSValueAuto);
    if (!columnCount)
        columnCount = CSSValuePool::singleton().createIdentifierValue(CSSValueAuto);
    addProperty(CSSPropertyColumnWidth, CSSPropertyInvalid, columnWidth.releaseNonNull(), important);
    addProperty(CSSPropertyColumnCount, CSSPropertyInvalid, columnCount.releaseNonNull(), important);
    return true;
}

bool CSSPropertyParser::consumeShorthandGreedily(const StylePropertyShorthand& shorthand, bool important)
{
    ASSERT(shorthand.length() <= 6); // Existing shorthands have at most 6 longhands.
    RefPtr<CSSValue> longhands[6];
    const CSSPropertyID* shorthandProperties = shorthand.properties();
    do {
        bool foundLonghand = false;
        for (size_t i = 0; !foundLonghand && i < shorthand.length(); ++i) {
            if (longhands[i])
                continue;
            longhands[i] = parseSingleValue(shorthandProperties[i], shorthand.id());
            if (longhands[i])
                foundLonghand = true;
        }
        if (!foundLonghand)
            return false;
    } while (!m_range.atEnd());

    for (size_t i = 0; i < shorthand.length(); ++i) {
        if (longhands[i])
            addProperty(shorthandProperties[i], shorthand.id(), longhands[i].releaseNonNull(), important);
        else
            addProperty(shorthandProperties[i], shorthand.id(), CSSValuePool::singleton().createImplicitInitialValue(), important);
    }
    return true;
}

bool CSSPropertyParser::consumeFlex(bool important)
{
    static const double unsetValue = -1;
    double flexGrow = unsetValue;
    double flexShrink = unsetValue;
    RefPtr<CSSPrimitiveValue> flexBasis;

    if (m_range.peek().id() == CSSValueNone) {
        flexGrow = 0;
        flexShrink = 0;
        flexBasis = CSSValuePool::singleton().createIdentifierValue(CSSValueAuto);
        m_range.consumeIncludingWhitespace();
    } else {
        unsigned index = 0;
        while (!m_range.atEnd() && index++ < 3) {
            double num;
            if (consumeNumberRaw(m_range, num)) {
                if (num < 0)
                    return false;
                if (flexGrow == unsetValue)
                    flexGrow = num;
                else if (flexShrink == unsetValue)
                    flexShrink = num;
                else if (!num) // flex only allows a basis of 0 (sans units) if flex-grow and flex-shrink values have already been set.
                    flexBasis = CSSPrimitiveValue::create(0, CSSPrimitiveValue::UnitTypes::CSS_PX);
                else
                    return false;
            } else if (!flexBasis) {
                if (m_range.peek().id() == CSSValueAuto)
                    flexBasis = consumeIdent(m_range);
                if (!flexBasis)
                    flexBasis = consumeLengthOrPercent(m_range, m_context.mode, ValueRangeNonNegative);
                if (index == 2 && !m_range.atEnd())
                    return false;
            }
        }
        if (index == 0)
            return false;
        if (flexGrow == unsetValue)
            flexGrow = 1;
        if (flexShrink == unsetValue)
            flexShrink = 1;
        if (!flexBasis)
            flexBasis = CSSPrimitiveValue::create(0, CSSPrimitiveValue::UnitTypes::CSS_PERCENTAGE);
    }

    if (!m_range.atEnd())
        return false;
    addProperty(CSSPropertyFlexGrow, CSSPropertyFlex, CSSPrimitiveValue::create(clampTo<float>(flexGrow), CSSPrimitiveValue::UnitTypes::CSS_NUMBER), important);
    addProperty(CSSPropertyFlexShrink, CSSPropertyFlex, CSSPrimitiveValue::create(clampTo<float>(flexShrink), CSSPrimitiveValue::UnitTypes::CSS_NUMBER), important);
    addProperty(CSSPropertyFlexBasis, CSSPropertyFlex, flexBasis.releaseNonNull(), important);
    return true;
}

bool CSSPropertyParser::consumeBorder(bool important)
{
    RefPtr<CSSValue> width;
    RefPtr<CSSValue> style;
    RefPtr<CSSValue> color;

    while (!width || !style || !color) {
        if (!width) {
            width = consumeLineWidth(m_range, m_context.mode, UnitlessQuirk::Forbid);
            if (width)
                continue;
        }
        if (!style) {
            style = parseSingleValue(CSSPropertyBorderLeftStyle, CSSPropertyBorder);
            if (style)
                continue;
        }
        if (!color) {
            color = consumeColor(m_range, m_context.mode);
            if (color)
                continue;
        }
        break;
    }

    if (!width && !style && !color)
        return false;

    if (!width)
        width = CSSValuePool::singleton().createImplicitInitialValue();
    if (!style)
        style = CSSValuePool::singleton().createImplicitInitialValue();
    if (!color)
        color = CSSValuePool::singleton().createImplicitInitialValue();

    addExpandedPropertyForValue(CSSPropertyBorderWidth, width.releaseNonNull(), important);
    addExpandedPropertyForValue(CSSPropertyBorderStyle, style.releaseNonNull(), important);
    addExpandedPropertyForValue(CSSPropertyBorderColor, color.releaseNonNull(), important);
    addExpandedPropertyForValue(CSSPropertyBorderImage, CSSValuePool::singleton().createImplicitInitialValue(), important);

    return m_range.atEnd();
}

bool CSSPropertyParser::consume4Values(const StylePropertyShorthand& shorthand, bool important)
{
    ASSERT(shorthand.length() == 4);
    const CSSPropertyID* longhands = shorthand.properties();
    RefPtr<CSSValue> top = parseSingleValue(longhands[0], shorthand.id());
    if (!top)
        return false;

    RefPtr<CSSValue> right = parseSingleValue(longhands[1], shorthand.id());
    RefPtr<CSSValue> bottom;
    RefPtr<CSSValue> left;
    if (right) {
        bottom = parseSingleValue(longhands[2], shorthand.id());
        if (bottom)
            left = parseSingleValue(longhands[3], shorthand.id());
    }

    if (!right)
        right = top;
    if (!bottom)
        bottom = top;
    if (!left)
        left = right;

    addProperty(longhands[0], shorthand.id(), top.releaseNonNull(), important);
    addProperty(longhands[1], shorthand.id(), right.releaseNonNull(), important);
    addProperty(longhands[2], shorthand.id(), bottom.releaseNonNull(), important);
    addProperty(longhands[3], shorthand.id(), left.releaseNonNull(), important);

    return m_range.atEnd();
}

bool CSSPropertyParser::consumeBorderImage(CSSPropertyID property, bool important)
{
    RefPtr<CSSValue> source;
    RefPtr<CSSValue> slice;
    RefPtr<CSSValue> width;
    RefPtr<CSSValue> outset;
    RefPtr<CSSValue> repeat;
    
    if (consumeBorderImageComponents(property, m_range, m_context, source, slice, width, outset, repeat)) {
        if (!source)
            source = CSSValuePool::singleton().createImplicitInitialValue();
        if (!slice)
            slice = CSSValuePool::singleton().createImplicitInitialValue();
        if (!width)
            width = CSSValuePool::singleton().createImplicitInitialValue();
        if (!outset)
            outset = CSSValuePool::singleton().createImplicitInitialValue();
        if (!repeat)
            repeat = CSSValuePool::singleton().createImplicitInitialValue();
        switch (property) {
        case CSSPropertyWebkitMaskBoxImage:
            addProperty(CSSPropertyWebkitMaskBoxImageSource, CSSPropertyWebkitMaskBoxImage, source.releaseNonNull(), important);
            addProperty(CSSPropertyWebkitMaskBoxImageSlice, CSSPropertyWebkitMaskBoxImage, slice.releaseNonNull(), important);
            addProperty(CSSPropertyWebkitMaskBoxImageWidth, CSSPropertyWebkitMaskBoxImage, width.releaseNonNull(), important);
            addProperty(CSSPropertyWebkitMaskBoxImageOutset, CSSPropertyWebkitMaskBoxImage, outset.releaseNonNull(), important);
            addProperty(CSSPropertyWebkitMaskBoxImageRepeat, CSSPropertyWebkitMaskBoxImage, repeat.releaseNonNull(), important);
            return true;
        case CSSPropertyBorderImage:
            addProperty(CSSPropertyBorderImageSource, CSSPropertyBorderImage, source.releaseNonNull(), important);
            addProperty(CSSPropertyBorderImageSlice, CSSPropertyBorderImage, slice.releaseNonNull(), important);
            addProperty(CSSPropertyBorderImageWidth, CSSPropertyBorderImage, width.releaseNonNull() , important);
            addProperty(CSSPropertyBorderImageOutset, CSSPropertyBorderImage, outset.releaseNonNull(), important);
            addProperty(CSSPropertyBorderImageRepeat, CSSPropertyBorderImage, repeat.releaseNonNull(), important);
            return true;
        default:
            ASSERT_NOT_REACHED();
            return false;
        }
    }
    return false;
}

static inline CSSValueID mapFromPageBreakBetween(CSSValueID value)
{
    if (value == CSSValueAlways)
        return CSSValuePage;
    if (value == CSSValueAuto || value == CSSValueLeft || value == CSSValueRight)
        return value;
    if (value == CSSValueAvoid)
        return CSSValueAvoidPage;
    return CSSValueInvalid;
}

static inline CSSValueID mapFromColumnBreakBetween(CSSValueID value)
{
    if (value == CSSValueAlways)
        return CSSValueColumn;
    if (value == CSSValueAuto)
        return value;
    if (value == CSSValueAvoid)
        return CSSValueAvoidColumn;
    return CSSValueInvalid;
}

#if ENABLE(CSS_REGIONS)
static inline CSSValueID mapFromRegionBreakBetween(CSSValueID value)
{
    if (value == CSSValueAlways)
        return CSSValueRegion;
    if (value == CSSValueAuto)
        return value;
    if (value == CSSValueAvoid)
        return CSSValueAvoidRegion;
    return CSSValueInvalid;
}
#endif

static inline CSSValueID mapFromColumnRegionOrPageBreakInside(CSSValueID value)
{
    if (value == CSSValueAuto || value == CSSValueAvoid)
        return value;
    return CSSValueInvalid;
}

static inline CSSPropertyID mapFromLegacyBreakProperty(CSSPropertyID property)
{
    if (property == CSSPropertyPageBreakAfter || property == CSSPropertyWebkitColumnBreakAfter)
        return CSSPropertyBreakAfter;
    if (property == CSSPropertyPageBreakBefore || property == CSSPropertyWebkitColumnBreakBefore)
        return CSSPropertyBreakBefore;
#if ENABLE(CSS_REGIONS)
    if (property == CSSPropertyWebkitRegionBreakAfter)
        return CSSPropertyBreakAfter;
    if (property == CSSPropertyWebkitRegionBreakBefore)
        return CSSPropertyBreakBefore;
    ASSERT(property == CSSPropertyPageBreakInside || property == CSSPropertyWebkitColumnBreakInside || property == CSSPropertyWebkitRegionBreakInside);
#else
    ASSERT(property == CSSPropertyPageBreakInside || property == CSSPropertyWebkitColumnBreakInside);
#endif
    return CSSPropertyBreakInside;
}

bool CSSPropertyParser::consumeLegacyBreakProperty(CSSPropertyID property, bool important)
{
    // The fragmentation spec says that page-break-(after|before|inside) are to be treated as
    // shorthands for their break-(after|before|inside) counterparts. We'll do the same for the
    // non-standard properties -webkit-column-break-(after|before|inside).
    RefPtr<CSSPrimitiveValue> keyword = consumeIdent(m_range);
    if (!keyword)
        return false;
    if (!m_range.atEnd())
        return false;
    CSSValueID value = keyword->valueID();
    switch (property) {
    case CSSPropertyPageBreakAfter:
    case CSSPropertyPageBreakBefore:
        value = mapFromPageBreakBetween(value);
        break;
    case CSSPropertyWebkitColumnBreakAfter:
    case CSSPropertyWebkitColumnBreakBefore:
        value = mapFromColumnBreakBetween(value);
        break;
#if ENABLE(CSS_REGIONS)
    case CSSPropertyWebkitRegionBreakAfter:
    case CSSPropertyWebkitRegionBreakBefore:
        value = mapFromRegionBreakBetween(value);
        break;
    case CSSPropertyWebkitRegionBreakInside:
#endif
    case CSSPropertyPageBreakInside:
    case CSSPropertyWebkitColumnBreakInside:
        value = mapFromColumnRegionOrPageBreakInside(value);
        break;
    default:
        ASSERT_NOT_REACHED();
    }
    if (value == CSSValueInvalid)
        return false;

    CSSPropertyID genericBreakProperty = mapFromLegacyBreakProperty(property);
    addProperty(genericBreakProperty, property, CSSValuePool::singleton().createIdentifierValue(value), important);
    return true;
}

static bool consumeBackgroundPosition(CSSParserTokenRange& range, const CSSParserContext& context, UnitlessQuirk unitless, RefPtr<CSSValue>& resultX, RefPtr<CSSValue>& resultY)
{
    do {
        RefPtr<CSSPrimitiveValue> positionX;
        RefPtr<CSSPrimitiveValue> positionY;
        if (!consumePosition(range, context.mode, unitless, positionX, positionY))
            return false;
        addBackgroundValue(resultX, positionX.releaseNonNull());
        addBackgroundValue(resultY, positionY.releaseNonNull());
    } while (consumeCommaIncludingWhitespace(range));
    return true;
}

static bool consumeRepeatStyleComponent(CSSParserTokenRange& range, RefPtr<CSSPrimitiveValue>& value1, RefPtr<CSSPrimitiveValue>& value2, bool& implicit)
{
    if (consumeIdent<CSSValueRepeatX>(range)) {
        value1 = CSSValuePool::singleton().createIdentifierValue(CSSValueRepeat);
        value2 = CSSValuePool::singleton().createIdentifierValue(CSSValueNoRepeat);
        implicit = true;
        return true;
    }
    if (consumeIdent<CSSValueRepeatY>(range)) {
        value1 = CSSValuePool::singleton().createIdentifierValue(CSSValueNoRepeat);
        value2 = CSSValuePool::singleton().createIdentifierValue(CSSValueRepeat);
        implicit = true;
        return true;
    }
    value1 = consumeIdent<CSSValueRepeat, CSSValueNoRepeat, CSSValueRound, CSSValueSpace>(range);
    if (!value1)
        return false;

    value2 = consumeIdent<CSSValueRepeat, CSSValueNoRepeat, CSSValueRound, CSSValueSpace>(range);
    if (!value2) {
        value2 = value1;
        implicit = true;
    }
    return true;
}

static bool consumeRepeatStyle(CSSParserTokenRange& range, RefPtr<CSSValue>& resultX, RefPtr<CSSValue>& resultY, bool& implicit)
{
    do {
        RefPtr<CSSPrimitiveValue> repeatX;
        RefPtr<CSSPrimitiveValue> repeatY;
        if (!consumeRepeatStyleComponent(range, repeatX, repeatY, implicit))
            return false;
        addBackgroundValue(resultX, repeatX.releaseNonNull());
        addBackgroundValue(resultY, repeatY.releaseNonNull());
    } while (consumeCommaIncludingWhitespace(range));
    return true;
}

// Note: consumeBackgroundShorthand assumes y properties (for example background-position-y) follow
// the x properties in the shorthand array.
bool CSSPropertyParser::consumeBackgroundShorthand(const StylePropertyShorthand& shorthand, bool important)
{
    const unsigned longhandCount = shorthand.length();
    RefPtr<CSSValue> longhands[10];
    ASSERT(longhandCount <= 10);

    bool implicit = false;
    do {
        bool parsedLonghand[10] = { false };
        RefPtr<CSSValue> originValue;
        do {
            bool foundProperty = false;
            for (size_t i = 0; i < longhandCount; ++i) {
                if (parsedLonghand[i])
                    continue;

                RefPtr<CSSValue> value;
                RefPtr<CSSValue> valueY;
                CSSPropertyID property = shorthand.properties()[i];
                if (property == CSSPropertyBackgroundRepeatX || property == CSSPropertyWebkitMaskRepeatX) {
                    RefPtr<CSSPrimitiveValue> primitiveValue;
                    RefPtr<CSSPrimitiveValue> primitiveValueY;
                    consumeRepeatStyleComponent(m_range, primitiveValue, primitiveValueY, implicit);
                    value = primitiveValue;
                    valueY = primitiveValueY;
                } else if (property == CSSPropertyBackgroundPositionX || property == CSSPropertyWebkitMaskPositionX) {
                    CSSParserTokenRange rangeCopy = m_range;
                    RefPtr<CSSPrimitiveValue> primitiveValue;
                    RefPtr<CSSPrimitiveValue> primitiveValueY;
                    if (!consumePosition(rangeCopy, m_context.mode, UnitlessQuirk::Forbid, primitiveValue, primitiveValueY))
                        continue;
                    value = primitiveValue;
                    valueY = primitiveValueY;
                    m_range = rangeCopy;
                } else if (property == CSSPropertyBackgroundSize || property == CSSPropertyWebkitMaskSize) {
                    if (!consumeSlashIncludingWhitespace(m_range))
                        continue;
                    value = consumeBackgroundSize(property, m_range, m_context.mode);
                    if (!value || !parsedLonghand[i - 1]) // Position must have been parsed in the current layer.
                        return false;
                } else if (property == CSSPropertyBackgroundPositionY || property == CSSPropertyBackgroundRepeatY
                    || property == CSSPropertyWebkitMaskPositionY || property == CSSPropertyWebkitMaskRepeatY) {
                    continue;
                } else {
                    value = consumeBackgroundComponent(property, m_range, m_context);
                }
                if (value) {
                    if (property == CSSPropertyBackgroundOrigin || property == CSSPropertyWebkitMaskOrigin)
                        originValue = value;
                    parsedLonghand[i] = true;
                    foundProperty = true;
                    addBackgroundValue(longhands[i], value.releaseNonNull());
                    if (valueY) {
                        parsedLonghand[i + 1] = true;
                        addBackgroundValue(longhands[i + 1], valueY.releaseNonNull());
                    }
                }
            }
            if (!foundProperty)
                return false;
        } while (!m_range.atEnd() && m_range.peek().type() != CommaToken);

        // FIXME: This will make invalid longhands, see crbug.com/386459
        for (size_t i = 0; i < longhandCount; ++i) {
            CSSPropertyID property = shorthand.properties()[i];
            if (property == CSSPropertyBackgroundColor && !m_range.atEnd()) {
                if (parsedLonghand[i])
                    return false; // Colors are only allowed in the last layer.
                continue;
            }
            if ((property == CSSPropertyBackgroundClip || property == CSSPropertyWebkitMaskClip) && !parsedLonghand[i] && originValue) {
                addBackgroundValue(longhands[i], originValue.releaseNonNull());
                continue;
            }
            if (!parsedLonghand[i])
                addBackgroundValue(longhands[i], CSSValuePool::singleton().createImplicitInitialValue());
        }
    } while (consumeCommaIncludingWhitespace(m_range));
    if (!m_range.atEnd())
        return false;

    for (size_t i = 0; i < longhandCount; ++i) {
        CSSPropertyID property = shorthand.properties()[i];
        if (property == CSSPropertyBackgroundSize && longhands[i] && m_context.useLegacyBackgroundSizeShorthandBehavior)
            continue;
        addProperty(property, shorthand.id(), *longhands[i], important, implicit);
    }
    return true;
}

// FIXME-NEWPARSER: Hack to work around the fact that we aren't using CSSCustomIdentValue
// for stuff yet. This can be replaced by CSSValue::isCustomIdentValue() once we switch
// to using CSSCustomIdentValue everywhere.
static bool isCustomIdentValue(const CSSValue& value)
{
    return is<CSSPrimitiveValue>(value) && downcast<CSSPrimitiveValue>(value).isString();
}

#if ENABLE(CSS_GRID_LAYOUT)
bool CSSPropertyParser::consumeGridItemPositionShorthand(CSSPropertyID shorthandId, bool important)
{
    const StylePropertyShorthand& shorthand = shorthandForProperty(shorthandId);
    ASSERT(shorthand.length() == 2);
    RefPtr<CSSValue> startValue = consumeGridLine(m_range);
    if (!startValue)
        return false;

    RefPtr<CSSValue> endValue;
    if (consumeSlashIncludingWhitespace(m_range)) {
        endValue = consumeGridLine(m_range);
        if (!endValue)
            return false;
    } else {
        endValue = isCustomIdentValue(*startValue) ? startValue : CSSValuePool::singleton().createIdentifierValue(CSSValueAuto);
    }
    if (!m_range.atEnd())
        return false;
    addProperty(shorthand.properties()[0], shorthandId, startValue.releaseNonNull(), important);
    addProperty(shorthand.properties()[1], shorthandId, endValue.releaseNonNull(), important);
    return true;
}

bool CSSPropertyParser::consumeGridAreaShorthand(bool important)
{
    RefPtr<CSSValue> rowStartValue = consumeGridLine(m_range);
    if (!rowStartValue)
        return false;
    RefPtr<CSSValue> columnStartValue;
    RefPtr<CSSValue> rowEndValue;
    RefPtr<CSSValue> columnEndValue;
    if (consumeSlashIncludingWhitespace(m_range)) {
        columnStartValue = consumeGridLine(m_range);
        if (!columnStartValue)
            return false;
        if (consumeSlashIncludingWhitespace(m_range)) {
            rowEndValue = consumeGridLine(m_range);
            if (!rowEndValue)
                return false;
            if (consumeSlashIncludingWhitespace(m_range)) {
                columnEndValue = consumeGridLine(m_range);
                if (!columnEndValue)
                    return false;
            }
        }
    }
    if (!m_range.atEnd())
        return false;
    if (!columnStartValue)
        columnStartValue = isCustomIdentValue(*rowStartValue) ? rowStartValue : CSSValuePool::singleton().createIdentifierValue(CSSValueAuto);
    if (!rowEndValue)
        rowEndValue = isCustomIdentValue(*rowStartValue) ? rowStartValue : CSSValuePool::singleton().createIdentifierValue(CSSValueAuto);
    if (!columnEndValue)
        columnEndValue = isCustomIdentValue(*columnStartValue) ? columnStartValue : CSSValuePool::singleton().createIdentifierValue(CSSValueAuto);

    addProperty(CSSPropertyGridRowStart, CSSPropertyGridArea, rowStartValue.releaseNonNull(), important);
    addProperty(CSSPropertyGridColumnStart, CSSPropertyGridArea, columnStartValue.releaseNonNull(), important);
    addProperty(CSSPropertyGridRowEnd, CSSPropertyGridArea, rowEndValue.releaseNonNull(), important);
    addProperty(CSSPropertyGridColumnEnd, CSSPropertyGridArea, columnEndValue.releaseNonNull(), important);
    return true;
}

bool CSSPropertyParser::consumeGridTemplateRowsAndAreasAndColumns(CSSPropertyID shorthandId, bool important)
{
    NamedGridAreaMap gridAreaMap;
    size_t rowCount = 0;
    size_t columnCount = 0;
    RefPtr<CSSValueList> templateRows = CSSValueList::createSpaceSeparated();

    // Persists between loop iterations so we can use the same value for
    // consecutive <line-names> values
    RefPtr<CSSGridLineNamesValue> lineNames;

    do {
        // Handle leading <custom-ident>*.
        bool hasPreviousLineNames = lineNames;
        lineNames = consumeGridLineNames(m_range, lineNames.get());
        if (lineNames && !hasPreviousLineNames)
            templateRows->append(*lineNames);

        // Handle a template-area's row.
        if (m_range.peek().type() != StringToken || !parseGridTemplateAreasRow(m_range.consumeIncludingWhitespace().value().toString(), gridAreaMap, rowCount, columnCount))
            return false;
        ++rowCount;

        // Handle template-rows's track-size.
        RefPtr<CSSValue> value = consumeGridTrackSize(m_range, m_context.mode);
        if (!value)
            value = CSSValuePool::singleton().createIdentifierValue(CSSValueAuto);
        templateRows->append(*value);

        // This will handle the trailing/leading <custom-ident>* in the grammar.
        lineNames = consumeGridLineNames(m_range);
        if (lineNames)
            templateRows->append(lineNames.releaseNonNull());
    } while (!m_range.atEnd() && !(m_range.peek().type() == DelimiterToken && m_range.peek().delimiter() == '/'));

    RefPtr<CSSValue> columnsValue;
    if (!m_range.atEnd()) {
        if (!consumeSlashIncludingWhitespace(m_range))
            return false;
        columnsValue = consumeGridTrackList(m_range, m_context.mode, GridTemplateNoRepeat);
        if (!columnsValue || !m_range.atEnd())
            return false;
    } else {
        columnsValue = CSSValuePool::singleton().createIdentifierValue(CSSValueNone);
    }
    addProperty(CSSPropertyGridTemplateRows, shorthandId, templateRows.releaseNonNull(), important);
    addProperty(CSSPropertyGridTemplateColumns, shorthandId, columnsValue.releaseNonNull(), important);
    addProperty(CSSPropertyGridTemplateAreas, shorthandId, CSSGridTemplateAreasValue::create(gridAreaMap, rowCount, columnCount), important);
    return true;
}

bool CSSPropertyParser::consumeGridTemplateShorthand(CSSPropertyID shorthandId, bool important)
{
    CSSParserTokenRange rangeCopy = m_range;
    RefPtr<CSSValue> rowsValue = consumeIdent<CSSValueNone>(m_range);

    // 1- 'none' case.
    if (rowsValue && m_range.atEnd()) {
        addProperty(CSSPropertyGridTemplateRows, shorthandId, CSSValuePool::singleton().createIdentifierValue(CSSValueNone), important);
        addProperty(CSSPropertyGridTemplateColumns, shorthandId, CSSValuePool::singleton().createIdentifierValue(CSSValueNone), important);
        addProperty(CSSPropertyGridTemplateAreas, shorthandId, CSSValuePool::singleton().createIdentifierValue(CSSValueNone), important);
        return true;
    }

    // 2- <grid-template-rows> / <grid-template-columns>
    if (!rowsValue)
        rowsValue = consumeGridTrackList(m_range, m_context.mode, GridTemplate);

    if (rowsValue) {
        if (!consumeSlashIncludingWhitespace(m_range))
            return false;
        RefPtr<CSSValue> columnsValue = consumeGridTemplatesRowsOrColumns(m_range, m_context.mode);
        if (!columnsValue || !m_range.atEnd())
            return false;

        addProperty(CSSPropertyGridTemplateRows, shorthandId, rowsValue.releaseNonNull(), important);
        addProperty(CSSPropertyGridTemplateColumns, shorthandId, columnsValue.releaseNonNull(), important);
        addProperty(CSSPropertyGridTemplateAreas, shorthandId, CSSValuePool::singleton().createIdentifierValue(CSSValueNone), important);
        return true;
    }

    // 3- [ <line-names>? <string> <track-size>? <line-names>? ]+ [ / <track-list> ]?
    m_range = rangeCopy;
    return consumeGridTemplateRowsAndAreasAndColumns(shorthandId, important);
}

static RefPtr<CSSValue> consumeImplicitGridAutoFlow(CSSParserTokenRange& range, Ref<CSSPrimitiveValue>&& flowDirection)
{
    // [ auto-flow && dense? ]
    if (range.atEnd())
        return nullptr;
    auto list = CSSValueList::createSpaceSeparated();
    list->append(WTFMove(flowDirection));
    if (range.peek().id() == CSSValueAutoFlow) {
        range.consumeIncludingWhitespace();
        RefPtr<CSSValue> denseIdent = consumeIdent<CSSValueDense>(range);
        if (denseIdent)
            list->append(denseIdent.releaseNonNull());
    } else {
        // Dense case
        if (range.peek().id() != CSSValueDense)
            return nullptr;
        range.consumeIncludingWhitespace();
        if (range.atEnd() || range.peek().id() != CSSValueAutoFlow)
            return nullptr;
        range.consumeIncludingWhitespace();
        list->append(CSSValuePool::singleton().createIdentifierValue(CSSValueDense));
    }
    
    return WTFMove(list);
}

bool CSSPropertyParser::consumeGridShorthand(bool important)
{
    ASSERT(shorthandForProperty(CSSPropertyGrid).length() == 8);

    CSSParserTokenRange rangeCopy = m_range;

    // 1- <grid-template>
    if (consumeGridTemplateShorthand(CSSPropertyGrid, important)) {
        // It can only be specified the explicit or the implicit grid properties in a single grid declaration.
        // The sub-properties not specified are set to their initial value, as normal for shorthands.
        addProperty(CSSPropertyGridAutoFlow, CSSPropertyGrid, CSSValuePool::singleton().createImplicitInitialValue(), important);
        addProperty(CSSPropertyGridAutoColumns, CSSPropertyGrid, CSSValuePool::singleton().createImplicitInitialValue(), important);
        addProperty(CSSPropertyGridAutoRows, CSSPropertyGrid, CSSValuePool::singleton().createImplicitInitialValue(), important);
        addProperty(CSSPropertyGridColumnGap, CSSPropertyGrid, CSSValuePool::singleton().createImplicitInitialValue(), important);
        addProperty(CSSPropertyGridRowGap, CSSPropertyGrid, CSSValuePool::singleton().createImplicitInitialValue(), important);
        return true;
    }

    m_range = rangeCopy;

    RefPtr<CSSValue> autoColumnsValue;
    RefPtr<CSSValue> autoRowsValue;
    RefPtr<CSSValue> templateRows;
    RefPtr<CSSValue> templateColumns;
    RefPtr<CSSValue> gridAutoFlow;
    
    if (m_range.peek().id() == CSSValueAutoFlow || m_range.peek().id() == CSSValueDense) {
        // 2- [ auto-flow && dense? ] <grid-auto-rows>? / <grid-template-columns>
        gridAutoFlow = consumeImplicitGridAutoFlow(m_range, CSSValuePool::singleton().createIdentifierValue(CSSValueRow));
        if (!gridAutoFlow || m_range.atEnd())
            return false;
        if (consumeSlashIncludingWhitespace(m_range))
            autoRowsValue = CSSValuePool::singleton().createImplicitInitialValue();
        else {
            autoRowsValue = consumeGridTrackList(m_range, m_context.mode, GridAuto);
            if (!autoRowsValue)
                return false;
            if (!consumeSlashIncludingWhitespace(m_range))
                return false;
        }
        if (m_range.atEnd())
            return false;
        templateColumns = consumeGridTemplatesRowsOrColumns(m_range, m_context.mode);
        if (!templateColumns)
            return false;
        templateRows = CSSValuePool::singleton().createImplicitInitialValue();
        autoColumnsValue = CSSValuePool::singleton().createImplicitInitialValue();
    } else {
        // 3- <grid-template-rows> / [ auto-flow && dense? ] <grid-auto-columns>?
        templateRows = consumeGridTemplatesRowsOrColumns(m_range, m_context.mode);
        if (!templateRows)
            return false;
        if (!consumeSlashIncludingWhitespace(m_range) || m_range.atEnd())
            return false;
        gridAutoFlow = consumeImplicitGridAutoFlow(m_range, CSSValuePool::singleton().createIdentifierValue(CSSValueColumn));
        if (!gridAutoFlow)
            return false;
        if (m_range.atEnd())
            autoColumnsValue = CSSValuePool::singleton().createImplicitInitialValue();
        else {
            autoColumnsValue = consumeGridTrackList(m_range, m_context.mode, GridAuto);
            if (!autoColumnsValue)
                return false;
        }
        templateColumns = CSSValuePool::singleton().createImplicitInitialValue();
        autoRowsValue = CSSValuePool::singleton().createImplicitInitialValue();
    }
    
    if (!m_range.atEnd())
        return false;
    
    // It can only be specified the explicit or the implicit grid properties in a single grid declaration.
    // The sub-properties not specified are set to their initial value, as normal for shorthands.
    addProperty(CSSPropertyGridTemplateColumns, CSSPropertyGrid, templateColumns.releaseNonNull(), important);
    addProperty(CSSPropertyGridTemplateRows, CSSPropertyGrid, templateRows.releaseNonNull(), important);
    addProperty(CSSPropertyGridTemplateAreas, CSSPropertyGrid, CSSValuePool::singleton().createImplicitInitialValue(), important);
    addProperty(CSSPropertyGridAutoFlow, CSSPropertyGrid, gridAutoFlow.releaseNonNull(), important);
    addProperty(CSSPropertyGridAutoColumns, CSSPropertyGrid, autoColumnsValue.releaseNonNull(), important);
    addProperty(CSSPropertyGridAutoRows, CSSPropertyGrid, autoRowsValue.releaseNonNull(), important);
    addProperty(CSSPropertyGridColumnGap, CSSPropertyGrid, CSSValuePool::singleton().createImplicitInitialValue(), important);
    addProperty(CSSPropertyGridRowGap, CSSPropertyGrid, CSSValuePool::singleton().createImplicitInitialValue(), important);
    
    return true;
}
#endif

bool CSSPropertyParser::parseShorthand(CSSPropertyID property, bool important)
{
    switch (property) {
    case CSSPropertyWebkitMarginCollapse: {
        CSSValueID id = m_range.consumeIncludingWhitespace().id();
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyWebkitMarginBeforeCollapse, id, m_context.mode))
            return false;
        addProperty(CSSPropertyWebkitMarginBeforeCollapse, CSSPropertyWebkitMarginCollapse, CSSValuePool::singleton().createIdentifierValue(id), important);
        if (m_range.atEnd()) {
            addProperty(CSSPropertyWebkitMarginAfterCollapse, CSSPropertyWebkitMarginCollapse, CSSValuePool::singleton().createIdentifierValue(id), important);
            return true;
        }
        id = m_range.consumeIncludingWhitespace().id();
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyWebkitMarginAfterCollapse, id, m_context.mode))
            return false;
        addProperty(CSSPropertyWebkitMarginAfterCollapse, CSSPropertyWebkitMarginCollapse, CSSValuePool::singleton().createIdentifierValue(id), important);
        return true;
    }
    case CSSPropertyOverflow: {
        CSSValueID id = m_range.consumeIncludingWhitespace().id();
        if (!CSSParserFastPaths::isValidKeywordPropertyAndValue(CSSPropertyOverflowY, id, m_context.mode))
            return false;
        if (!m_range.atEnd())
            return false;
        RefPtr<CSSValue> overflowYValue = CSSValuePool::singleton().createIdentifierValue(id);
        RefPtr<CSSValue> overflowXValue;

        // FIXME: -webkit-paged-x or -webkit-paged-y only apply to overflow-y. If this value has been
        // set using the shorthand, then for now overflow-x will default to auto, but once we implement
        // pagination controls, it should default to hidden. If the overflow-y value is anything but
        // paged-x or paged-y, then overflow-x and overflow-y should have the same value.
        if (id == CSSValueWebkitPagedX || id == CSSValueWebkitPagedY)
            overflowXValue = CSSValuePool::singleton().createIdentifierValue(CSSValueAuto);
        else
            overflowXValue = overflowYValue;
        addProperty(CSSPropertyOverflowX, CSSPropertyOverflow, *overflowXValue, important);
        addProperty(CSSPropertyOverflowY, CSSPropertyOverflow, *overflowYValue, important);
        return true;
    }
    case CSSPropertyFont: {
        const CSSParserToken& token = m_range.peek();
        if (token.id() >= CSSValueCaption && token.id() <= CSSValueStatusBar)
            return consumeSystemFont(important);
        return consumeFont(important);
    }
    case CSSPropertyFontVariant:
        return consumeFontVariantShorthand(important);
    case CSSPropertyBorderSpacing:
        return consumeBorderSpacing(important);
    case CSSPropertyColumns:
        return consumeColumns(important);
    case CSSPropertyAnimation:
        return consumeAnimationShorthand(animationShorthandForParsing(), important);
    case CSSPropertyTransition:
        return consumeAnimationShorthand(transitionShorthandForParsing(), important);
    case CSSPropertyTextDecoration:
        return consumeShorthandGreedily(webkitTextDecorationShorthand(), important);
    case CSSPropertyMargin:
        return consume4Values(marginShorthand(), important);
    case CSSPropertyPadding:
        return consume4Values(paddingShorthand(), important);
    case CSSPropertyWebkitTextEmphasis:
        return consumeShorthandGreedily(webkitTextEmphasisShorthand(), important);
    case CSSPropertyOutline:
        return consumeShorthandGreedily(outlineShorthand(), important);
    case CSSPropertyWebkitBorderStart:
        return consumeShorthandGreedily(webkitBorderStartShorthand(), important);
    case CSSPropertyWebkitBorderEnd:
        return consumeShorthandGreedily(webkitBorderEndShorthand(), important);
    case CSSPropertyWebkitBorderBefore:
        return consumeShorthandGreedily(webkitBorderBeforeShorthand(), important);
    case CSSPropertyWebkitBorderAfter:
        return consumeShorthandGreedily(webkitBorderAfterShorthand(), important);
    case CSSPropertyWebkitTextStroke:
        return consumeShorthandGreedily(webkitTextStrokeShorthand(), important);
    case CSSPropertyMarker: {
        RefPtr<CSSValue> marker = parseSingleValue(CSSPropertyMarkerStart);
        if (!marker || !m_range.atEnd())
            return false;
        auto markerRef = marker.releaseNonNull();
        addProperty(CSSPropertyMarkerStart, CSSPropertyMarker, markerRef.copyRef(), important);
        addProperty(CSSPropertyMarkerMid, CSSPropertyMarker, markerRef.copyRef(), important);
        addProperty(CSSPropertyMarkerEnd, CSSPropertyMarker, markerRef.copyRef(), important);
        return true;
    }
    case CSSPropertyFlex:
        return consumeFlex(important);
    case CSSPropertyFlexFlow:
        return consumeShorthandGreedily(flexFlowShorthand(), important);
    case CSSPropertyColumnRule:
        return consumeShorthandGreedily(columnRuleShorthand(), important);
    case CSSPropertyListStyle:
        return consumeShorthandGreedily(listStyleShorthand(), important);
    case CSSPropertyBorderRadius: {
        RefPtr<CSSPrimitiveValue> horizontalRadii[4];
        RefPtr<CSSPrimitiveValue> verticalRadii[4];
        if (!consumeRadii(horizontalRadii, verticalRadii, m_range, m_context.mode, property == CSSPropertyWebkitBorderRadius))
            return false;
        addProperty(CSSPropertyBorderTopLeftRadius, CSSPropertyBorderRadius, createPrimitiveValuePair(horizontalRadii[0].releaseNonNull(), verticalRadii[0].releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce), important);
        addProperty(CSSPropertyBorderTopRightRadius, CSSPropertyBorderRadius, createPrimitiveValuePair(horizontalRadii[1].releaseNonNull(), verticalRadii[1].releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce), important);
        addProperty(CSSPropertyBorderBottomRightRadius, CSSPropertyBorderRadius, createPrimitiveValuePair(horizontalRadii[2].releaseNonNull(), verticalRadii[2].releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce), important);
        addProperty(CSSPropertyBorderBottomLeftRadius, CSSPropertyBorderRadius, createPrimitiveValuePair(horizontalRadii[3].releaseNonNull(), verticalRadii[3].releaseNonNull(), Pair::IdenticalValueEncoding::Coalesce), important);
        return true;
    }
    case CSSPropertyBorderColor:
        return consume4Values(borderColorShorthand(), important);
    case CSSPropertyBorderStyle:
        return consume4Values(borderStyleShorthand(), important);
    case CSSPropertyBorderWidth:
        return consume4Values(borderWidthShorthand(), important);
    case CSSPropertyBorderTop:
        return consumeShorthandGreedily(borderTopShorthand(), important);
    case CSSPropertyBorderRight:
        return consumeShorthandGreedily(borderRightShorthand(), important);
    case CSSPropertyBorderBottom:
        return consumeShorthandGreedily(borderBottomShorthand(), important);
    case CSSPropertyBorderLeft:
        return consumeShorthandGreedily(borderLeftShorthand(), important);
    case CSSPropertyBorder:
        return consumeBorder(important);
    case CSSPropertyBorderImage:
        return consumeBorderImage(property, important);
    case CSSPropertyWebkitMaskPosition:
    case CSSPropertyBackgroundPosition: {
        RefPtr<CSSValue> resultX;
        RefPtr<CSSValue> resultY;
        if (!consumeBackgroundPosition(m_range, m_context, UnitlessQuirk::Allow, resultX, resultY) || !m_range.atEnd())
            return false;
        addProperty(property == CSSPropertyBackgroundPosition ? CSSPropertyBackgroundPositionX : CSSPropertyWebkitMaskPositionX, property, resultX.releaseNonNull(), important);
        addProperty(property == CSSPropertyBackgroundPosition ? CSSPropertyBackgroundPositionY : CSSPropertyWebkitMaskPositionY, property, resultY.releaseNonNull(), important);
        return true;
    }
    case CSSPropertyBackgroundRepeat:
    case CSSPropertyWebkitMaskRepeat: {
        RefPtr<CSSValue> resultX;
        RefPtr<CSSValue> resultY;
        bool implicit = false;
        if (!consumeRepeatStyle(m_range, resultX, resultY, implicit) || !m_range.atEnd())
            return false;
        addProperty(property == CSSPropertyBackgroundRepeat ? CSSPropertyBackgroundRepeatX : CSSPropertyWebkitMaskRepeatX, property, resultX.releaseNonNull(), important, implicit);
        addProperty(property == CSSPropertyBackgroundRepeat ? CSSPropertyBackgroundRepeatY : CSSPropertyWebkitMaskRepeatY, property, resultY.releaseNonNull(), important, implicit);
        return true;
    }
    case CSSPropertyBackground:
        return consumeBackgroundShorthand(backgroundShorthand(), important);
    case CSSPropertyWebkitMask:
        return consumeBackgroundShorthand(webkitMaskShorthand(), important);
    case CSSPropertyTransformOrigin:
        return consumeTransformOrigin(important);
    case CSSPropertyPerspectiveOrigin:
        return consumePerspectiveOrigin(important);
#if ENABLE(CSS_GRID_LAYOUT)
    case CSSPropertyGridGap: {
        RefPtr<CSSValue> rowGap = consumeLength(m_range, m_context.mode, ValueRangeNonNegative);
        RefPtr<CSSValue> columnGap = consumeLength(m_range, m_context.mode, ValueRangeNonNegative);
        if (!rowGap || !m_range.atEnd())
            return false;
        if (!columnGap)
            columnGap = rowGap;
        addProperty(CSSPropertyGridRowGap, CSSPropertyGridGap, rowGap.releaseNonNull(), important);
        addProperty(CSSPropertyGridColumnGap, CSSPropertyGridGap, columnGap.releaseNonNull(), important);
        return true;
    }
    case CSSPropertyGridColumn:
    case CSSPropertyGridRow:
        return consumeGridItemPositionShorthand(property, important);
    case CSSPropertyGridArea:
        return consumeGridAreaShorthand(important);
    case CSSPropertyGridTemplate:
        return consumeGridTemplateShorthand(CSSPropertyGridTemplate, important);
    case CSSPropertyGrid:
        return consumeGridShorthand(important);
#endif
    case CSSPropertyWebkitMarquee:
        return consumeShorthandGreedily(webkitMarqueeShorthand(), important);
    default:
        return false;
    }
}

} // namespace WebCore
