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

#ifndef LOBBY_STK_COMMAND_CONTEXT_HPP
#define LOBBY_STK_COMMAND_CONTEXT_HPP

// This file defines a command context class to be used in ServerLobby::handleServerCommand
// and the network console.

#include <memory>
#include <mutex>
#include <sstream>
#include <context.hpp>
#include "network/moderation_toolkit/player_restriction.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
#include "network/network_player_profile.hpp"
#include "utils/cpp2011.hpp"

class ServerLobby;
class ServerLobbyCommands;
class STKPeer;

// An instance of this class is associated with the STKPeer and
// every time peer runs the command in the server lobby,
// or when network console runs the command in the stdout
// context override with its instance is used instead of the context factory.
//
// The single instance of STKCommandContext is shared among different times
// the command is executed.
class STKCommandContext : public nnwcli::CommandExecutorContext
{
private:
    std::mutex m_lock; // for if the per-player command dispatching ever gets multithreaded
    ServerLobby* m_lobby;
    ServerLobbyCommands* m_lobby_commands;
    STKPeer* m_peer; // when nullptr, the command is dispatched in the network console.
    NetworkPlayerProfile* m_player;

    std::stringstream m_response_buffer;
public:

    STKCommandContext() : m_lobby(nullptr), m_peer(nullptr) {}
    ~STKCommandContext();

    std::mutex& get_lock();

    ServerLobby* get_lobby() const { return m_lobby; }
    ServerLobbyCommands* get_lobby_commands() const { return m_lobby_commands; }
    void set_lobby(ServerLobby* const lobby) { m_lobby = lobby; }
    void set_lobby_commands(ServerLobbyCommands* const lobby_cmds) { m_lobby_commands = lobby_cmds; }

    STKPeer* get_peer() const { return m_peer; }
    void set_peer(STKPeer* const peer);

    //------------------------------------------------------------------------------
    std::stringstream& get_response_buffer()
    { return m_response_buffer; }
    //------------------------------------------------------------------------------
    virtual void write(const char* data, std::size_t n) OVERRIDE;
    virtual void write(const std::string& data) OVERRIDE;
    virtual void vnprintf(const char* format, std::size_t n, va_list args) OVERRIDE;
    virtual void vnprintf(const std::string& format, std::size_t n, va_list args) OVERRIDE;
    virtual void flush() OVERRIDE;
    //------------------------------------------------------------------------------
    bool testPermission(ServerPermissionLevel lvl);
    ServerPermissionLevel getPermissionLevel() const;
    ServerPermissionLevel getVeto() const;
    bool hasRestriction(PlayerRestriction flag);
    std::shared_ptr<NetworkPlayerProfile> getProfile();
    std::string getProfileName();
    bool isCrowned() const;
    //------------------------------------------------------------------------------
    void sendNoPermission();
    bool testPlayerOnly();
    bool testConsoleOnly();
};

#endif // LOBBY_STK_COMMAND_CONTEXT_HPP
