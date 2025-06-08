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

#include "playerhasaddon.hpp"
#include "addons/addon.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool PlayerHasAddonCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string addon_id;
    std::string player_name;

    *parser >> addon_id;
    if (!parser->parse_string(player_name, false))
    {
        // not required for the player
        if (!stk_ctx->testPlayerOnly())
        {
            ctx->write("\nBecause console has all the addons, specify the player name.\n");
            std::stringstream ss;
            format_usage_into(ss, ctx->get_alias());
            *ctx << ss;
            ctx->flush();
            CMD_VOTABLE(data, false);
            return false;
        }
    }

    parser->parse_finish(); // Do not allow more arguments

    // Do not allow voting for the command
    CMD_VOTABLE(data, false);

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    STKPeer* player_peer;

    if (player_name.empty())
        player_peer = stk_ctx->get_peer();
    else
    {
        CMD_REQUIRE_PERM(stk_ctx, m_other_players_perm);
        player_peer = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(player_name)).get();
    }

    if (!player_peer)
    {
        ctx->nprintf("Player %s was not found.", 512,
                player_name.c_str());
        ctx->flush();
        return false;
    }

    if (player_peer->hasPlayerProfiles())
        player_name = StringUtils::wideToUtf8(player_peer->getPlayerProfiles()[0]->getName());

    std::string addon_id_test = Addon::createAddonId(addon_id);
    const auto& kt = player_peer->getClientAssets();
    const bool found = (kt.first.find(addon_id_test) != kt.first.cend() ||
            kt.second.find(addon_id_test) != kt.second.cend());

    ctx->write(player_name);
    ctx->write(" has ");
    if (!found)
        ctx->write("no ");
    ctx->write("addon ");
    ctx->write(addon_id);

    ctx->flush();

    return true;
}
