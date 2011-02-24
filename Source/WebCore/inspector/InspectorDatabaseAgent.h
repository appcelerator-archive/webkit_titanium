/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef InspectorDatabaseAgent_h
#define InspectorDatabaseAgent_h

#include "PlatformString.h"
#include <wtf/HashMap.h>
#include <wtf/PassOwnPtr.h>

namespace WebCore {

class Database;
class InspectorArray;
class InspectorDatabaseResource;
class InspectorFrontend;
class InstrumentingAgents;

typedef String ErrorString;

class InspectorDatabaseAgent {
public:
    class FrontendProvider;

    static PassOwnPtr<InspectorDatabaseAgent> create(InstrumentingAgents* instrumentingAgents)
    {
        return adoptPtr(new InspectorDatabaseAgent(instrumentingAgents));
    }
    ~InspectorDatabaseAgent();

    void setFrontend(InspectorFrontend*);
    void clearFrontend();

    void clearResources();

    // Called from the front-end.
    void getDatabaseTableNames(ErrorString* error, long databaseId, RefPtr<InspectorArray>* names);
    void executeSQL(ErrorString* error, long databaseId, const String& query, bool* success, long* transactionId);

    // Called from the injected script.
    long databaseId(Database*);

    void didOpenDatabase(PassRefPtr<Database>, const String& domain, const String& name, const String& version);
private:
    explicit InspectorDatabaseAgent(InstrumentingAgents*);

    Database* databaseForId(long databaseId);

    InstrumentingAgents* m_instrumentingAgents;
    typedef HashMap<int, RefPtr<InspectorDatabaseResource> > DatabaseResourcesMap;
    DatabaseResourcesMap m_resources;
    RefPtr<FrontendProvider> m_frontendProvider;
};

} // namespace WebCore

#endif // !defined(InspectorDatabaseAgent_h)
