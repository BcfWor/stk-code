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

#include "scanservers.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "utils/time.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ScanServersCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    
    // for creating a weak reference to the peer and passing it
    ServerLobbyCommands::DispatchData* const dispatch_data =
        reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);
    if (!data)
        return false;

    auto parser = ctx->get_parser();
    parser->parse_finish();

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();

    uint64_t now = StkTime::getMonoTimeMs();
    // first, check if the timeout has not ran out yet
    if (stk_ctx->get_peer() && m_last_wanrefresh_cmd_time &&
            m_last_wanrefresh_cmd_time + (uint64_t)(ServerConfig::m_check_servers_cooldown * 1000.0f)
            > now)
    {
        ctx->write("Someone has already used the command. Please wait before doing it again.");
        ctx->flush();
        return false;
    }

    // then, set current time
    m_last_wanrefresh_cmd_time = now;
    // and current sender: friend class
    lobby->m_last_wanrefresh_requester = dispatch_data->m_peer_wkptr;
    lobby->m_last_wanrefresh_is_peer.store(stk_ctx->get_peer());
    // and, create a request
    lobby->m_last_wanrefresh_res = ServersManager::get()->getWANRefreshRequest();
    
    ctx->write("Fetching, please wait...");
    ctx->flush();
    return true;
}
