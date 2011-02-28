/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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

#include "PluginTest.h"

#include <assert.h>
#include <string.h>

using namespace std;
extern NPNetscapeFuncs *browser;

PluginTest* PluginTest::create(NPP npp, const string& identifier)
{
    if (identifier.empty())
        return new PluginTest(npp, identifier);
        
    CreateTestFunction createTestFunction = createTestFunctions()[identifier];
    if (createTestFunction)
        return createTestFunction(npp, identifier);

    return 0;
}

PluginTest::PluginTest(NPP npp, const string& identifier)
    : m_npp(npp)
    , m_identifier(identifier)
{
}

PluginTest::~PluginTest()
{
}

NPError PluginTest::NPP_New(NPMIMEType pluginType, uint16_t mode, int16_t argc, char *argn[], char *argv[], NPSavedData *saved)
{
    return NPERR_NO_ERROR;
}

NPError PluginTest::NPP_Destroy(NPSavedData**)
{
    return NPERR_NO_ERROR;
}

NPError PluginTest::NPP_DestroyStream(NPStream *stream, NPReason reason)
{
    return NPERR_NO_ERROR;
}

NPError PluginTest::NPP_GetValue(NPPVariable variable, void *value)
{
    // We don't know anything about plug-in values so just return NPERR_GENERIC_ERROR.
    return NPERR_GENERIC_ERROR;
}

NPError PluginTest::NPP_SetWindow(NPP, NPWindow*)
{
    return NPERR_NO_ERROR;
}

int16_t PluginTest::NPP_HandleEvent(void*)
{
    return 0;
}

void PluginTest::NPN_InvalidateRect(NPRect* invalidRect)
{
    browser->invalidaterect(m_npp, invalidRect);
}

NPError PluginTest::NPN_GetURL(const char* url, const char* target)
{
    return browser->geturl(m_npp, url, target);
}

NPIdentifier PluginTest::NPN_GetStringIdentifier(const NPUTF8 *name)
{
    return browser->getstringidentifier(name);
}

NPIdentifier PluginTest::NPN_GetIntIdentifier(int32_t intid)
{
    return browser->getintidentifier(intid);
}

NPError PluginTest::NPN_GetValue(NPNVariable variable, void* value)
{
    return browser->getvalue(m_npp, variable, value);
}

NPObject* PluginTest::NPN_CreateObject(NPClass* npClass)
{
    return browser->createobject(m_npp, npClass);
}                                 

bool PluginTest::NPN_RemoveProperty(NPObject* npObject, NPIdentifier propertyName)
{
    return browser->removeproperty(m_npp, npObject, propertyName);
}

void PluginTest::executeScript(const char* script)
{
    NPObject* windowScriptObject;
    browser->getvalue(m_npp, NPNVWindowNPObject, &windowScriptObject);

    NPString npScript;
    npScript.UTF8Characters = script;
    npScript.UTF8Length = strlen(script);

    NPVariant browserResult;
    browser->evaluate(m_npp, windowScriptObject, &npScript, &browserResult);
    browser->releasevariantvalue(&browserResult);
}

void PluginTest::waitUntilDone()
{
    executeScript("layoutTestController.waitUntilDone()");
}

void PluginTest::notifyDone()
{
    executeScript("layoutTestController.notifyDone()");
}

void PluginTest::registerCreateTestFunction(const string& identifier, CreateTestFunction createTestFunction)
{
    assert(!createTestFunctions().count(identifier));
 
    createTestFunctions()[identifier] = createTestFunction;
}

std::map<std::string, PluginTest::CreateTestFunction>& PluginTest::createTestFunctions()
{
    static std::map<std::string, CreateTestFunction> testFunctions;
    
    return testFunctions;
}
