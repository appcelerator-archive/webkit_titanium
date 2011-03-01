/*
 *  Copyright (C) 2007 Alp Toker <alp@atoker.com>
 *  Copyright (C) 2008 Nuanti Ltd.
 *  Copyright (C) 2009 Diego Escalante Urrelo <diegoe@gnome.org>
 *  Copyright (C) 2006, 2007 Apple Inc.  All rights reserved.
 *  Copyright (C) 2009, 2010 Igalia S.L.
 *  Copyright (C) 2010, Martin Robinson <mrobinson@webkit.org>
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
#include "EditorClientGtk.h"

#include "DataObjectGtk.h"
#include "DumpRenderTreeSupportGtk.h"
#include "EditCommand.h"
#include "Editor.h"
#include "EventNames.h"
#include "FocusController.h"
#include "Frame.h"
#include <glib.h>
#include "KeyboardEvent.h"
#include "markup.h"
#include "NotImplemented.h"
#include "Page.h"
#include "PasteboardHelperGtk.h"
#include "PlatformKeyboardEvent.h"
#include "WebKitDOMBinding.h"
#include "WebKitDOMCSSStyleDeclarationPrivate.h"
#include "WebKitDOMHTMLElementPrivate.h"
#include "WebKitDOMNodePrivate.h"
#include "WebKitDOMRangePrivate.h"
#include "WindowsKeyboardCodes.h"
#include "webkitglobalsprivate.h"
#include "webkitmarshal.h"
#include "webkitwebsettingsprivate.h"
#include "webkitwebviewprivate.h"
#include <wtf/text/CString.h>

// Arbitrary depth limit for the undo stack, to keep it from using
// unbounded memory.  This is the maximum number of distinct undoable
// actions -- unbroken stretches of typed characters are coalesced
// into a single action.
#define maximumUndoStackDepth 1000

using namespace WebCore;

namespace WebKit {

static void imContextCommitted(GtkIMContext* context, const gchar* compositionString, EditorClient* client)
{
    Frame* frame = core(client->webView())->focusController()->focusedOrMainFrame();
    if (!frame || !frame->editor()->canEdit())
        return;

    // If this signal fires during a keydown event when we are not in the middle
    // of a composition, then treat this 'commit' as a normal key event and just
    // change the editable area right before the keypress event.
    if (client->treatContextCommitAsKeyEvent()) {
        client->updatePendingComposition(compositionString);
        return;
    }

    // If this signal fires during a mousepress event when we are in the middle
    // of a composition, skip this 'commit' because the composition is already confirmed. 
    if (client->preventNextCompositionCommit()) 
        return;
 
    frame->editor()->confirmComposition(String::fromUTF8(compositionString));
    client->clearPendingComposition();
}

static void imContextPreeditChanged(GtkIMContext* context, EditorClient* client)
{
    Frame* frame = core(client->webView())->focusController()->focusedOrMainFrame();
    if (!frame || !frame->editor()->canEdit())
        return;

    // We ignore the provided PangoAttrList for now.
    GOwnPtr<gchar> newPreedit(0);
    gtk_im_context_get_preedit_string(context, &newPreedit.outPtr(), 0, 0);

    String preeditString = String::fromUTF8(newPreedit.get());
    Vector<CompositionUnderline> underlines;
    underlines.append(CompositionUnderline(0, preeditString.length(), Color(0, 0, 0), false));
    frame->editor()->setComposition(preeditString, underlines, 0, 0);
}

static void backspaceCallback(GtkWidget* widget, EditorClient* client)
{
    g_signal_stop_emission_by_name(widget, "backspace");
    client->addPendingEditorCommand("DeleteBackward");
}

static void selectAllCallback(GtkWidget* widget, gboolean select, EditorClient* client)
{
    g_signal_stop_emission_by_name(widget, "select-all");
    client->addPendingEditorCommand(select ? "SelectAll" : "Unselect");
}

static void cutClipboardCallback(GtkWidget* widget, EditorClient* client)
{
    g_signal_stop_emission_by_name(widget, "cut-clipboard");
    client->addPendingEditorCommand("Cut");
}

static void copyClipboardCallback(GtkWidget* widget, EditorClient* client)
{
    g_signal_stop_emission_by_name(widget, "copy-clipboard");
    client->addPendingEditorCommand("Copy");
}

static void pasteClipboardCallback(GtkWidget* widget, EditorClient* client)
{
    g_signal_stop_emission_by_name(widget, "paste-clipboard");
    client->addPendingEditorCommand("Paste");
}

static void toggleOverwriteCallback(GtkWidget* widget, EditorClient*)
{
    // We don't support toggling the overwrite mode, but the default callback expects
    // the GtkTextView to have a layout, so we handle this signal just to stop it.
    g_signal_stop_emission_by_name(widget, "toggle-overwrite");
}

// GTK+ will still send these signals to the web view. So we can safely stop signal
// emission without breaking accessibility.
static void popupMenuCallback(GtkWidget* widget, EditorClient*)
{
    g_signal_stop_emission_by_name(widget, "popup-menu");
}

static void showHelpCallback(GtkWidget* widget, EditorClient*)
{
    g_signal_stop_emission_by_name(widget, "show-help");
}

static const char* const gtkDeleteCommands[][2] = {
    { "DeleteBackward",               "DeleteForward"                        }, // Characters
    { "DeleteWordBackward",           "DeleteWordForward"                    }, // Word ends
    { "DeleteWordBackward",           "DeleteWordForward"                    }, // Words
    { "DeleteToBeginningOfLine",      "DeleteToEndOfLine"                    }, // Lines
    { "DeleteToBeginningOfLine",      "DeleteToEndOfLine"                    }, // Line ends
    { "DeleteToBeginningOfParagraph", "DeleteToEndOfParagraph"               }, // Paragraph ends
    { "DeleteToBeginningOfParagraph", "DeleteToEndOfParagraph"               }, // Paragraphs
    { 0,                              0                                      } // Whitespace (M-\ in Emacs)
};

static void deleteFromCursorCallback(GtkWidget* widget, GtkDeleteType deleteType, gint count, EditorClient* client)
{
    g_signal_stop_emission_by_name(widget, "delete-from-cursor");
    int direction = count > 0 ? 1 : 0;

    // Ensuring that deleteType <= G_N_ELEMENTS here results in a compiler warning
    // that the condition is always true.

    if (deleteType == GTK_DELETE_WORDS) {
        if (!direction) {
            client->addPendingEditorCommand("MoveWordForward");
            client->addPendingEditorCommand("MoveWordBackward");
        } else {
            client->addPendingEditorCommand("MoveWordBackward");
            client->addPendingEditorCommand("MoveWordForward");
        }
    } else if (deleteType == GTK_DELETE_DISPLAY_LINES) {
        if (!direction)
            client->addPendingEditorCommand("MoveToBeginningOfLine");
        else
            client->addPendingEditorCommand("MoveToEndOfLine");
    } else if (deleteType == GTK_DELETE_PARAGRAPHS) {
        if (!direction)
            client->addPendingEditorCommand("MoveToBeginningOfParagraph");
        else
            client->addPendingEditorCommand("MoveToEndOfParagraph");
    }

    const char* rawCommand = gtkDeleteCommands[deleteType][direction];
    if (!rawCommand)
      return;

    for (int i = 0; i < abs(count); i++)
        client->addPendingEditorCommand(rawCommand);
}

static const char* const gtkMoveCommands[][4] = {
    { "MoveBackward",                                   "MoveForward",
      "MoveBackwardAndModifySelection",                 "MoveForwardAndModifySelection"             }, // Forward/backward grapheme
    { "MoveLeft",                                       "MoveRight",
      "MoveBackwardAndModifySelection",                 "MoveForwardAndModifySelection"             }, // Left/right grapheme
    { "MoveWordBackward",                               "MoveWordForward",
      "MoveWordBackwardAndModifySelection",             "MoveWordForwardAndModifySelection"         }, // Forward/backward word
    { "MoveUp",                                         "MoveDown",
      "MoveUpAndModifySelection",                       "MoveDownAndModifySelection"                }, // Up/down line
    { "MoveToBeginningOfLine",                          "MoveToEndOfLine",
      "MoveToBeginningOfLineAndModifySelection",        "MoveToEndOfLineAndModifySelection"         }, // Up/down line ends
    { "MoveParagraphForward",                           "MoveParagraphBackward",
      "MoveParagraphForwardAndModifySelection",         "MoveParagraphBackwardAndModifySelection"   }, // Up/down paragraphs
    { "MoveToBeginningOfParagraph",                     "MoveToEndOfParagraph",
      "MoveToBeginningOfParagraphAndModifySelection",   "MoveToEndOfParagraphAndModifySelection"    }, // Up/down paragraph ends.
    { "MovePageUp",                                     "MovePageDown",
      "MovePageUpAndModifySelection",                   "MovePageDownAndModifySelection"            }, // Up/down page
    { "MoveToBeginningOfDocument",                      "MoveToEndOfDocument",
      "MoveToBeginningOfDocumentAndModifySelection",    "MoveToEndOfDocumentAndModifySelection"     }, // Begin/end of buffer
    { 0,                                                0,
      0,                                                0                                           } // Horizontal page movement
};

static void moveCursorCallback(GtkWidget* widget, GtkMovementStep step, gint count, gboolean extendSelection, EditorClient* client)
{
    g_signal_stop_emission_by_name(widget, "move-cursor");
    int direction = count > 0 ? 1 : 0;
    if (extendSelection)
        direction += 2;

    if (static_cast<unsigned>(step) >= G_N_ELEMENTS(gtkMoveCommands))
        return;

    const char* rawCommand = gtkMoveCommands[step][direction];
    if (!rawCommand)
        return;

    for (int i = 0; i < abs(count); i++)
        client->addPendingEditorCommand(rawCommand);
}

void EditorClient::updatePendingComposition(const gchar* newComposition)
{
    // The IMContext may signal more than one completed composition in a row,
    // in which case we want to append them, rather than overwrite the old one.
    if (!m_pendingComposition)
        m_pendingComposition.set(g_strdup(newComposition));
    else
        m_pendingComposition.set(g_strconcat(m_pendingComposition.get(), newComposition, NULL));
}

void EditorClient::willSetInputMethodState()
{
}

void EditorClient::setInputMethodState(bool active)
{
    WebKitWebViewPrivate* priv = m_webView->priv;

    if (active)
        gtk_im_context_focus_in(priv->imContext.get());
    else
        gtk_im_context_focus_out(priv->imContext.get());

#ifdef MAEMO_CHANGES
    if (active)
        hildon_gtk_im_context_show(priv->imContext.get());
    else
        hildon_gtk_im_context_hide(priv->imContext.get());
#endif
}

bool EditorClient::shouldDeleteRange(Range* range)
{
    gboolean accept = TRUE;
    GRefPtr<WebKitDOMRange> kitRange(adoptGRef(kit(range)));
    g_signal_emit_by_name(m_webView, "should-delete-range", kitRange.get(), &accept);
    return accept;
}

bool EditorClient::shouldShowDeleteInterface(HTMLElement* element)
{
    gboolean accept = TRUE;
    GRefPtr<WebKitDOMHTMLElement> kitElement(adoptGRef(kit(element)));
    g_signal_emit_by_name(m_webView, "should-show-delete-interface-for-element", kitElement.get(), &accept);
    return accept;
}

bool EditorClient::isContinuousSpellCheckingEnabled()
{
    WebKitWebSettings* settings = webkit_web_view_get_settings(m_webView);

    gboolean enabled;
    g_object_get(settings, "enable-spell-checking", &enabled, NULL);

    return enabled;
}

bool EditorClient::isGrammarCheckingEnabled()
{
    notImplemented();
    return false;
}

int EditorClient::spellCheckerDocumentTag()
{
    notImplemented();
    return 0;
}

bool EditorClient::shouldBeginEditing(WebCore::Range* range)
{
    clearPendingComposition();

    gboolean accept = TRUE;
    GRefPtr<WebKitDOMRange> kitRange(adoptGRef(kit(range)));
    g_signal_emit_by_name(m_webView, "should-begin-editing", kitRange.get(), &accept);
    return accept;
}

bool EditorClient::shouldEndEditing(WebCore::Range* range)
{
    clearPendingComposition();

    gboolean accept = TRUE;
    GRefPtr<WebKitDOMRange> kitRange(adoptGRef(kit(range)));
    g_signal_emit_by_name(m_webView, "should-end-editing", kitRange.get(), &accept);
    return accept;
}

static WebKitInsertAction kit(EditorInsertAction action)
{
    switch (action) {
    case EditorInsertActionTyped:
        return WEBKIT_INSERT_ACTION_TYPED;
    case EditorInsertActionPasted:
        return WEBKIT_INSERT_ACTION_PASTED;
    case EditorInsertActionDropped:
        return WEBKIT_INSERT_ACTION_DROPPED;
    }
    ASSERT_NOT_REACHED();
    return WEBKIT_INSERT_ACTION_TYPED;
}

bool EditorClient::shouldInsertText(const String& string, Range* range, EditorInsertAction action)
{
    gboolean accept = TRUE;
    GRefPtr<WebKitDOMRange> kitRange(adoptGRef(kit(range)));
    g_signal_emit_by_name(m_webView, "should-insert-text", string.utf8().data(), kitRange.get(), kit(action), &accept);
    return accept;
}

static WebKitSelectionAffinity kit(EAffinity affinity)
{
    switch (affinity) {
    case UPSTREAM:
        return WEBKIT_SELECTION_AFFINITY_UPSTREAM;
    case DOWNSTREAM:
        return WEBKIT_SELECTION_AFFINITY_DOWNSTREAM;
    }
    ASSERT_NOT_REACHED();
    return WEBKIT_SELECTION_AFFINITY_UPSTREAM;
}

bool EditorClient::shouldChangeSelectedRange(Range* fromRange, Range* toRange, EAffinity affinity, bool stillSelecting)
{
    gboolean accept = TRUE;
    GRefPtr<WebKitDOMRange> kitFromRange(fromRange ? adoptGRef(kit(fromRange)) : 0);
    GRefPtr<WebKitDOMRange> kitToRange(toRange ? adoptGRef(kit(toRange)) : 0);
    g_signal_emit_by_name(m_webView, "should-change-selected-range", kitFromRange.get(), kitToRange.get(),
                          kit(affinity), stillSelecting, &accept);
    return accept;
}

bool EditorClient::shouldApplyStyle(WebCore::CSSStyleDeclaration* declaration, WebCore::Range* range)
{
    gboolean accept = TRUE;
    GRefPtr<WebKitDOMCSSStyleDeclaration> kitDeclaration(kit(declaration));
    GRefPtr<WebKitDOMRange> kitRange(adoptGRef(kit(range)));
    g_signal_emit_by_name(m_webView, "should-apply-style", kitDeclaration.get(), kitRange.get(), &accept);
    return accept;
}

bool EditorClient::shouldMoveRangeAfterDelete(WebCore::Range*, WebCore::Range*)
{
    notImplemented();
    return true;
}

void EditorClient::didBeginEditing()
{
    g_signal_emit_by_name(m_webView, "editing-began");
}

void EditorClient::respondToChangedContents()
{
    g_signal_emit_by_name(m_webView, "user-changed-contents");
}

static WebKitWebView* viewSettingClipboard = 0;
static void collapseSelection(GtkClipboard* clipboard, WebKitWebView* webView)
{
    if (viewSettingClipboard && viewSettingClipboard == webView)
        return;

    WebCore::Page* corePage = core(webView);
    if (!corePage || !corePage->focusController())
        return;

    Frame* frame = corePage->focusController()->focusedOrMainFrame();

    // Collapse the selection without clearing it
    ASSERT(frame);
    frame->selection()->setBase(frame->selection()->extent(), frame->selection()->affinity());
}

#if PLATFORM(X11)
static void setSelectionPrimaryClipboardIfNeeded(WebKitWebView* webView)
{
    if (!gtk_widget_has_screen(GTK_WIDGET(webView)))
        return;

    GtkClipboard* clipboard = gtk_widget_get_clipboard(GTK_WIDGET(webView), GDK_SELECTION_PRIMARY);
    DataObjectGtk* dataObject = DataObjectGtk::forClipboard(clipboard);
    WebCore::Page* corePage = core(webView);
    Frame* targetFrame = corePage->focusController()->focusedOrMainFrame();

    if (!targetFrame->selection()->isRange())
        return;

    dataObject->clear();
    dataObject->setRange(targetFrame->selection()->toNormalizedRange());

    viewSettingClipboard = webView;
    GClosure* callback = g_cclosure_new_object(G_CALLBACK(collapseSelection), G_OBJECT(webView));
    g_closure_set_marshal(callback, g_cclosure_marshal_VOID__VOID);
    pasteboardHelperInstance()->writeClipboardContents(clipboard, callback);
    viewSettingClipboard = 0;
}
#endif

void EditorClient::respondToChangedSelection()
{
    g_signal_emit_by_name(m_webView, "selection-changed");

    WebKitWebViewPrivate* priv = m_webView->priv;
    WebCore::Page* corePage = core(m_webView);
    Frame* targetFrame = corePage->focusController()->focusedOrMainFrame();

    if (!targetFrame)
        return;

    if (targetFrame->editor()->ignoreCompositionSelectionChange())
        return;

#if PLATFORM(X11)
    setSelectionPrimaryClipboardIfNeeded(m_webView);
#endif

    if (!targetFrame->editor()->hasComposition())
        return;

    unsigned start;
    unsigned end;
    if (!targetFrame->editor()->getCompositionSelection(start, end)) {
        // gtk_im_context_reset() clears the composition for us.
        gtk_im_context_reset(priv->imContext.get());
        targetFrame->editor()->confirmCompositionWithoutDisturbingSelection();
    }
}

void EditorClient::didEndEditing()
{
    g_signal_emit_by_name(m_webView, "editing-ended");
}

void EditorClient::didWriteSelectionToPasteboard()
{
    notImplemented();
}

void EditorClient::didSetSelectionTypesForPasteboard()
{
    notImplemented();
}

void EditorClient::registerCommandForUndo(WTF::PassRefPtr<WebCore::EditCommand> command)
{
    if (undoStack.size() == maximumUndoStackDepth)
        undoStack.removeFirst();
    if (!m_isInRedo)
        redoStack.clear();
    undoStack.append(command);
}

void EditorClient::registerCommandForRedo(WTF::PassRefPtr<WebCore::EditCommand> command)
{
    redoStack.append(command);
}

void EditorClient::clearUndoRedoOperations()
{
    undoStack.clear();
    redoStack.clear();
}

bool EditorClient::canCopyCut(bool defaultValue) const
{
    return defaultValue;
}

bool EditorClient::canPaste(bool defaultValue) const
{
    return defaultValue;
}

bool EditorClient::canUndo() const
{
    return !undoStack.isEmpty();
}

bool EditorClient::canRedo() const
{
    return !redoStack.isEmpty();
}

void EditorClient::undo()
{
    if (canUndo()) {
        RefPtr<WebCore::EditCommand> command(*(--undoStack.end()));
        undoStack.remove(--undoStack.end());
        // unapply will call us back to push this command onto the redo stack.
        command->unapply();
    }
}

void EditorClient::redo()
{
    if (canRedo()) {
        RefPtr<WebCore::EditCommand> command(*(--redoStack.end()));
        redoStack.remove(--redoStack.end());

        ASSERT(!m_isInRedo);
        m_isInRedo = true;
        // reapply will call us back to push this command onto the undo stack.
        command->reapply();
        m_isInRedo = false;
    }
}

bool EditorClient::shouldInsertNode(Node* node, Range* range, EditorInsertAction action)
{
    gboolean accept = TRUE;
    GRefPtr<WebKitDOMRange> kitRange(adoptGRef(kit(range)));
    GRefPtr<WebKitDOMNode> kitNode(adoptGRef(kit(node)));
    g_signal_emit_by_name(m_webView, "should-insert-node", kitNode.get(), kitRange.get(), kit(action), &accept);
    return accept;
}

void EditorClient::pageDestroyed()
{
    delete this;
}

bool EditorClient::smartInsertDeleteEnabled()
{
    notImplemented();
    return false;
}

bool EditorClient::isSelectTrailingWhitespaceEnabled()
{
    if (!DumpRenderTreeSupportGtk::dumpRenderTreeModeEnabled())
        return false;
    return DumpRenderTreeSupportGtk::selectTrailingWhitespaceEnabled();
}

void EditorClient::toggleContinuousSpellChecking()
{
    WebKitWebSettings* settings = webkit_web_view_get_settings(m_webView);

    gboolean enabled;
    g_object_get(settings, "enable-spell-checking", &enabled, NULL);

    g_object_set(settings, "enable-spell-checking", !enabled, NULL);
}

void EditorClient::toggleGrammarChecking()
{
}

static const unsigned CtrlKey = 1 << 0;
static const unsigned AltKey = 1 << 1;
static const unsigned ShiftKey = 1 << 2;

struct KeyDownEntry {
    unsigned virtualKey;
    unsigned modifiers;
    const char* name;
};

struct KeyPressEntry {
    unsigned charCode;
    unsigned modifiers;
    const char* name;
};

static const KeyDownEntry keyDownEntries[] = {
    { 'B',       CtrlKey,            "ToggleBold"                                  },
    { 'I',       CtrlKey,            "ToggleItalic"                                },
    { VK_ESCAPE, 0,                  "Cancel"                                      },
    { VK_OEM_PERIOD, CtrlKey,        "Cancel"                                      },
    { VK_TAB,    0,                  "InsertTab"                                   },
    { VK_TAB,    ShiftKey,           "InsertBacktab"                               },
    { VK_RETURN, 0,                  "InsertNewline"                               },
    { VK_RETURN, CtrlKey,            "InsertNewline"                               },
    { VK_RETURN, AltKey,             "InsertNewline"                               },
    { VK_RETURN, AltKey | ShiftKey,  "InsertNewline"                               },
};

static const KeyPressEntry keyPressEntries[] = {
    { '\t',   0,                  "InsertTab"                                   },
    { '\t',   ShiftKey,           "InsertBacktab"                               },
    { '\r',   0,                  "InsertNewline"                               },
    { '\r',   CtrlKey,            "InsertNewline"                               },
    { '\r',   AltKey,             "InsertNewline"                               },
    { '\r',   AltKey | ShiftKey,  "InsertNewline"                               },
};

void EditorClient::generateEditorCommands(const KeyboardEvent* event)
{
    ASSERT(event->type() == eventNames().keydownEvent || event->type() == eventNames().keypressEvent);

    m_pendingEditorCommands.clear();

    // First try to interpret the command as a native GTK+ key binding.
#ifdef GTK_API_VERSION_2
    gtk_bindings_activate_event(GTK_OBJECT(m_nativeWidget.get()), event->keyEvent()->gdkEventKey());
#else
    gtk_bindings_activate_event(G_OBJECT(m_nativeWidget.get()), event->keyEvent()->gdkEventKey());
#endif
    if (m_pendingEditorCommands.size() > 0)
        return;

    static HashMap<int, const char*> keyDownCommandsMap;
    static HashMap<int, const char*> keyPressCommandsMap;

    if (keyDownCommandsMap.isEmpty()) {
        for (unsigned i = 0; i < G_N_ELEMENTS(keyDownEntries); i++)
            keyDownCommandsMap.set(keyDownEntries[i].modifiers << 16 | keyDownEntries[i].virtualKey, keyDownEntries[i].name);

        for (unsigned i = 0; i < G_N_ELEMENTS(keyPressEntries); i++)
            keyPressCommandsMap.set(keyPressEntries[i].modifiers << 16 | keyPressEntries[i].charCode, keyPressEntries[i].name);
    }

    unsigned modifiers = 0;
    if (event->shiftKey())
        modifiers |= ShiftKey;
    if (event->altKey())
        modifiers |= AltKey;
    if (event->ctrlKey())
        modifiers |= CtrlKey;

    // For keypress events, we want charCode(), but keyCode() does that.
    int mapKey = modifiers << 16 | event->keyCode();
    if (!mapKey)
        return;
    HashMap<int, const char*>* commandMap = event->type() == eventNames().keydownEvent ?
        &keyDownCommandsMap : &keyPressCommandsMap;
    if (const char* commandString = commandMap->get(mapKey))
        m_pendingEditorCommands.append(commandString);
}

bool EditorClient::executePendingEditorCommands(Frame* frame, bool allowTextInsertion)
{
    Vector<Editor::Command> commands;
    for (size_t i = 0; i < m_pendingEditorCommands.size(); i++) {
        const char* commandString = m_pendingEditorCommands.at(i);
        ASSERT(commandString);
        Editor::Command command = frame->editor()->command(commandString);
        if (command.isTextInsertion() && !allowTextInsertion)
            return false;

        commands.append(command);
    }

    bool success = true;
    for (size_t i = 0; i < commands.size(); i++) {
        if (!commands.at(i).execute()) {
            success = false;
            break;
        }
    }

    m_pendingEditorCommands.clear();

    // If we successfully completed all editor commands, then
    // this signals a canceling of the composition.
    if (success)
        clearPendingComposition();

    return success;
}

void EditorClient::handleKeyboardEvent(KeyboardEvent* event)
{
    Node* node = event->target()->toNode();
    ASSERT(node);
    Frame* frame = node->document()->frame();
    ASSERT(frame);

    const PlatformKeyboardEvent* platformEvent = event->keyEvent();
    if (!platformEvent)
        return;

    generateEditorCommands(event);
    if (m_pendingEditorCommands.size() > 0) {

        // During RawKeyDown events if an editor command will insert text, defer
        // the insertion until the keypress event. We want keydown to bubble up
        // through the DOM first.
        if (platformEvent->type() == PlatformKeyboardEvent::RawKeyDown) {
            if (executePendingEditorCommands(frame, false))
                event->setDefaultHandled();

            return;
        }

        // Only allow text insertion commands if the current node is editable.
        if (executePendingEditorCommands(frame, frame->editor()->canEdit())) {
            event->setDefaultHandled();
            return;
        }
    }

    // Don't allow text insertion for nodes that cannot edit.
    if (!frame->editor()->canEdit())
        return;

    // This is just a normal text insertion, so wait to execute the insertion
    // until a keypress event happens. This will ensure that the insertion will not
    // be reflected in the contents of the field until the keyup DOM event.
    if (event->type() == eventNames().keypressEvent) {

        // If we have a pending composition at this point, it happened while
        // filtering a keypress, so we treat it as a normal text insertion.
        // This will also ensure that if the keypress event handler changed the
        // currently focused node, the text is still inserted into the original
        // node (insertText() has this logic, but confirmComposition() does not).
        if (m_pendingComposition) {
            frame->editor()->insertText(String::fromUTF8(m_pendingComposition.get()), event);
            clearPendingComposition();
            event->setDefaultHandled();

        } else {
            // Don't insert null or control characters as they can result in unexpected behaviour
            if (event->charCode() < ' ')
                return;

            // Don't insert anything if a modifier is pressed
            if (platformEvent->ctrlKey() || platformEvent->altKey())
                return;

            if (frame->editor()->insertText(platformEvent->text(), event))
                event->setDefaultHandled();
        }
    }
}

void EditorClient::handleInputMethodKeydown(KeyboardEvent* event)
{
    Frame* targetFrame = core(m_webView)->focusController()->focusedOrMainFrame();
    if (!targetFrame || !targetFrame->editor()->canEdit())
        return;

    WebKitWebViewPrivate* priv = m_webView->priv;

    m_preventNextCompositionCommit = false;

    // Some IM contexts (e.g. 'simple') will act as if they filter every
    // keystroke and just issue a 'commit' signal during handling. In situations
    // where the 'commit' signal happens during filtering and there is no active
    // composition, act as if the keystroke was not filtered. The one exception to
    // this is when the keyval parameter of the GdkKeyEvent is 0, which is often
    // a key event sent by the IM context for committing the current composition.

    // Here is a typical sequence of events for the 'simple' context:
    // 1. GDK key press event -> webkit_web_view_key_press_event
    // 2. Keydown event -> EditorClient::handleInputMethodKeydown
    //     gtk_im_context_filter_keypress returns true, but there is a pending
    //     composition so event->preventDefault is not called (below).
    // 3. Keydown event bubbles through the DOM
    // 4. Keydown event -> EditorClient::handleKeyboardEvent
    //     No action taken.
    // 4. GDK key release event -> webkit_web_view_key_release_event
    // 5. gtk_im_context_filter_keypress is called on the release event.
    //     Simple does not filter most key releases, so the event continues.
    // 6. Keypress event bubbles through the DOM.
    // 7. Keypress event -> EditorClient::handleKeyboardEvent
    //     pending composition is inserted.
    // 8. Keyup event bubbles through the DOM.
    // 9. Keyup event -> EditorClient::handleKeyboardEvent
    //     No action taken.

    // There are two situations where we do filter the keystroke:
    // 1. The IMContext instructed us to filter and we have no pending composition.
    // 2. The IMContext did not instruct us to filter, but the keystroke caused a
    //    composition in progress to finish. It seems that sometimes SCIM will finish
    //    a composition and not mark the keystroke as filtered.
    m_treatContextCommitAsKeyEvent = (!targetFrame->editor()->hasComposition())
         && event->keyEvent()->gdkEventKey()->keyval;
    clearPendingComposition();
    if ((gtk_im_context_filter_keypress(priv->imContext.get(), event->keyEvent()->gdkEventKey()) && !m_pendingComposition)
        || (!m_treatContextCommitAsKeyEvent && !targetFrame->editor()->hasComposition()))
        event->preventDefault();

    m_treatContextCommitAsKeyEvent = false;
}

void EditorClient::handleInputMethodMousePress()
{
    Frame* targetFrame = core(m_webView)->focusController()->focusedOrMainFrame();

    if (!targetFrame || !targetFrame->editor()->canEdit())
        return;

    WebKitWebViewPrivate* priv = m_webView->priv;

    // When a mouse press fires, the commit signal happens during a composition.
    // In this case, if the focused node is changed, the commit signal happens in a diffrent node.
    // Therefore, we need to confirm the current compositon and ignore the next commit signal. 
    GOwnPtr<gchar> newPreedit(0);
    gtk_im_context_get_preedit_string(priv->imContext.get(), &newPreedit.outPtr(), 0, 0);
    
    if (g_utf8_strlen(newPreedit.get(), -1)) {
        targetFrame->editor()->confirmComposition();
        m_preventNextCompositionCommit = true;
        gtk_im_context_reset(priv->imContext.get());
    } 
}

EditorClient::EditorClient(WebKitWebView* webView)
    : m_isInRedo(false)
#if ENABLE(SPELLCHECK)
    , m_textCheckerClient(webView)
#endif
    , m_webView(webView)
    , m_preventNextCompositionCommit(false)
    , m_treatContextCommitAsKeyEvent(false)
    , m_nativeWidget(gtk_text_view_new())
{
    WebKitWebViewPrivate* priv = m_webView->priv;
    g_signal_connect(priv->imContext.get(), "commit", G_CALLBACK(imContextCommitted), this);
    g_signal_connect(priv->imContext.get(), "preedit-changed", G_CALLBACK(imContextPreeditChanged), this);

    g_signal_connect(m_nativeWidget.get(), "backspace", G_CALLBACK(backspaceCallback), this);
    g_signal_connect(m_nativeWidget.get(), "cut-clipboard", G_CALLBACK(cutClipboardCallback), this);
    g_signal_connect(m_nativeWidget.get(), "copy-clipboard", G_CALLBACK(copyClipboardCallback), this);
    g_signal_connect(m_nativeWidget.get(), "paste-clipboard", G_CALLBACK(pasteClipboardCallback), this);
    g_signal_connect(m_nativeWidget.get(), "select-all", G_CALLBACK(selectAllCallback), this);
    g_signal_connect(m_nativeWidget.get(), "move-cursor", G_CALLBACK(moveCursorCallback), this);
    g_signal_connect(m_nativeWidget.get(), "delete-from-cursor", G_CALLBACK(deleteFromCursorCallback), this);
    g_signal_connect(m_nativeWidget.get(), "toggle-overwrite", G_CALLBACK(toggleOverwriteCallback), this);
    g_signal_connect(m_nativeWidget.get(), "popup-menu", G_CALLBACK(popupMenuCallback), this);
    g_signal_connect(m_nativeWidget.get(), "show-help", G_CALLBACK(showHelpCallback), this);
}

EditorClient::~EditorClient()
{
    WebKitWebViewPrivate* priv = m_webView->priv;
    g_signal_handlers_disconnect_by_func(priv->imContext.get(), (gpointer)imContextCommitted, this);
    g_signal_handlers_disconnect_by_func(priv->imContext.get(), (gpointer)imContextPreeditChanged, this);
}

void EditorClient::textFieldDidBeginEditing(Element*)
{
}

void EditorClient::textFieldDidEndEditing(Element*)
{
}

void EditorClient::textDidChangeInTextField(Element*)
{
}

bool EditorClient::doTextFieldCommandFromEvent(Element*, KeyboardEvent*)
{
    return false;
}

void EditorClient::textWillBeDeletedInTextField(Element*)
{
    notImplemented();
}

void EditorClient::textDidChangeInTextArea(Element*)
{
    notImplemented();
}

void EditorClient::updateSpellingUIWithGrammarString(const String&, const GrammarDetail&)
{
    notImplemented();
}

void EditorClient::updateSpellingUIWithMisspelledWord(const String&)
{
    notImplemented();
}

void EditorClient::showSpellingUI(bool)
{
    notImplemented();
}

bool EditorClient::spellingUIIsShowing()
{
    notImplemented();
    return false;
}

}
