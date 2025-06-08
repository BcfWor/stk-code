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

#include "stk_command_context.hpp"

#include "network/server_config.hpp"
#include "network/stk_peer.hpp"
#include "network/network_player_profile.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/network_string.hpp"
#include "utils/string_utils.hpp"
#include <cstdio>
#include <iostream>

STKCommandContext::~STKCommandContext()
{
    m_response_buffer.clear();
}

void STKCommandContext::write(const char* data, std::size_t n)
{
    m_response_buffer << data;
}
void STKCommandContext::write(const std::string& data)
{
    m_response_buffer << data;
}
void STKCommandContext::vnprintf(const char* format, std::size_t n, va_list args)
{
    char nullterm_buffer[2049];
    nullterm_buffer[2048] = 0;
    if (n > 2048)
        n = 2048;

    int size = std::vsnprintf(nullterm_buffer, n, format, args);
    if (size)
        m_response_buffer << nullterm_buffer;
}
void STKCommandContext::vnprintf(const std::string& format, std::size_t n, va_list args)
{
    vnprintf(format.c_str(), n, args);
}
void STKCommandContext::flush()
{
    std::string str = m_response_buffer.str();

    // remove the trailing newline from the response
    if (str[str.size() - 1] == '\n')
    {
        str.erase(str.size() - 1);
    }

    if (m_response_buffer.str().empty())
        return;

    if (!m_peer)
    {
        std::cout << str << std::endl << std::flush;
    }
    else
    {
        NetworkString* msg = m_lobby->getNetworkString();
        msg->addUInt8(LobbyProtocol::LE_CHAT);
        msg->setSynchronous(true);
        msg->encodeString16(StringUtils::utf8ToWide(str));
        m_peer->sendPacket(msg, true/*reliable*/);
        delete msg;
    }
    m_response_buffer.clear();
    m_response_buffer.str(std::string());
}

void STKCommandContext::set_peer(STKPeer* const peer)
{
    m_peer = peer;

    if (m_peer && m_peer->hasPlayerProfiles())
        m_player = m_peer->getPlayerProfiles()[0].get();
}
bool STKCommandContext::testPermission(const ServerPermissionLevel lvl)
{
    if (!m_peer) return true;

    if (!m_peer->hasPlayerProfiles() || m_peer->getPermissionLevel() < lvl)
    {
        sendNoPermission();
        return false;
    }
    return true;
}
ServerPermissionLevel STKCommandContext::getPermissionLevel() const
{
    // Console has ownership permission
    if (!m_peer) return PERM_OWNER;

    if (ServerConfig::m_server_owner > 0 &&
            m_peer->hasPlayerProfiles() &&
            ServerConfig::m_server_owner == m_peer->getPlayerProfiles()[0]->getOnlineId())
        return PERM_OWNER;

    if (!m_peer->hasPlayerProfiles())
        return PERM_NONE;

    return (ServerPermissionLevel) m_peer->getPermissionLevel();
}
ServerPermissionLevel STKCommandContext::getVeto() const
{
    // Console has ownership permission
    if (!m_peer) return PERM_OWNER;

    if (!m_peer->hasPlayerProfiles())
        return PERM_NONE;

    return (ServerPermissionLevel) m_peer->getVeto();
}

bool STKCommandContext::hasRestriction(const PlayerRestriction flag)
{
    if (!m_peer) return false;
    return m_peer->hasPlayerProfiles() && m_peer->hasRestriction(flag);
}

std::shared_ptr<NetworkPlayerProfile> STKCommandContext::getProfile()
{
    if (m_peer && m_peer->hasPlayerProfiles())
        return m_peer->getPlayerProfiles()[0];
    return nullptr;
}
std::string STKCommandContext::getProfileName()
{
    if (!m_peer)
        return "@server";
    if (!m_peer->hasPlayerProfiles())
        return "(unknown)";
    return StringUtils::wideToUtf8(getProfile()->getName());
}

bool STKCommandContext::isCrowned() const
{
    return get_lobby()->m_server_owner.lock().get() == m_peer;
}
void STKCommandContext::sendNoPermission()
{
    write("You are not allowed to run this command.");
    flush();
}

bool STKCommandContext::testPlayerOnly()
{
    if (m_peer)
        return true;

    write("This command is only for players.");
    flush();
    return false;
}

bool STKCommandContext::testConsoleOnly()
{
    if (!m_peer)
        return true;

    write("This command is only for the host.");
    flush();
    return false;
}
