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

#include "karts.hpp"
#include "tracks.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>

static
bool lobby_assets_command(
        nnwcli::CommandExecutorContext* const ctx, void* const data,
        const ServerPermissionLevel required_perm,
        std::set<std::string>& names)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish(); // Do not allow more arguments

    CMD_REQUIRE_PERM(stk_ctx, required_perm);

    bool nfirst = false;
    for (const std::string& it : names)
    {
        if (it.size() > 6 && it.substr(0, 6) == "addon_")
            continue;

        if (nfirst)
            ctx->write(", ");
        ctx->write(it);
        nfirst = true;
    }
    ctx->flush();

    return true;
}

bool KartsCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    ServerLobby* const lobby = stk_ctx->get_lobby();
    return lobby_assets_command(ctx, data, m_required_perm, lobby->m_available_kts.first);
}
bool TracksCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    ServerLobby* const lobby = stk_ctx->get_lobby();
    return lobby_assets_command(ctx, data, m_required_perm, lobby->m_available_kts.second);
}
