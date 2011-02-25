/*
 *  Copyright (C) 1999-2001 Harri Porten (porten@kde.org)
 *  Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc. All rights reserved.
 *  Copyright (C) 2007 Samuel Weinig <sam@webkit.org>
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
#include "JSDOMBinding.h"

#include "debugger/DebuggerCallFrame.h"

#include "ActiveDOMObject.h"
#include "DOMCoreException.h"
#include "DOMObjectHashTableMap.h"
#include "Document.h"
#include "EventException.h"
#include "ExceptionBase.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "HTMLAudioElement.h"
#include "HTMLCanvasElement.h"
#include "HTMLFrameElementBase.h"
#include "HTMLImageElement.h"
#include "HTMLLinkElement.h"
#include "HTMLNames.h"
#include "HTMLScriptElement.h"
#include "HTMLStyleElement.h"
#include "JSDOMCoreException.h"
#include "JSDOMWindowCustom.h"
#include "JSEventException.h"
#include "JSExceptionBase.h"
#include "JSMainThreadExecState.h"
#include "JSNode.h"
#include "JSRangeException.h"
#include "JSXMLHttpRequestException.h"
#include "KURL.h"
#include "MessagePort.h"
#include "ProcessingInstruction.h"
#include "RangeException.h"
#include "ScriptCachedFrameData.h"
#include "ScriptCallStack.h"
#include "ScriptController.h"
#include "Settings.h"
#include "WebCoreJSClientData.h"
#include "XMLHttpRequestException.h"
#include <runtime/DateInstance.h>
#include <runtime/Error.h>
#include <runtime/JSFunction.h>
#include <wtf/MathExtras.h>
#include <wtf/StdLibExtras.h>

#if ENABLE(SVG)
#include "JSSVGException.h"
#include "SVGException.h"
#endif

#if ENABLE(XPATH)
#include "JSXPathException.h"
#include "XPathException.h"
#endif

#if ENABLE(DATABASE)
#include "JSSQLException.h"
#include "SQLException.h"
#endif

#if ENABLE(BLOB) || ENABLE(FILE_SYSTEM)
#include "FileException.h"
#include "JSFileException.h"
#endif

#if ENABLE(INDEXED_DATABASE)
#include "IDBDatabaseException.h"
#include "JSIDBDatabaseException.h"
#endif

using namespace JSC;

namespace WebCore {

using namespace HTMLNames;

typedef Document::JSWrapperCache JSWrapperCache;
typedef Document::JSWrapperCacheMap JSWrapperCacheMap;

class JSGlobalDataWorldIterator {
public:
    JSGlobalDataWorldIterator(JSGlobalData* globalData)
        : m_pos(static_cast<WebCoreJSClientData*>(globalData->clientData)->m_worldSet.begin())
        , m_end(static_cast<WebCoreJSClientData*>(globalData->clientData)->m_worldSet.end())
    {
    }

    operator bool()
    {
        return m_pos != m_end;
    }

    DOMWrapperWorld* operator*()
    {
        ASSERT(m_pos != m_end);
        return *m_pos;
    }

    DOMWrapperWorld* operator->()
    {
        ASSERT(m_pos != m_end);
        return *m_pos;
    }

    JSGlobalDataWorldIterator& operator++()
    {
        ++m_pos;
        return *this;
    }

private:
    HashSet<DOMWrapperWorld*>::iterator m_pos;
    HashSet<DOMWrapperWorld*>::iterator m_end;
};

const JSC::HashTable* getHashTableForGlobalData(JSGlobalData& globalData, const JSC::HashTable* staticTable)
{
    return DOMObjectHashTableMap::mapFor(globalData).get(staticTable);
}

bool hasCachedDOMObjectWrapperUnchecked(JSGlobalData* globalData, void* objectHandle)
{
    for (JSGlobalDataWorldIterator worldIter(globalData); worldIter; ++worldIter) {
        if (worldIter->m_wrappers.get(objectHandle))
            return true;
    }
    return false;
}

bool hasCachedDOMObjectWrapper(JSGlobalData* globalData, void* objectHandle)
{
    for (JSGlobalDataWorldIterator worldIter(globalData); worldIter; ++worldIter) {
        if (worldIter->m_wrappers.get(objectHandle))
            return true;
    }
    return false;
}

DOMObject* getCachedDOMObjectWrapper(JSC::ExecState* exec, void* objectHandle) 
{
    return domObjectWrapperMapFor(exec).get(objectHandle);
}

void cacheDOMObjectWrapper(JSC::ExecState* exec, void* objectHandle, DOMObject* wrapper) 
{
    domObjectWrapperMapFor(exec).set(exec->globalData(), objectHandle, wrapper);
}

bool hasCachedDOMNodeWrapperUnchecked(Document* document, Node* node)
{
    if (!document)
        return hasCachedDOMObjectWrapperUnchecked(JSDOMWindow::commonJSGlobalData(), node);

    JSWrapperCacheMap& wrapperCacheMap = document->wrapperCacheMap();
    for (JSWrapperCacheMap::iterator iter = wrapperCacheMap.begin(); iter != wrapperCacheMap.end(); ++iter) {
        if (iter->second->get(node))
            return true;
    }
    return false;
}

void cacheDOMNodeWrapper(JSC::ExecState* exec, Document* document, Node* node, JSNode* wrapper)
{
    if (!document)
        domObjectWrapperMapFor(exec).set(exec->globalData(), node, wrapper);
    else
        document->getWrapperCache(currentWorld(exec))->set(exec->globalData(), node, wrapper);

    if (currentWorld(exec)->isNormal())
        node->setWrapper(exec->globalData(), wrapper);
}

static inline bool isObservableThroughDOM(JSNode* jsNode, DOMWrapperWorld* world)
{
    // Certain conditions implicitly make existence of a JS DOM node wrapper observable
    // through the DOM, even if no explicit reference to it remains.

    Node* node = jsNode->impl();

    if (node->inDocument()) {
        // If a node is in the document, and its wrapper has custom properties,
        // the wrapper is observable because future access to the node through the
        // DOM must reflect those properties.
        if (jsNode->hasCustomProperties())
            return true;

        // If a node is in the document, and has event listeners, its wrapper is
        // observable because its wrapper is responsible for marking those event listeners.
        if (node->hasEventListeners())
            return true; // Technically, we may overzealously mark a wrapper for a node that has only non-JS event listeners. Oh well.

        // If a node owns another object with a wrapper with custom properties,
        // the wrapper must be treated as observable, because future access to
        // those objects through the DOM must reflect those properties.
        // FIXME: It would be better if this logic could be in the node next to
        // the custom markChildren functions rather than here.
        // Note that for some compound objects like stylesheets and CSSStyleDeclarations,
        // we don't descend to check children for custom properties, and just conservatively
        // keep the node wrappers protecting them alive.
        if (node->isElementNode()) {
            if (NamedNodeMap* attributes = static_cast<Element*>(node)->attributeMap()) {
                if (DOMObject* wrapper = world->m_wrappers.get(attributes)) {
                    // FIXME: This check seems insufficient, because NamedNodeMap items can have custom properties themselves.
                    // Maybe it would be OK to just keep the wrapper alive, as it is done for CSSOM objects below.
                    if (wrapper->hasCustomProperties())
                        return true;
                }
            }
            if (node->isStyledElement()) {
                if (CSSMutableStyleDeclaration* style = static_cast<StyledElement*>(node)->inlineStyleDecl()) {
                    if (world->m_wrappers.get(style))
                        return true;
                }
            }
            if (static_cast<Element*>(node)->hasTagName(canvasTag)) {
                if (CanvasRenderingContext* context = static_cast<HTMLCanvasElement*>(node)->renderingContext()) {
                    if (DOMObject* wrapper = world->m_wrappers.get(context)) {
                        if (wrapper->hasCustomProperties())
                            return true;
                    }
                }
            } else if (static_cast<Element*>(node)->hasTagName(linkTag)) {
                if (StyleSheet* sheet = static_cast<HTMLLinkElement*>(node)->sheet()) {
                    if (world->m_wrappers.get(sheet))
                        return true;
                }
            } else if (static_cast<Element*>(node)->hasTagName(styleTag)) {
                if (StyleSheet* sheet = static_cast<HTMLStyleElement*>(node)->sheet()) {
                    if (world->m_wrappers.get(sheet))
                        return true;
                }
            }
        } else if (node->nodeType() == Node::PROCESSING_INSTRUCTION_NODE) {
            if (StyleSheet* sheet = static_cast<ProcessingInstruction*>(node)->sheet()) {
                if (world->m_wrappers.get(sheet))
                    return true;
            }
        }
    } else {
        // If a wrapper is the last reference to an image or script element
        // that is loading but not in the document, the wrapper is observable
        // because it is the only thing keeping the image element alive, and if
        // the image element is destroyed, its load event will not fire.
        // FIXME: The DOM should manage this issue without the help of JavaScript wrappers.
        if (node->hasTagName(imgTag) && !static_cast<HTMLImageElement*>(node)->haveFiredLoadEvent())
            return true;
        if (node->hasTagName(scriptTag) && !static_cast<HTMLScriptElement*>(node)->haveFiredLoadEvent())
            return true;
#if ENABLE(VIDEO)
        if (node->hasTagName(audioTag) && !static_cast<HTMLAudioElement*>(node)->paused())
            return true;
#endif
    }

    // If a node is firing event listeners, its wrapper is observable because
    // its wrapper is responsible for marking those event listeners.
    if (node->isFiringEventListeners())
        return true;

    return false;
}

void markDOMNodesForDocument(MarkStack& markStack, Document* document)
{
    JSWrapperCacheMap& wrapperCacheMap = document->wrapperCacheMap();
    for (JSWrapperCacheMap::iterator wrappersIter = wrapperCacheMap.begin(); wrappersIter != wrapperCacheMap.end(); ++wrappersIter) {
        DOMWrapperWorld* world = wrappersIter->first;
        JSWrapperCache* nodeDict = wrappersIter->second;

        JSWrapperCache::iterator nodeEnd = nodeDict->end();
        for (JSWrapperCache::iterator nodeIt = nodeDict->begin(); nodeIt != nodeEnd; ++nodeIt) {
            if (isObservableThroughDOM(nodeIt.get().second, world))
                markStack.deprecatedAppend(nodeIt.getSlot().second);
        }
    }
}

void markActiveObjectsForContext(MarkStack& markStack, JSGlobalData& globalData, ScriptExecutionContext* scriptExecutionContext)
{
    // If an element has pending activity that may result in event listeners being called
    // (e.g. an XMLHttpRequest), we need to keep JS wrappers alive.

    const HashMap<ActiveDOMObject*, void*>& activeObjects = scriptExecutionContext->activeDOMObjects();
    HashMap<ActiveDOMObject*, void*>::const_iterator activeObjectsEnd = activeObjects.end();
    for (HashMap<ActiveDOMObject*, void*>::const_iterator iter = activeObjects.begin(); iter != activeObjectsEnd; ++iter) {
        if (iter->first->hasPendingActivity()) {
            // Generally, an active object with pending activity must have a wrapper to mark its listeners.
            // However, some ActiveDOMObjects don't have JS wrappers.
            markDOMObjectWrapper(markStack, globalData, iter->second);
        }
    }

    const HashSet<MessagePort*>& messagePorts = scriptExecutionContext->messagePorts();
    HashSet<MessagePort*>::const_iterator portsEnd = messagePorts.end();
    for (HashSet<MessagePort*>::const_iterator iter = messagePorts.begin(); iter != portsEnd; ++iter) {
        // If the message port is remotely entangled, then always mark it as in-use because we can't determine reachability across threads.
        if (!(*iter)->locallyEntangledPort() || (*iter)->hasPendingActivity())
            markDOMObjectWrapper(markStack, globalData, *iter);
    }
}

typedef std::pair<JSNode*, DOMWrapperWorld*> WrapperAndWorld;
typedef WTF::Vector<WrapperAndWorld, 8> WrapperSet;

static inline void takeWrappers(Node* node, Document* document, WrapperSet& wrapperSet)
{
    if (document) {
        JSWrapperCacheMap& wrapperCacheMap = document->wrapperCacheMap();
        for (JSWrapperCacheMap::iterator iter = wrapperCacheMap.begin(); iter != wrapperCacheMap.end(); ++iter) {
            if (JSNode* wrapper = iter->second->take(node))
                wrapperSet.append(WrapperAndWorld(wrapper, iter->first));
        }
    } else {
        for (JSGlobalDataWorldIterator worldIter(JSDOMWindow::commonJSGlobalData()); worldIter; ++worldIter) {
            DOMWrapperWorld* world = *worldIter;
            if (JSNode* wrapper = static_cast<JSNode*>(world->m_wrappers.take(node)))
                wrapperSet.append(WrapperAndWorld(wrapper, world));
        }
    }
}

void updateDOMNodeDocument(Node* node, Document* oldDocument, Document* newDocument)
{
    ASSERT(oldDocument != newDocument);

    WrapperSet wrapperSet;
    takeWrappers(node, oldDocument, wrapperSet);

    for (unsigned i = 0; i < wrapperSet.size(); ++i) {
        JSNode* wrapper = wrapperSet[i].first;
        if (newDocument)
            newDocument->getWrapperCache(wrapperSet[i].second)->set(*wrapperSet[i].second->globalData(), node, wrapper);
        else
            wrapperSet[i].second->m_wrappers.set(*wrapperSet[i].second->globalData(), node, wrapper);
    }
}

void markDOMObjectWrapper(MarkStack& markStack, JSGlobalData& globalData, void* object)
{
    // FIXME: This could be changed to only mark wrappers that are "observable"
    // as markDOMNodesForDocument does, allowing us to collect more wrappers,
    // but doing this correctly would be challenging.
    if (!object)
        return;

    for (JSGlobalDataWorldIterator worldIter(&globalData); worldIter; ++worldIter) {
        if (HandleSlot wrapperSlot = worldIter->m_wrappers.getSlot(object))
            markStack.deprecatedAppend(wrapperSlot);
    }
}

void markDOMNodeWrapper(MarkStack& markStack, Document* document, Node* node)
{
    if (document) {
        JSWrapperCacheMap& wrapperCacheMap = document->wrapperCacheMap();
        for (JSWrapperCacheMap::iterator iter = wrapperCacheMap.begin(); iter != wrapperCacheMap.end(); ++iter) {
            if (HandleSlot wrapperSlot = iter->second->getSlot(node))
                markStack.deprecatedAppend(wrapperSlot);
        }
        return;
    }

    for (JSGlobalDataWorldIterator worldIter(JSDOMWindow::commonJSGlobalData()); worldIter; ++worldIter) {
        if (HandleSlot wrapperSlot = worldIter->m_wrappers.getSlot(node))
            markStack.deprecatedAppend(wrapperSlot);
    }
}

static void stringWrapperDestroyed(JSString*, void* context)
{
    StringImpl* cacheKey = static_cast<StringImpl*>(context);
    cacheKey->deref();
}

JSValue jsStringSlowCase(ExecState* exec, JSStringCache& stringCache, StringImpl* stringImpl)
{
    JSString* wrapper = jsStringWithFinalizer(exec, UString(stringImpl), stringWrapperDestroyed, stringImpl);
    stringCache.set(exec->globalData(), stringImpl, wrapper);
    // ref explicitly instead of using a RefPtr-keyed hashtable because the wrapper can
    // outlive the cache, so the stringImpl has to match the wrapper's lifetime.
    stringImpl->ref();
    return wrapper;
}

JSValue jsStringOrNull(ExecState* exec, const String& s)
{
    if (s.isNull())
        return jsNull();
    return jsString(exec, s);
}

JSValue jsOwnedStringOrNull(ExecState* exec, const String& s)
{
    if (s.isNull())
        return jsNull();
    return jsOwnedString(exec, stringToUString(s));
}

JSValue jsStringOrUndefined(ExecState* exec, const String& s)
{
    if (s.isNull())
        return jsUndefined();
    return jsString(exec, s);
}

JSValue jsStringOrFalse(ExecState* exec, const String& s)
{
    if (s.isNull())
        return jsBoolean(false);
    return jsString(exec, s);
}

JSValue jsString(ExecState* exec, const KURL& url)
{
    return jsString(exec, url.string());
}

JSValue jsStringOrNull(ExecState* exec, const KURL& url)
{
    if (url.isNull())
        return jsNull();
    return jsString(exec, url.string());
}

JSValue jsStringOrUndefined(ExecState* exec, const KURL& url)
{
    if (url.isNull())
        return jsUndefined();
    return jsString(exec, url.string());
}

JSValue jsStringOrFalse(ExecState* exec, const KURL& url)
{
    if (url.isNull())
        return jsBoolean(false);
    return jsString(exec, url.string());
}

AtomicStringImpl* findAtomicString(const Identifier& identifier)
{
    if (identifier.isNull())
        return 0;
    StringImpl* impl = identifier.impl();
    ASSERT(impl->existingHash());
    return AtomicString::find(impl->characters(), impl->length(), impl->existingHash());
}

String valueToStringWithNullCheck(ExecState* exec, JSValue value)
{
    if (value.isNull())
        return String();
    return ustringToString(value.toString(exec));
}

String valueToStringWithUndefinedOrNullCheck(ExecState* exec, JSValue value)
{
    if (value.isUndefinedOrNull())
        return String();
    return ustringToString(value.toString(exec));
}

JSValue jsDateOrNull(ExecState* exec, double value)
{
    if (!isfinite(value))
        return jsNull();
    return new (exec) DateInstance(exec, value);
}

double valueToDate(ExecState* exec, JSValue value)
{
    if (value.isNumber())
        return value.uncheckedGetNumber();
    if (!value.inherits(&DateInstance::s_info))
        return std::numeric_limits<double>::quiet_NaN();
    return static_cast<DateInstance*>(value.toObject(exec))->internalNumber();
}

void reportException(ExecState* exec, JSValue exception)
{
    if (exception.isObject() && asObject(exception)->exceptionType() == Terminated)
        return;

    UString errorMessage = exception.toString(exec);
    JSObject* exceptionObject = exception.toObject(exec);
    int lineNumber = exceptionObject->get(exec, Identifier(exec, "line")).toInt32(exec);
    UString exceptionSourceURL = exceptionObject->get(exec, Identifier(exec, "sourceURL")).toString(exec);
    exec->clearException();

    if (ExceptionBase* exceptionBase = toExceptionBase(exception))
        errorMessage = stringToUString(exceptionBase->message() + ": "  + exceptionBase->description());

    ScriptExecutionContext* scriptExecutionContext = static_cast<JSDOMGlobalObject*>(exec->lexicalGlobalObject())->scriptExecutionContext();
    ASSERT(scriptExecutionContext);

    // Crash data indicates null-dereference crashes at this point in the Safari 4 Public Beta.
    // It's harmless to return here without reporting the exception to the log and the debugger in this case.
    if (!scriptExecutionContext)
        return;

    scriptExecutionContext->reportException(ustringToString(errorMessage), lineNumber, ustringToString(exceptionSourceURL), 0);
}

void reportCurrentException(ExecState* exec)
{
    JSValue exception = exec->exception();
    exec->clearException();
    reportException(exec, exception);
}

void setDOMException(ExecState* exec, ExceptionCode ec)
{
    if (!ec || exec->hadException())
        return;

    // FIXME: All callers to setDOMException need to pass in the right global object
    // for now, we're going to assume the lexicalGlobalObject.  Which is wrong in cases like this:
    // frames[0].document.createElement(null, null); // throws an exception which should have the subframes prototypes.
    JSDOMGlobalObject* globalObject = deprecatedGlobalObjectForPrototype(exec);

    ExceptionCodeDescription description;
    getExceptionCodeDescription(ec, description);

    JSValue errorObject;
    switch (description.type) {
        case DOMExceptionType:
            errorObject = toJS(exec, globalObject, DOMCoreException::create(description));
            break;
        case RangeExceptionType:
            errorObject = toJS(exec, globalObject, RangeException::create(description));
            break;
        case EventExceptionType:
            errorObject = toJS(exec, globalObject, EventException::create(description));
            break;
        case XMLHttpRequestExceptionType:
            errorObject = toJS(exec, globalObject, XMLHttpRequestException::create(description));
            break;
#if ENABLE(SVG)
        case SVGExceptionType:
            errorObject = toJS(exec, globalObject, SVGException::create(description).get());
            break;
#endif
#if ENABLE(XPATH)
        case XPathExceptionType:
            errorObject = toJS(exec, globalObject, XPathException::create(description));
            break;
#endif
#if ENABLE(DATABASE)
        case SQLExceptionType:
            errorObject = toJS(exec, globalObject, SQLException::create(description));
            break;
#endif
#if ENABLE(BLOB) || ENABLE(FILE_SYSTEM)
        case FileExceptionType:
            errorObject = toJS(exec, globalObject, FileException::create(description));
            break;
#endif
#if ENABLE(INDEXED_DATABASE)
        case IDBDatabaseExceptionType:
            errorObject = toJS(exec, globalObject, IDBDatabaseException::create(description));
            break;
#endif
    }

    ASSERT(errorObject);
    throwError(exec, errorObject);
}

DOMWindow* activeDOMWindow(ExecState* exec)
{
    return asJSDOMWindow(exec->lexicalGlobalObject())->impl();
}

DOMWindow* firstDOMWindow(ExecState* exec)
{
    return asJSDOMWindow(exec->dynamicGlobalObject())->impl();
}

bool checkNodeSecurity(ExecState* exec, Node* node)
{
    return node && allowsAccessFromFrame(exec, node->document()->frame());
}

bool allowsAccessFromFrame(ExecState* exec, Frame* frame)
{
    if (!frame)
        return false;
    JSDOMWindow* window = toJSDOMWindow(frame, currentWorld(exec));
    return window && window->allowsAccessFrom(exec);
}

bool allowsAccessFromFrame(ExecState* exec, Frame* frame, String& message)
{
    if (!frame)
        return false;
    JSDOMWindow* window = toJSDOMWindow(frame, currentWorld(exec));
    return window && window->allowsAccessFrom(exec, message);
}

void printErrorMessageForFrame(Frame* frame, const String& message)
{
    if (!frame)
        return;
    frame->domWindow()->printErrorMessage(message);
}

// FIXME: We should remove or at least deprecate this function. Callers can use firstDOMWindow directly.
Frame* toDynamicFrame(ExecState* exec)
{
    return firstDOMWindow(exec)->frame();
}

// FIXME: We should remove this function. Callers can use ScriptController directly.
bool processingUserGesture()
{
    return ScriptController::processingUserGesture();
}

JSValue objectToStringFunctionGetter(ExecState* exec, JSValue, const Identifier& propertyName)
{
    return new (exec) JSFunction(exec, exec->lexicalGlobalObject(), exec->lexicalGlobalObject()->functionStructure(), 0, propertyName, objectProtoFuncToString);
}

Structure* getCachedDOMStructure(JSDOMGlobalObject* globalObject, const ClassInfo* classInfo)
{
    JSDOMStructureMap& structures = globalObject->structures();
    return structures.get(classInfo).get();
}

Structure* cacheDOMStructure(JSDOMGlobalObject* globalObject, NonNullPassRefPtr<Structure> structure, const ClassInfo* classInfo)
{
    JSDOMStructureMap& structures = globalObject->structures();
    ASSERT(!structures.contains(classInfo));
    return structures.set(classInfo, structure).first->second.get();
}

JSC::JSObject* toJSSequence(ExecState* exec, JSValue value, unsigned& length)
{
    JSObject* object = value.getObject();
    if (!object) {
        throwTypeError(exec);
        return 0;
    }
    JSValue lengthValue = object->get(exec, exec->propertyNames().length);
    if (exec->hadException())
        return 0;

    if (lengthValue.isUndefinedOrNull()) {
        throwTypeError(exec);
        return 0;
    }

    length = lengthValue.toUInt32(exec);
    if (exec->hadException())
        return 0;

    return object;
}

} // namespace WebCore
