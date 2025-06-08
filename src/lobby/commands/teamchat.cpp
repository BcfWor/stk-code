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

#include "teamchat.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/moderation_toolkit/player_restriction.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool TeamChatCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish();
    
    // It is a chat command: Network console has no effect here.
    if (!stk_ctx->testPlayerOnly())
    {
        ctx->write("Tip: Console does not belong to a team. You can write private messages to the players instead.");
        ctx->flush();
        return false;
    }

    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    CMD_REQUIRE_PERM(stk_ctx, PERM_PRISONER);

    lobby->m_team_speakers.insert(stk_ctx->get_peer());

    ctx->write("Your messages are now addressed to team only");
    ctx->flush();

    return true;
}
