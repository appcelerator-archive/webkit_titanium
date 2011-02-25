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

#include "config.h"
#include "Connection.h"

#include "BinarySemaphore.h"
#include "CoreIPCMessageKinds.h"
#include "RunLoop.h"
#include "WorkItem.h"
#include <wtf/CurrentTime.h>

using namespace std;

namespace CoreIPC {

class Connection::SyncMessageState : public RefCounted<Connection::SyncMessageState> {
public:
    static PassRefPtr<SyncMessageState> getOrCreate(RunLoop*);
    ~SyncMessageState();

    void beginWaitForSyncReply();
    void endWaitForSyncReply();

    void wakeUpClientRunLoop()
    {
        m_waitForSyncReplySemaphore.signal();
    }

    bool wait(double absoluteTime)
    {
        return m_waitForSyncReplySemaphore.wait(absoluteTime);
    }

    // Returns true if this message will be handled on a client thread that is currently
    // waiting for a reply to a synchronous message.
    bool processIncomingMessage(Connection*, IncomingMessage&);

    void dispatchMessages();

private:
    explicit SyncMessageState(RunLoop*);

    typedef HashMap<RunLoop*, SyncMessageState*> SyncMessageStateMap;
    static SyncMessageStateMap& syncMessageStateMap()
    {
        DEFINE_STATIC_LOCAL(SyncMessageStateMap, syncMessageStateMap, ());
        return syncMessageStateMap;
    }

    static Mutex& syncMessageStateMapMutex()
    {
        DEFINE_STATIC_LOCAL(Mutex, syncMessageStateMapMutex, ());
        return syncMessageStateMapMutex;
    }

    RunLoop* m_runLoop;
    BinarySemaphore m_waitForSyncReplySemaphore;

    // Protects m_waitForSyncReplyCount and m_messagesToDispatchWhileWaitingForSyncReply.
    Mutex m_mutex;

    unsigned m_waitForSyncReplyCount;

