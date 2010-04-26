/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2007, Holger Hans Peter Freyther
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ClipboardGtk_h
#define ClipboardGtk_h

#include "DataObjectGtk.h"
#include "CachedResourceClient.h"
#include "Clipboard.h"
#include "PasteboardHelper.h"

namespace WebCore {
    class CachedImage;

    // State available during IE's events for drag and drop and copy/paste
    // Created from the EventHandlerGtk to be used by the dom
    class ClipboardGtk : public Clipboard, public CachedResourceClient {
    public:
        static PassRefPtr<ClipboardGtk> create(ClipboardAccessPolicy policy, PassRefPtr<DataObjectGtk> dataObject, bool isForDragging)
        {
            return adoptRef(new ClipboardGtk(policy, dataObject, isForDragging));
        }
        static PassRefPtr<ClipboardGtk> create(ClipboardAccessPolicy policy, GtkClipboard* clipboard, bool isForDragging)
        {
            return adoptRef(new ClipboardGtk(policy, clipboard, isForDragging));
        }
        virtual ~ClipboardGtk();

        void clearData(const String&);
        void clearAllData();
        String getData(const String&, bool&) const;
        bool setData(const String&, const String&);

        virtual HashSet<String> types() const;
        virtual PassRefPtr<FileList> files() const;

        void setDragImage(CachedImage* image, Node* node, const IntPoint& loc);
        void setDragImage(CachedImage*, const IntPoint&);
        void setDragImageElement(Node *, const IntPoint&);

        virtual DragImageRef createDragImage(IntPoint& dragLoc) const;
        virtual void declareAndWriteDragImage(Element*, const KURL&, const String& title, Frame*);
        virtual void writeRange(Range*, Frame* frame);
        virtual void writeURL(const KURL&, const String&, Frame* frame);
        virtual void writePlainText(const String&);

        virtual bool hasData();
        PassRefPtr<DataObjectGtk> dataObject() { return m_dataObject; }

    private:
        ClipboardGtk(ClipboardAccessPolicy, PassRefPtr<DataObjectGtk>, bool);
        ClipboardGtk(ClipboardAccessPolicy, GtkClipboard*, bool);

        RefPtr<DataObjectGtk> m_dataObject;
        GtkClipboard* m_clipboard;
        PasteboardHelper* m_helper;
    };
}

#endif
