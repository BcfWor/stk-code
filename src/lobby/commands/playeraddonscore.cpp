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

#include "playeraddonscore.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <sstream>
#include <string>

bool PlayerAddonScoreCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string player_name;

    parser->parse_string(player_name, false);
    parser->parse_finish(); // Do not allow more arguments

    // Do not allow voting for the command
    CMD_VOTABLE(data, false);
    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    if (player_name.empty() && !stk_ctx->testPlayerOnly())
    {
        ctx->write("\nConsole has no addon score. Specify player name instead.\n");
        std::stringstream ss;
        format_usage_into(ss, ctx->get_alias());
        *ctx << ss;
        ctx->flush();
        return false;
    }

    STKPeer* player_peer;

    if (player_name.empty())
        player_peer = stk_ctx->get_peer();
    else
        player_peer = STKHost::get()->findPeerByName(
                StringUtils::utf8ToWide(player_name)).get();

    if (!player_peer)
    {
        ctx->nprintf("Player \"%s\" was not found.", 64,
                player_name.c_str());
        ctx->flush();
        return false;
    }

    auto& scores = player_peer->getAddonsScores();
    if (scores[AS_KART] == -1 && scores[AS_TRACK] == -1 &&
        scores[AS_ARENA] == -1 && scores[AS_SOCCER] == -1)
    {
        ctx->write(player_name + " has no addons.");
        ctx->flush();
        return true;
    }
    else
    {
        // legacy code, refactor later
        std::string msg = player_name;
        msg += " addon:";
        if (scores[AS_KART] != -1)
            msg += " kart: " + StringUtils::toString(scores[AS_KART]) + ",";
        if (scores[AS_TRACK] != -1)
            msg += " track: " + StringUtils::toString(scores[AS_TRACK]) + ",";
        if (scores[AS_ARENA] != -1)
            msg += " arena: " + StringUtils::toString(scores[AS_ARENA]) + ",";
        if (scores[AS_SOCCER] != -1)
            msg += " soccer: " + StringUtils::toString(scores[AS_SOCCER]) + ",";
        msg = msg.substr(0, msg.size() - 1);
        *ctx << msg;
        ctx->flush();
    }
    return true;
}
