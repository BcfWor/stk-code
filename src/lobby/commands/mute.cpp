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

#include "mute.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool MuteCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string playername;

    *parser >> playername; // Get arg 3

    parser->parse_finish(); // Do not allow more arguments

    if (!stk_ctx->testPlayerOnly()) return false;

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    std::shared_ptr<STKPeer> player_peer;
    std::string result_msg;
    core::stringw playername_w = StringUtils::utf8ToWide(playername);
    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    ServerLobbyCommands::DispatchData* const dispatch_data =
        reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);
    if (!data) return false;

    player_peer = STKHost::get()->findPeerByName(playername_w, true, true);

    if (!player_peer)
    {
        ctx->nprintf("Player \"%s\" was not found.", 255,
                playername.c_str());
        ctx->flush();
        return false;
    }

    if (stk_ctx->get_peer() && player_peer->isSamePeer(stk_ctx->get_peer()))
    {
        ctx->write("You cannot specify yourself.");
        ctx->flush();
        return false;
    }

    std::weak_ptr<STKPeer>& peer_wkptr = dispatch_data->m_peer_wkptr;

    lobby->m_peers_muted_players[peer_wkptr].insert(playername_w);

    ctx->write("Muted player ");
    ctx->write(StringUtils::wideToUtf8(player_peer->getPlayerProfiles()[0]->getName()));
    ctx->flush();
    return true;
}
