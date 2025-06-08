//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2013-2015 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "player_queue.hpp"
#include "network/protocols/lobby_protocol.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/log.hpp"
#include <memory>

static const char* LOGNAME = "LobbyPlayerQueue";

LobbyPlayerQueue* LobbyPlayerQueue::g_instance = nullptr;

LobbyPlayerQueue::LobbyPlayerQueue()
{
    m_max_players_in_game = ServerConfig::m_max_players_in_game;
}

void LobbyPlayerQueue::create()
{
    assert(!g_instance);

    g_instance = new LobbyPlayerQueue();
    Log::verbose(LOGNAME, "Created instance");
}

void LobbyPlayerQueue::destroy()
{
    assert(g_instance);

    delete g_instance;
    Log::verbose(LOGNAME, "Destroyed instance");
}

void LobbyPlayerQueue::onPeerJoin(std::shared_ptr<STKPeer>& peer)
{
    // check eligibility and add the player to the queue
    peer->testEligibility();
    if (peer->isEligibleForGame())
    {
        m_peer_queue.push_back(peer);
    }
    else {
        Log::verbose(LOGNAME, "Peer %d is not eligible for the queue.", peer->getHostId());
    }
    updateCachedSpectators();
}
void LobbyPlayerQueue::onPeerLeave(STKPeer* peer)
{
    // Delete itself from the queue and update the cache of spectators
    auto int_peer = findPeer(peer);

    // If the peer was not eligible for the game or is not present in the queue
    if (int_peer.first == m_peer_queue.cend())
    {
        Log::verbose(LOGNAME, "Peer %d was not in the queue.", peer->getHostId());
        return;
    }

    // remove the peer from the queue
    m_peer_queue.erase(int_peer.first);

    updateCachedSpectators();
}
void LobbyPlayerQueue::clearAllExpiredPeers()
{
    unsigned int counter = 0;
    for (auto it = m_peer_queue.begin();
            it != m_peer_queue.end();)
    {
        if (it->expired())
        {
            it = m_peer_queue.erase(it);
            counter++;
        }
        else
            it++;
    }

    Log::verbose(LOGNAME, "Cleared %d expired peers.", counter);
}
void LobbyPlayerQueue::onPeerEligibilityChange(std::shared_ptr<STKPeer> const peer,
        const PeerEligibility old_value)
{
    const PeerEligibility eligibility = peer->getEligibility();
    // hook whenever the eligibility factors change
    if (old_value != eligibility)
    {
        if (peer->isEligibleForGame())
        {
            // add the player to the queue (assume it hasn't been added before)
            m_peer_queue.push_back(peer);
            Log::verbose(LOGNAME, "Peer %d added to the queue.", peer->getHostId());
        }
        else
        {
            // remove the player from the queue
            auto peer_it = findPeer(peer.get());
            if (peer_it.first != m_peer_queue.cend())
            {
                m_peer_queue.erase(peer_it.first);
                Log::verbose(LOGNAME, "Peer %d removed from the queue.", peer->getHostId());
            }
        }
        updateCachedSpectators();
        return;
    }
}

void LobbyPlayerQueue::setMaxPlayersInGame(
        const unsigned int amount, const bool notify)
{
    m_max_players_in_game = amount;
    // update the queue
    updateCachedSpectators();

    std::shared_ptr<ServerLobby> const lobby = 
        LobbyProtocol::get<ServerLobby>();

    if (lobby)
        lobby->updatePlayerList();

    if (notify)
    {
        std::stringstream ss;
        ss << "The number of slots has been changed to " << amount << ".";
        lobby->sendStringToAllPeers(ss.str());
    }
}
// the peer has been found with the findPeer, after which it can be moved
void LobbyPlayerQueue::peerToFront(PeerQueue::const_iterator peer_it)
{
    std::weak_ptr<STKPeer> peer = *peer_it;

    m_peer_queue.erase(peer_it); // peer_it is invalid

    m_peer_queue.push_front(peer);
}
void LobbyPlayerQueue::peerToBack(PeerQueue::const_iterator peer_it)
{
    std::weak_ptr<STKPeer> peer = *peer_it;

    m_peer_queue.erase(peer_it); // peer_it is invalid

    m_peer_queue.push_back(peer);
}
bool LobbyPlayerQueue::isSpectatorByLimit(STKPeer* const peer) const
{
    return m_cache_spectators_by_limit.find(peer) != m_cache_spectators_by_limit.cend();
}
bool LobbyPlayerQueue::isSpectatorByLimit(std::shared_ptr<STKPeer>& peer) const
{
    return m_cache_spectators_by_limit.find(peer.get()) != m_cache_spectators_by_limit.cend();
}
std::pair<LobbyPlayerQueue::PeerQueue::const_iterator, unsigned int>
    LobbyPlayerQueue::findPeer(STKPeer* const peer, const bool rev)
{
    if (!peer->isEligibleForGame())
        return std::make_pair(m_peer_queue.cend(), 0);

    // linear search

    if (rev)
    {
        for (std::size_t i = m_peer_queue.size() - 1;
                i >= 0; i--)
        {
            if (m_peer_queue[i].expired()) continue;
            if (m_peer_queue[i].lock()->isSamePeer(peer))
            {
                // found
                assert((*(m_peer_queue.cbegin() + i)).lock().get() == peer);
                return std::make_pair(m_peer_queue.cbegin() + i, i);
            }
            if (i != 0)
                continue;
        }
    }
    else
    {
        for (std::size_t i = 0;
                i < m_peer_queue.size(); i++)
        {
            if (m_peer_queue[i].expired())
            {
                continue;
            };
            if (!m_peer_queue[i].lock()->isSamePeer(peer))
                continue;
            // found
            assert((*(m_peer_queue.cbegin() + i)).lock().get() == peer);
            return std::make_pair(m_peer_queue.cbegin() + i, i);
        }
    }
    // peer wasn't able to play in the first place.
    return std::make_pair(m_peer_queue.cend(), 0);
}

const std::set<STKPeer*>& LobbyPlayerQueue::getSpectatorsByLimit() const
{
    return m_cache_spectators_by_limit;
}

void LobbyPlayerQueue::updateCachedSpectators()
{
    // iterate in reverse and count the profiles of the peer,
    // when the amount is exceeded, proceed to further add more peers
    m_cache_spectators_by_limit.clear();

    unsigned int profile_counter = 0;
    auto it = m_peer_queue.crbegin();
    // iterating from the head of the queue, the players that are on the front
    // have their profile size summed up.
    for (; profile_counter < m_max_players_in_game && it != m_peer_queue.crend();
            it++)
    {
        auto peer = it->lock();
        if (!peer->hasPlayerProfiles())
            continue;
        
        const std::size_t player_count = peer->getPlayerProfiles().size();
        if (profile_counter + player_count > m_max_players_in_game)
            break;
        profile_counter += player_count;
    }
    if (it == m_peer_queue.crend())
        // No players in the queue are exceeding the limit
        return;

    for (; it != m_peer_queue.crend(); it++)
    {
        // the rest of the queue are the players that are under limit
        m_cache_spectators_by_limit.insert(it->lock().get());
    }
}
