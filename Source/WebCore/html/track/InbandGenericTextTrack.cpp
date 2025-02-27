/*
 * Copyright (C) 2012-2014 Apple Inc.  All rights reserved.
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

#include "config.h"
#include "InbandGenericTextTrack.h"

#if ENABLE(VIDEO_TRACK)

#include "HTMLMediaElement.h"
#include "InbandTextTrackPrivate.h"
#include "Logging.h"
#include "VTTRegionList.h"
#include <math.h>
#include <wtf/text/CString.h>

namespace WebCore {

void GenericTextTrackCueMap::add(GenericCueData& cueData, TextTrackCueGeneric& cue)
{
    m_dataToCueMap.add(&cueData, &cue);
    m_cueToDataMap.add(&cue, &cueData);
}

TextTrackCueGeneric* GenericTextTrackCueMap::find(GenericCueData& cueData)
{
    return m_dataToCueMap.get(&cueData);
}

GenericCueData* GenericTextTrackCueMap::find(TextTrackCue& cue)
{
    return m_cueToDataMap.get(&cue);
}

void GenericTextTrackCueMap::remove(GenericCueData& cueData)
{
    if (auto cue = m_dataToCueMap.take(&cueData))
        m_cueToDataMap.remove(cue);
}

void GenericTextTrackCueMap::remove(TextTrackCue& cue)
{
    if (auto data = m_cueToDataMap.take(&cue))
        m_dataToCueMap.remove(data);
}

Ref<InbandGenericTextTrack> InbandGenericTextTrack::create(ScriptExecutionContext* context, TextTrackClient* client, PassRefPtr<InbandTextTrackPrivate> playerPrivate)
{
    return adoptRef(*new InbandGenericTextTrack(context, client, playerPrivate));
}

InbandGenericTextTrack::InbandGenericTextTrack(ScriptExecutionContext* context, TextTrackClient* client, PassRefPtr<InbandTextTrackPrivate> trackPrivate)
    : InbandTextTrack(context, client, trackPrivate)
{
}

InbandGenericTextTrack::~InbandGenericTextTrack()
{
}

void InbandGenericTextTrack::updateCueFromCueData(TextTrackCueGeneric* cue, GenericCueData* cueData)
{
    cue->willChange();

    cue->setStartTime(cueData->startTime());
    MediaTime endTime = cueData->endTime();
    if (endTime.isPositiveInfinite() && mediaElement())
        endTime = mediaElement()->durationMediaTime();
    cue->setEndTime(endTime);
    cue->setText(cueData->content());
    cue->setId(cueData->id());
    cue->setBaseFontSizeRelativeToVideoHeight(cueData->baseFontSize());
    cue->setFontSizeMultiplier(cueData->relativeFontSize());
    cue->setFontName(cueData->fontName());

    if (cueData->position() > 0)
        cue->setPosition(std::round(cueData->position()));
    if (cueData->line() > 0)
        cue->setLine(std::round(cueData->line()));
    if (cueData->size() > 0)
        cue->setSize(std::round(cueData->size()));
    if (cueData->backgroundColor().isValid())
        cue->setBackgroundColor(cueData->backgroundColor().rgb());
    if (cueData->foregroundColor().isValid())
        cue->setForegroundColor(cueData->foregroundColor().rgb());
    if (cueData->highlightColor().isValid())
        cue->setHighlightColor(cueData->highlightColor().rgb());

    if (cueData->align() == GenericCueData::Start)
        cue->setAlign(ASCIILiteral("start"));
    else if (cueData->align() == GenericCueData::Middle)
        cue->setAlign(ASCIILiteral("middle"));
    else if (cueData->align() == GenericCueData::End)
        cue->setAlign(ASCIILiteral("end"));
    cue->setSnapToLines(false);

    cue->didChange();
}

void InbandGenericTextTrack::addGenericCue(InbandTextTrackPrivate* trackPrivate, PassRefPtr<GenericCueData> prpCueData)
{
    ASSERT_UNUSED(trackPrivate, trackPrivate == m_private);

    RefPtr<GenericCueData> cueData = prpCueData;
    if (m_cueMap.find(*cueData))
        return;

    auto cue = TextTrackCueGeneric::create(*scriptExecutionContext(), cueData->startTime(), cueData->endTime(), cueData->content());
    updateCueFromCueData(cue.ptr(), cueData.get());
    if (hasCue(cue.ptr(), TextTrackCue::IgnoreDuration)) {
        LOG(Media, "InbandGenericTextTrack::addGenericCue ignoring already added cue: start=%s, end=%s, content=\"%s\"\n", toString(cueData->startTime()).utf8().data(), toString(cueData->endTime()).utf8().data(), cueData->content().utf8().data());
        return;
    }

    LOG(Media, "InbandGenericTextTrack::addGenericCue added cue: start=%.2f, end=%.2f, content=\"%s\"\n", cueData->startTime().toDouble(), cueData->endTime().toDouble(), cueData->content().utf8().data());

    if (cueData->status() != GenericCueData::Complete)
        m_cueMap.add(*cueData, cue);

    addCue(WTFMove(cue));
}

void InbandGenericTextTrack::updateGenericCue(InbandTextTrackPrivate*, GenericCueData* cueData)
{
    auto* cue = m_cueMap.find(*cueData);
    if (!cue)
        return;

    updateCueFromCueData(cue, cueData);

    if (cueData->status() == GenericCueData::Complete)
        m_cueMap.remove(*cueData);
}

void InbandGenericTextTrack::removeGenericCue(InbandTextTrackPrivate*, GenericCueData* cueData)
{
    auto* cue = m_cueMap.find(*cueData);
    if (cue) {
        LOG(Media, "InbandGenericTextTrack::removeGenericCue removing cue: start=%s, end=%s, content=\"%s\"\n",  toString(cueData->startTime()).utf8().data(), toString(cueData->endTime()).utf8().data(), cueData->content().utf8().data());
        removeCue(*cue);
    } else {
        LOG(Media, "InbandGenericTextTrack::removeGenericCue UNABLE to find cue: start=%.2f, end=%.2f, content=\"%s\"\n", cueData->startTime().toDouble(), cueData->endTime().toDouble(), cueData->content().utf8().data());
    }
}

ExceptionOr<void> InbandGenericTextTrack::removeCue(TextTrackCue& cue)
{
    auto result = TextTrack::removeCue(cue);
    if (!result.hasException())
        m_cueMap.remove(cue);
    return result;
}

WebVTTParser& InbandGenericTextTrack::parser()
{
    if (!m_webVTTParser)
        m_webVTTParser = std::make_unique<WebVTTParser>(static_cast<WebVTTParserClient*>(this), scriptExecutionContext());
    return *m_webVTTParser;
}

void InbandGenericTextTrack::parseWebVTTCueData(InbandTextTrackPrivate* trackPrivate, const ISOWebVTTCue& cueData)
{
    ASSERT_UNUSED(trackPrivate, trackPrivate == m_private);
    parser().parseCueData(cueData);
}

void InbandGenericTextTrack::parseWebVTTFileHeader(InbandTextTrackPrivate* trackPrivate, String header)
{
    ASSERT_UNUSED(trackPrivate, trackPrivate == m_private);
    parser().parseFileHeader(header);
}

void InbandGenericTextTrack::newCuesParsed()
{
    Vector<RefPtr<WebVTTCueData>> cues;
    parser().getNewCues(cues);

    for (auto& cueData : cues) {
        auto vttCue = VTTCue::create(*scriptExecutionContext(), *cueData);

        if (hasCue(vttCue.ptr(), TextTrackCue::IgnoreDuration)) {
            LOG(Media, "InbandGenericTextTrack::newCuesParsed ignoring already added cue: start=%.2f, end=%.2f, content=\"%s\"\n", vttCue->startTime(), vttCue->endTime(), vttCue->text().utf8().data());
            return;
        }
        addCue(WTFMove(vttCue));
    }
}

void InbandGenericTextTrack::newRegionsParsed()
{
    Vector<RefPtr<VTTRegion>> newRegions;
    parser().getNewRegions(newRegions);

    for (auto& region : newRegions) {
        region->setTrack(this);
        regions()->add(region);
    }
}

void InbandGenericTextTrack::fileFailedToParse()
{
    LOG(Media, "Error parsing WebVTT stream.");
}

} // namespace WebCore

#endif
