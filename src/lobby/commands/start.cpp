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

#include "start.hpp"
#include "lobby/player_queue.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_peer.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool StartCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    ServerLobbyCommands::DispatchData* const dispatch_data =
        reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);
    if (!data) return false;
    auto parser = ctx->get_parser();
    parser->parse_finish();

    // Interact with ServerLobby:
    // Note that if you want to interact with private members of that class, make sure to add
    // this command as a friend class in the ServerLobby.
    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    if (lobby->getCurrentState() != ServerLobby::WAITING_FOR_START_GAME)
    {
        ctx->write("The game has already been started.");
        ctx->flush();
        return false;
    }

    const std::size_t queue_size = LobbyPlayerQueue::get()->getQueueSize();
    if (!queue_size)
    {
        ctx->write("No players are able to play the game.");
        ctx->flush();
        return false;
    }
    STKPeer* const peer = stk_ctx->get_peer();

    if (peer)
    {
        std::shared_ptr<STKPeer> peer_shptr = dispatch_data->m_peer_wkptr.lock();
        // check if the peer is eligible
        const PeerEligibility old_el = peer->getEligibility();
        const PeerEligibility new_el = peer->testEligibility();
        LobbyPlayerQueue::get()->onPeerEligibilityChange(peer_shptr, old_el);

        const bool is_singleslot = queue_size == 1;

        if (is_singleslot) switch (new_el)
        {
            case PeerEligibility::PELG_PRESET_KART_REQUIRED:
                ctx->write("Use /setkart (kart_name) to play the game.");
                ctx->flush();
                return false;
            case PeerEligibility::PELG_PRESET_TRACK_REQUIRED:
                ctx->write("Use /settrack (track_name) - (reverse? on/off) to play the game.");
                ctx->flush();
                return false;
            case PeerEligibility::PELG_YES:
                lobby->startSelection();
                return true;
            case PeerEligibility::PELG_ACCESS_DENIED:
                ctx->write("You are not allowed to play the game.");
                ctx->flush();
                return false;
            case PeerEligibility::PELG_NO_FORCED_TRACK:
                ctx->write("You\'re missing an addon, download it first.");
                ctx->flush();
                return false;
            default:
                ctx->write("Game can't be started for whatever reason.");
                ctx->flush();
                return false;
        }
    }

    CMD_VOTABLE(data, true);
    // CMD_SELFVOTE_PERMLOWER_CROWNLESS(stk_ctx, data, permlvl, parser);
    // Same thing, if the command can be used by the crowned one:
    CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);

    lobby->startSelection();

    return true;
}
