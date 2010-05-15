/*
 *  Copyright (C) 2007 Holger Hans Peter Freyther
 *  Copyright (C) 2007 Alp Toker <alp@atoker.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "Pasteboard.h"

#include "DocumentFragment.h"
#include "Frame.h"
#include "NotImplemented.h"
#include "PlatformString.h"
#include "TextResourceDecoder.h"
#include "Image.h"
#include "RenderImage.h"
#include "KURL.h"
#include "markup.h"
#include <wtf/text/CString.h>

#include <gtk/gtk.h>

namespace WebCore {

Pasteboard* Pasteboard::generalPasteboard()
{
    static Pasteboard* pasteboard = new Pasteboard();
    return pasteboard;
}

Pasteboard::Pasteboard()
{
    notImplemented();
}

Pasteboard::~Pasteboard()
{
    delete m_helper;
}

void Pasteboard::setHelper(PasteboardHelper* helper)
{
    m_helper = helper;
}

void Pasteboard::writeSelection(Range* selectedRange, bool canSmartCopyOrDelete, Frame* frame)
{
    GtkClipboard* clipboard = gtk_clipboard_get_for_display(gdk_display_get_default(), GDK_SELECTION_CLIPBOARD);
    ASSERT(clipboard);
    DataObjectGtk* dataObject = DataObjectGtk::forClipboard(clipboard);
    ASSERT(dataObject);

    dataObject->setText(frame->selectedText());
    dataObject->setMarkup(createMarkup(selectedRange, 0, AnnotateForInterchange));
    m_helper->writeClipboardContents(clipboard);
}

void Pasteboard::writePlainText(const String& text)
{
	GtkClipboard* clipboard = gtk_clipboard_get_for_display(gdk_display_get_default(), GDK_SELECTION_CLIPBOARD);
    DataObjectGtk* dataObject = DataObjectGtk::forClipboard(clipboard);
    ASSERT(dataObject);

    dataObject->setText(text);
    m_helper->writeClipboardContents(clipboard);
}

void Pasteboard::writeURL(const KURL& url, const String& label, Frame* frame)
{
    if (url.isEmpty())
        return;

	GtkClipboard* clipboard = gtk_clipboard_get_for_display(gdk_display_get_default(), GDK_SELECTION_CLIPBOARD);
    DataObjectGtk* dataObject = DataObjectGtk::forClipboard(clipboard);
    ASSERT(dataObject);

    String actualLabel(label);
    if (actualLabel.isEmpty())
        actualLabel = url;

    Vector<KURL> uriList;
    uriList.append(url);
    dataObject->setURIList(uriList);
    dataObject->setText(actualLabel);

    m_helper->writeClipboardContents(clipboard);
}

void Pasteboard::writeImage(Node* node, const KURL&, const String&)
{
    GtkClipboard* clipboard = gtk_clipboard_get_for_display(gdk_display_get_default(), GDK_SELECTION_CLIPBOARD);

    ASSERT(node && node->renderer() && node->renderer()->isImage());
    RenderImage* renderer = toRenderImage(node->renderer());
    CachedImage* cachedImage = renderer->cachedImage();
    if (!cachedImage || cachedImage->errorOccurred())
        return;
    Image* image = cachedImage->image();
    ASSERT(image);

    DataObjectGtk* dataObject = DataObjectGtk::forClipboard(clipboard);
    ASSERT(dataObject);

    GdkPixbuf* pixbuf = image->getGdkPixbuf();
    dataObject->setImage(pixbuf);
    g_object_unref(pixbuf);

    m_helper->writeClipboardContents(clipboard);
}

void Pasteboard::clear()
{
    GtkClipboard* clipboard = gtk_clipboard_get_for_display(gdk_display_get_default(), GDK_SELECTION_CLIPBOARD);
    DataObjectGtk* dataObject = DataObjectGtk::forClipboard(clipboard);
    ASSERT(dataObject);

    dataObject->clear();
    m_helper->writeClipboardContents(clipboard);
}

bool Pasteboard::canSmartReplace()
{
    notImplemented();
    return false;
}

PassRefPtr<DocumentFragment> Pasteboard::documentFragment(Frame* frame, PassRefPtr<Range> context, bool allowPlainText, bool& chosePlainText)
{
    GtkClipboard* clipboard = m_helper->getCurrentTarget(frame);
    ASSERT(clipboard);
    DataObjectGtk* dataObject = DataObjectGtk::forClipboard(clipboard);
    ASSERT(dataObject);
    m_helper->getClipboardContents(clipboard);


    if (dataObject->hasMarkup()) {
        chosePlainText = false;
        String markup(dataObject->markup());

        if (!markup.isEmpty()) {
            RefPtr<DocumentFragment> fragment = createFragmentFromMarkup(frame->document(), markup, "", FragmentScriptingNotAllowed);
            if (fragment)
                return fragment.release();
        }
    }

    if (!allowPlainText || !dataObject->hasText())
        return 0;

    chosePlainText = true;
    String text(dataObject->text());
    RefPtr<DocumentFragment> fragment = createFragmentFromText(context.get(), text);
    if (fragment)
        return fragment.release();

    return 0;
}

String Pasteboard::plainText(Frame* frame)
{
    GtkClipboard* clipboard = m_helper->getCurrentTarget(frame);
    ASSERT(clipboard);
    DataObjectGtk* dataObject = DataObjectGtk::forClipboard(clipboard);
    ASSERT(dataObject);
    m_helper->getClipboardContents(clipboard);
    return dataObject->text();
}

}
