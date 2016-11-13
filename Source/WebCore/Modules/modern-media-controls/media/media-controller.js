/*
 * Copyright (C) 2016 Apple Inc. All Rights Reserved.
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

class MediaController
{

    constructor(shadowRoot, media, host)
    {
        this.shadowRoot = shadowRoot;
        this.media = media;
        this.host = host;

        this._updateControlsIfNeeded();

        media.addEventListener("resize", this);

        media.addEventListener("webkitfullscreenchange", this);
    }

    get layoutTraits()
    {
        let traits = window.navigator.platform === "MacIntel" ? LayoutTraits.macOS : LayoutTraits.iOS;
        if (this.media.webkitDisplayingFullscreen)
            traits = traits | LayoutTraits.Fullscreen;
        return traits;
    }

    // Protected

    set pageScaleFactor(pageScaleFactor)
    {
        // FIXME: To be implemented.
    }

    set usesLTRUserInterfaceLayoutDirection(flag)
    {
        // FIXME: To be implemented.
    }

    handleEvent(event)
    {
        if (event.type === "resize" && event.currentTarget === this.media)
            this._updateControlsSize();
        else if (event.type === "webkitfullscreenchange" && event.currentTarget === this.media)
            this._updateControlsIfNeeded();
    }

    // Private

    _updateControlsIfNeeded()
    {
        const previousControls = this.controls;
        const ControlsClass = this._controlsClass();
        if (previousControls && previousControls.constructor === ControlsClass)
            return;

        // Before we reset the .controls property, we need to destroy the previous
        // supporting objects so we don't leak.
        if (this._supportingObjects) {
            for (let supportingObject of this._supportingObjects)
                supportingObject.destroy();
        }

        this.controls = new ControlsClass;

        if (previousControls)
            this.shadowRoot.replaceChild(this.controls.element, previousControls.element);
        else
            this.shadowRoot.appendChild(this.controls.element);        

        this._updateControlsSize();

        this._supportingObjects = [AirplaySupport, ElapsedTimeSupport, FullscreenSupport, MuteSupport, PiPSupport, PlacardSupport, PlaybackSupport, RemainingTimeSupport, ScrubbingSupport, SkipBackSupport, StartSupport, StatusSupport, TracksSupport, VolumeSupport].map(SupportClass => {
            return new SupportClass(this);
        }, this);
    }

    _updateControlsSize()
    {
        this.controls.width = this.media.offsetWidth;
        this.controls.height = this.media.offsetHeight;
    }

    _controlsClass()
    {
        const layoutTraits = this.layoutTraits;
        if (layoutTraits & LayoutTraits.Fullscreen)
            return MacOSFullscreenMediaControls;
        return MacOSInlineMediaControls;
    }

}
