#include "Peer.h"
using namespace std;
using namespace dev;
using namespace dev::p2p;

namespace dev
{

namespace p2p
{

Peer::Peer(Peer const& _original):
    Node(_original),
    m_lastConnected(_original.m_lastConnected),
    m_lastAttempted(_original.m_lastAttempted),
    m_lastDisconnect(_original.m_lastDisconnect),
    m_session(_original.m_session)
{
    m_score = _original.m_score.load();
    m_rating = _original.m_rating.load();
    m_failedAttempts = _original.m_failedAttempts.load();
}

bool Peer::shouldReconnect() const
{
    return id && endpoint && chrono::system_clock::now() > m_lastAttempted + chrono::seconds(fallbackSeconds());
}
    
unsigned Peer::fallbackSeconds() const
{
    if (peerType == PeerType::Required)
        return 5;
    switch (m_lastDisconnect)
    {
    case BadProtocol:
        return 30 * (m_failedAttempts + 1);
    case UselessPeer:
    case TooManyPeers:
        return 25 * (m_failedAttempts + 1);
    case ClientQuit:
        return 15 * (m_failedAttempts + 1);
    case NoDisconnect:
    default:
        if (m_failedAttempts < 5)
            return m_failedAttempts ? m_failedAttempts * 5 : 5;
        else if (m_failedAttempts < 15)
            return 25 + (m_failedAttempts - 5) * 10;
        else
            return 25 + 100 + (m_failedAttempts - 15) * 20;
    }
}

}
}