    struct ConnectionAndIncomingMessage {
        Connection* connection;
        IncomingMessage incomingMessage;
    };
    Vector<ConnectionAndIncomingMessage> m_messagesToDispatchWhileWaitingForSyncReply;
};

PassRefPtr<Connection::SyncMessageState> Connection::SyncMessageState::getOrCreate(RunLoop* runLoop)
{
    MutexLocker locker(syncMessageStateMapMutex());
    pair<SyncMessageStateMap::iterator, bool> result = syncMessageStateMap().add(runLoop, 0);

    if (!result.second) {
        ASSERT(result.first->second);
        return result.first->second;
    }

    RefPtr<SyncMessageState> syncMessageState = adoptRef(new SyncMessageState(runLoop));
    result.first->second = syncMessageState.get();

    return syncMessageState.release();
}

Connection::SyncMessageState::SyncMessageState(RunLoop* runLoop)
    : m_runLoop(runLoop)
    , m_waitForSyncReplyCount(0)
{
}

Connection::SyncMessageState::~SyncMessageState()
{
    MutexLocker locker(syncMessageStateMapMutex());
    
    ASSERT(syncMessageStateMap().contains(m_runLoop));
    syncMessageStateMap().remove(m_runLoop);
}

void Connection::SyncMessageState::beginWaitForSyncReply()
{
    ASSERT(RunLoop::current() == m_runLoop);

    MutexLocker locker(m_mutex);
    m_waitForSyncReplyCount++;
}

void Connection::SyncMessageState::endWaitForSyncReply()
{
    ASSERT(RunLoop::current() == m_runLoop);

    MutexLocker locker(m_mutex);
    ASSERT(m_waitForSyncReplyCount);
    --m_waitForSyncReplyCount;

    if (m_waitForSyncReplyCount)
        return;

    // Dispatch any remaining incoming sync messages.
    for (size_t i = 0; i < m_messagesToDispatchWhileWaitingForSyncReply.size(); ++i) {
        ConnectionAndIncomingMessage& connectionAndIncomingMessage = m_messagesToDispatchWhileWaitingForSyncReply[i];
        connectionAndIncomingMessage.connection->enqueueIncomingMessage(connectionAndIncomingMessage.incomingMessage);
    }

    m_messagesToDispatchWhileWaitingForSyncReply.clear();
}

bool Connection::SyncMessageState::processIncomingMessage(Connection* connection, IncomingMessage& incomingMessage)
{
    MessageID messageID = incomingMessage.messageID();
    if (!messageID.isSync() && !messageID.shouldDispatchMessageWhenWaitingForSyncReply())
        return false;

    MutexLocker locker(m_mutex);
    if (!m_waitForSyncReplyCount)
        return false;

    ConnectionAndIncomingMessage connectionAndIncomingMessage;
    connectionAndIncomingMessage.connection = connection;
    connectionAndIncomingMessage.incomingMessage = incomingMessage;

    m_messagesToDispatchWhileWaitingForSyncReply.append(connectionAndIncomingMessage);
    wakeUpClientRunLoop();

    return true;
}

void Connection::SyncMessageState::dispatchMessages()
{
    ASSERT(m_runLoop == RunLoop::current());

    Vector<ConnectionAndIncomingMessage> messagesToDispatchWhileWaitingForSyncReply;

    {
        MutexLocker locker(m_mutex);
        m_messagesToDispatchWhileWaitingForSyncReply.swap(messagesToDispatchWhileWaitingForSyncReply);
    }

    for (size_t i = 0; i < messagesToDispatchWhileWaitingForSyncReply.size(); ++i) {
        ConnectionAndIncomingMessage& connectionAndIncomingMessage = messagesToDispatchWhileWaitingForSyncReply[i];
        connectionAndIncomingMessage.connection->dispatchMessage(connectionAndIncomingMessage.incomingMessage);
    }
}

PassRefPtr<Connection> Connection::createServerConnection(Identifier identifier, Client* client, RunLoop* clientRunLoop)
{
    return adoptRef(new Connection(identifier, true, client, clientRunLoop));
}

PassRefPtr<Connection> Connection::createClientConnection(Identifier identifier, Client* client, RunLoop* clientRunLoop)
{
    return adoptRef(new Connection(identifier, false, client, clientRunLoop));
}

Connection::Connection(Identifier identifier, bool isServer, Client* client, RunLoop* clientRunLoop)
    : m_client(client)
    , m_isServer(isServer)
    , m_syncRequestID(0)
    , m_didCloseOnConnectionWorkQueueCallback(0)
    , m_isConnected(false)
    , m_connectionQueue("com.apple.CoreIPC.ReceiveQueue")
    , m_clientRunLoop(clientRunLoop)
    , m_inDispatchMessageCount(0)
    , m_didReceiveInvalidMessage(false)
    , m_syncMessageState(SyncMessageState::getOrCreate(clientRunLoop))
    , m_shouldWaitForSyncReplies(true)
{
    ASSERT(m_client);

    platformInitialize(identifier);
}

Connection::~Connection()
{
    ASSERT(!isValid());

    m_connectionQueue.invalidate();
}

void Connection::setDidCloseOnConnectionWorkQueueCallback(DidCloseOnConnectionWorkQueueCallback callback)
{
    ASSERT(!m_isConnected);

    m_didCloseOnConnectionWorkQueueCallback = callback;    
}

void Connection::invalidate()
{
    if (!isValid()) {
        // Someone already called invalidate().
        return;
    }
    
    // Reset the client.
    m_client = 0;

    m_connectionQueue.scheduleWork(WorkItem::create(this, &Connection::platformInvalidate));
}

void Connection::markCurrentlyDispatchedMessageAsInvalid()
{
    // This should only be called while processing a message.
    ASSERT(m_inDispatchMessageCount > 0);

    m_didReceiveInvalidMessage = true;
}

PassOwnPtr<ArgumentEncoder> Connection::createSyncMessageArgumentEncoder(uint64_t destinationID, uint64_t& syncRequestID)
{
    OwnPtr<ArgumentEncoder> argumentEncoder = ArgumentEncoder::create(destinationID);

    // Encode the sync request ID.
    syncRequestID = ++m_syncRequestID;
    argumentEncoder->encode(syncRequestID);

    return argumentEncoder.release();
}

bool Connection::sendMessage(MessageID messageID, PassOwnPtr<ArgumentEncoder> arguments, unsigned messageSendFlags)
{
    if (!isValid())
        return false;

    if (messageSendFlags & DispatchMessageEvenWhenWaitingForSyncReply)
        messageID = messageID.messageIDWithAddedFlags(MessageID::DispatchMessageWhenWaitingForSyncReply);

    MutexLocker locker(m_outgoingMessagesLock);
    m_outgoingMessages.append(OutgoingMessage(messageID, arguments));
    
    // FIXME: We should add a boolean flag so we don't call this when work has already been scheduled.
    m_connectionQueue.scheduleWork(WorkItem::create(this, &Connection::sendOutgoingMessages));
    return true;
}

bool Connection::sendSyncReply(PassOwnPtr<ArgumentEncoder> arguments)
{
    return sendMessage(MessageID(CoreIPCMessage::SyncMessageReply), arguments);
}

PassOwnPtr<ArgumentDecoder> Connection::waitForMessage(MessageID messageID, uint64_t destinationID, double timeout)
{
    // First, check if this message is already in the incoming messages queue.
    {
        MutexLocker locker(m_incomingMessagesLock);

        for (size_t i = 0; i < m_incomingMessages.size(); ++i) {
            const IncomingMessage& message = m_incomingMessages[i];

            if (message.messageID() == messageID && message.arguments()->destinationID() == destinationID) {
                OwnPtr<ArgumentDecoder> arguments(message.arguments());
                
                // Erase the incoming message.
                m_incomingMessages.remove(i);
                return arguments.release();
            }
        }
    }
    
    double absoluteTime = currentTime() + timeout;
    
    std::pair<unsigned, uint64_t> messageAndDestination(std::make_pair(messageID.toInt(), destinationID));
    
    {
        MutexLocker locker(m_waitForMessageMutex);

        // We don't support having multiple clients wait for the same message.
        ASSERT(!m_waitForMessageMap.contains(messageAndDestination));
    
        // Insert our pending wait.
        m_waitForMessageMap.set(messageAndDestination, 0);
    }
    
    // Now wait for it to be set.
    while (true) {
        MutexLocker locker(m_waitForMessageMutex);

        HashMap<std::pair<unsigned, uint64_t>, ArgumentDecoder*>::iterator it = m_waitForMessageMap.find(messageAndDestination);
        if (it->second) {
            OwnPtr<ArgumentDecoder> arguments(it->second);
            m_waitForMessageMap.remove(it);
            
            return arguments.release();
        }
        
        // Now we wait.
        if (!m_waitForMessageCondition.timedWait(m_waitForMessageMutex, absoluteTime)) {
            // We timed out, now remove the pending wait.
            m_waitForMessageMap.remove(messageAndDestination);

            break;
        }
    }
    
    return PassOwnPtr<ArgumentDecoder>();
}

PassOwnPtr<ArgumentDecoder> Connection::sendSyncMessage(MessageID messageID, uint64_t syncRequestID, PassOwnPtr<ArgumentEncoder> encoder, double timeout)
{
    // We only allow sending sync messages from the client run loop.
    ASSERT(RunLoop::current() == m_clientRunLoop);

    if (!isValid())
        return 0;
    
    // Push the pending sync reply information on our stack.
    {
        MutexLocker locker(m_syncReplyStateMutex);
        if (!m_shouldWaitForSyncReplies) {
            m_client->didFailToSendSyncMessage(this);
            return 0;
        }

        m_pendingSyncReplies.append(PendingSyncReply(syncRequestID));
    }

    // We have to begin waiting for the sync reply before sending the message, in case the other side
    // would have sent a request before us, which would lead to a deadlock.
    m_syncMessageState->beginWaitForSyncReply();

    // First send the message.
    sendMessage(messageID, encoder);

    // Then wait for a reply. Waiting for a reply could involve dispatching incoming sync messages, so
    // keep an extra reference to the connection here in case it's invalidated.
    RefPtr<Connection> protect(this);
    OwnPtr<ArgumentDecoder> reply = waitForSyncReply(syncRequestID, timeout);

    // Finally, pop the pending sync reply information.
    {
        MutexLocker locker(m_syncReplyStateMutex);
        ASSERT(m_pendingSyncReplies.last().syncRequestID == syncRequestID);
        m_pendingSyncReplies.removeLast();
    }

    m_syncMessageState->endWaitForSyncReply();

    if (!reply)
        m_client->didFailToSendSyncMessage(this);

    return reply.release();
}

PassOwnPtr<ArgumentDecoder> Connection::waitForSyncReply(uint64_t syncRequestID, double timeout)
{
    double absoluteTime = currentTime() + timeout;

    bool timedOut = false;
    while (!timedOut) {
        // First, check if we have any messages that we need to process.
        m_syncMessageState->dispatchMessages();
        
        {
            MutexLocker locker(m_syncReplyStateMutex);

            // Second, check if there is a sync reply at the top of the stack.
            ASSERT(!m_pendingSyncReplies.isEmpty());
            
            PendingSyncReply& pendingSyncReply = m_pendingSyncReplies.last();
            ASSERT(pendingSyncReply.syncRequestID == syncRequestID);
            
            // We found the sync reply, or the connection was closed.
            if (pendingSyncReply.didReceiveReply || !m_shouldWaitForSyncReplies)
                return pendingSyncReply.releaseReplyDecoder();
        }

        // We didn't find a sync reply yet, keep waiting.
        timedOut = !m_syncMessageState->wait(absoluteTime);
    }

    // We timed out.
    return 0;
}

void Connection::processIncomingMessage(MessageID messageID, PassOwnPtr<ArgumentDecoder> arguments)
{
    // Check if this is a sync reply.
    if (messageID == MessageID(CoreIPCMessage::SyncMessageReply)) {
        MutexLocker locker(m_syncReplyStateMutex);
        ASSERT(!m_pendingSyncReplies.isEmpty());

        PendingSyncReply& pendingSyncReply = m_pendingSyncReplies.last();
        ASSERT(pendingSyncReply.syncRequestID == arguments->destinationID());

        pendingSyncReply.replyDecoder = arguments.leakPtr();
        pendingSyncReply.didReceiveReply = true;
        m_syncMessageState->wakeUpClientRunLoop();
        return;
    }

    IncomingMessage incomingMessage(messageID, arguments);

    // Check if this is a sync message or if it's a message that should be dispatched even when waiting for
    // a sync reply. If it is, and we're waiting for a sync reply this message needs to be dispatched.
    // If we don't we'll end up with a deadlock where both sync message senders are stuck waiting for a reply.
    if (m_syncMessageState->processIncomingMessage(this, incomingMessage))
        return;

    // Check if we're waiting for this message.
    {
        MutexLocker locker(m_waitForMessageMutex);
        
        HashMap<std::pair<unsigned, uint64_t>, ArgumentDecoder*>::iterator it = m_waitForMessageMap.find(std::make_pair(messageID.toInt(), incomingMessage.destinationID()));
        if (it != m_waitForMessageMap.end()) {
            it->second = incomingMessage.releaseArguments().leakPtr();
            ASSERT(it->second);
        
            m_waitForMessageCondition.signal();
            return;
        }
    }

    enqueueIncomingMessage(incomingMessage);
}

void Connection::connectionDidClose()
{
    // The connection is now invalid.
    platformInvalidate();

    {
        MutexLocker locker(m_syncReplyStateMutex);

        ASSERT(m_shouldWaitForSyncReplies);
        m_shouldWaitForSyncReplies = false;

        if (!m_pendingSyncReplies.isEmpty())
            m_syncMessageState->wakeUpClientRunLoop();
    }

    if (m_didCloseOnConnectionWorkQueueCallback)
        m_didCloseOnConnectionWorkQueueCallback(m_connectionQueue, this);

    m_clientRunLoop->scheduleWork(WorkItem::create(this, &Connection::dispatchConnectionDidClose));
}

void Connection::dispatchConnectionDidClose()
{
    // If the connection has been explicitly invalidated before dispatchConnectionDidClose was called,
    // then the client will be null here.
    if (!m_client)
        return;


    // Because we define a connection as being "valid" based on wheter it has a null client, we null out
    // the client before calling didClose here. Otherwise, sendSync will try to send a message to the connection and
    // will then wait indefinitely for a reply.
    Client* client = m_client;
    m_client = 0;
    
    client->didClose(this);
}

bool Connection::canSendOutgoingMessages() const
{
    return m_isConnected && platformCanSendOutgoingMessages();
}

void Connection::sendOutgoingMessages()
{
    if (!canSendOutgoingMessages())
        return;

    while (true) {
        OutgoingMessage message;
        {
            MutexLocker locker(m_outgoingMessagesLock);
            if (m_outgoingMessages.isEmpty())
                break;
            message = m_outgoingMessages.takeFirst();
        }

        if (!sendOutgoingMessage(message.messageID(), adoptPtr(message.arguments())))
            break;
    }
}

void Connection::dispatchSyncMessage(MessageID messageID, ArgumentDecoder* arguments)
{
    ASSERT(messageID.isSync());

    // Decode the sync request ID.
    uint64_t syncRequestID = 0;

    if (!arguments->decodeUInt64(syncRequestID) || !syncRequestID) {
        // We received an invalid sync message.
        arguments->markInvalid();
        return;
    }

    // Create our reply encoder.
    ArgumentEncoder* replyEncoder = ArgumentEncoder::create(syncRequestID).leakPtr();
    
    // Hand off both the decoder and encoder to the client..
    SyncReplyMode syncReplyMode = m_client->didReceiveSyncMessage(this, messageID, arguments, replyEncoder);

    // FIXME: If the message was invalid, we should send back a SyncMessageError.
    ASSERT(!arguments->isInvalid());

    if (syncReplyMode == ManualReply) {
        // The client will take ownership of the reply encoder and send it at some point in the future.
        // We won't do anything here.
        return;
    }

    // Send the reply.
    sendSyncReply(replyEncoder);
}

void Connection::enqueueIncomingMessage(IncomingMessage& incomingMessage)
{
    MutexLocker locker(m_incomingMessagesLock);
    m_incomingMessages.append(incomingMessage);

    m_clientRunLoop->scheduleWork(WorkItem::create(this, &Connection::dispatchMessages));
}

void Connection::dispatchMessage(IncomingMessage& message)
{
    OwnPtr<ArgumentDecoder> arguments = message.releaseArguments();

    // If there's no client, return. We do this after calling releaseArguments so that
    // the ArgumentDecoder message will be freed.
    if (!m_client)
        return;

    m_inDispatchMessageCount++;

    bool oldDidReceiveInvalidMessage = m_didReceiveInvalidMessage;
    m_didReceiveInvalidMessage = false;

    if (message.messageID().isSync())
        dispatchSyncMessage(message.messageID(), arguments.get());
    else
        m_client->didReceiveMessage(this, message.messageID(), arguments.get());

    m_didReceiveInvalidMessage |= arguments->isInvalid();
    m_inDispatchMessageCount--;

    if (m_didReceiveInvalidMessage && m_client)
        m_client->didReceiveInvalidMessage(this, message.messageID());

    m_didReceiveInvalidMessage = oldDidReceiveInvalidMessage;
}

void Connection::dispatchMessages()
{
    Vector<IncomingMessage> incomingMessages;
    
    {
        MutexLocker locker(m_incomingMessagesLock);
        m_incomingMessages.swap(incomingMessages);
    }

    for (size_t i = 0; i < incomingMessages.size(); ++i)
        dispatchMessage(incomingMessages[i]);
}

} // namespace CoreIPC
