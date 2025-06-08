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

#ifndef LOBBY_PLAYER_QUEUE_HPP
#define LOBBY_PLAYER_QUEUE_HPP

#include "network/stk_peer.hpp"
#include <cstddef>
#include <set>
#include <deque>
#include <memory>

class STKPeer;

// This class controls the queue of the game.
class LobbyPlayerQueue
{
public:
    typedef std::deque<std::weak_ptr<STKPeer>> PeerQueue;
private:
    static LobbyPlayerQueue* g_instance;

    // the queue of the peers that are always eligible to play.
    // If the peer becomes ineligible, it is immediately removed from the queue
    // back of the queue: cannot play
    // front of the queue: can play
    PeerQueue m_peer_queue;
    // Latest selection of players that
    std::set<STKPeer*> m_cache_spectators_by_limit;
    // Amount of players in game (slots)
    unsigned int m_max_players_in_game;
public:
    LobbyPlayerQueue();
    explicit LobbyPlayerQueue(LobbyPlayerQueue&) = delete;
    explicit LobbyPlayerQueue(LobbyPlayerQueue&&) = delete;
    ~LobbyPlayerQueue() {}

    // Instead of being created and destroyed in the main.cpp,
    // this is managed in the ServerLobby's constructor and destructor.
    static void create();
    static void destroy();
    [[nodiscard]] static LobbyPlayerQueue* get() { return g_instance; }
    //-------------------------------------------------------------------------
    // Server Lobby hooks
    void onPeerJoin(std::shared_ptr<STKPeer>& peer);
    void onPeerLeave(STKPeer* peer);

    // this hook requires reverse lookup O(n)
    void onPeerEligibilityChange(std::shared_ptr<STKPeer> peer,
            PeerEligibility old_value);
    //-------------------------------------------------------------------------
    unsigned int getMaxPlayersInGame() const { return m_max_players_in_game; }
    void setMaxPlayersInGame(unsigned int amount, bool notify = true);

    // prioritize the peer for the game
    void peerToFront(PeerQueue::const_iterator peer);
    // the opposite of peerToFront, the peer is the last in the queue
    void peerToBack(PeerQueue::const_iterator peer);
    bool isSpectatorByLimit(std::shared_ptr<STKPeer>& peer) const;
    std::size_t getQueueSize() const { return m_peer_queue.size(); }
    //-------------------------------------------------------------------------
    // O(n) functions

    // Find the peer in the queue and return its absolute queue number
    std::pair<PeerQueue::const_iterator, unsigned int>
        findPeer(STKPeer* peer, bool reverse = false);
    // Test the peer by the plain pointer instead of the shared pointer
    bool isSpectatorByLimit(STKPeer* peer) const;
    void clearAllExpiredPeers();
    //-------------------------------------------------------------------------

    const std::set<STKPeer*>& getSpectatorsByLimit() const;
private:
    //-------------------------------------------------------------------------
    // Internals

    void updateCachedSpectators();
};

#endif // LOBBY_PLAYER_QUEUE_HPP
