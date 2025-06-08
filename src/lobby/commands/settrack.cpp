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

#include "settrack.hpp"
#include "lobby/player_queue.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool SetTrackCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    const bool canSpecifyExtra = stk_ctx->getPermissionLevel() >= m_required_perm;

    auto parser = ctx->get_parser();

    std::string track_id;
    int laps = -1;
    bool specvalue = false;

    *parser >> track_id;

    if (canSpecifyExtra)
    {
        parser->parse_integer(laps, false);
        parser->parse_bool(specvalue, false);
    }

    parser->parse_finish(); // Do not allow more arguments

    // CMD_REQUIRE_CROWN_OR_PERM(stk_ctx, m_required_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();
    STKPeer* const peer = stk_ctx->get_peer();

    if (ServerConfig::m_command_track_mode)
    {
        CMD_REQUIRE_PERM(stk_ctx, PERM_PLAYER);
        if (LobbyPlayerQueue::get()->isSpectatorByLimit(peer) ||
                !peer->isEligibleForGame())
        {
            ctx->write("You need to be able to play in order to use that command.");
            ctx->flush();
            return false;
        }
    }
    else
    {
        CMD_REQUIRE_PERM(stk_ctx, m_required_perm);
    }
    bool isField = (ctx->get_alias() == "setfield");

    // Check that peer and server have the track
    bool found = lobby->setForcedTrack(track_id, laps, specvalue, isField, true);
    if (!found)
    {
        if (isField)
            ctx->write("Soccer field \'");
        else
            ctx->write("Track \'");
        ctx->write(track_id);
        ctx->write("\' does not exist or is not installed.");
        ctx->flush();
        return true;
    }
    return true;
}
