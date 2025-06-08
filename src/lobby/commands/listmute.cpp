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

#include "listmute.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ListmuteCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    ServerLobbyCommands::DispatchData* const dispatch_data =
        reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);
    if (!data) return false;

    auto parser = ctx->get_parser();
    parser->parse_finish(); // Do not allow more arguments

    if (!stk_ctx->testPlayerOnly()) return false;
    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();

    auto& peer_wkptr = dispatch_data->m_peer_wkptr;
    auto& mutes = lobby->m_peers_muted_players[peer_wkptr];

    if (mutes.empty())
    {
        ctx->write("You're not ignoring any players.");
    }
    else
    {
        for (auto& name : mutes)
        {
            ctx->write(StringUtils::wideToUtf8(name));
            ctx->write(" ");
        }
        ctx->write("muted");
    }
    ctx->flush();
    return true;
}
