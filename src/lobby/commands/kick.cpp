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

#include "kick.hpp"
#include "kickall.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "parser/placeholder_parser.hpp"
#include "utils/string_utils.hpp"
#include "utils/log.hpp"
#include <memory>
#include <parser/argline_parser.hpp>
#include <string>

static const char* LOGNAME = "KickCommand";

bool KickCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string player_name, reason;

    *parser >> player_name;
    parser->parse_full(reason); // allow more arguments

    std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
        StringUtils::utf8ToWide(player_name), true/*ignoreCase*/, true/*prefixOnly*/);

    if (!player_peer || player_peer->isAIPeer())
    {
        ctx->nprintf("Player \"%s\" not found.", 64, player_name.c_str());
        ctx->flush();
        return false;
    }
    player_name = StringUtils::wideToUtf8(player_peer->getPlayerProfiles()[0]->getName());
    
    // override vote arguments with PlaceholderParser.
    auto vote_parser = std::make_shared<nnwcli::PlaceholderParser>();
    vote_parser->push_string(player_name);
    if (!reason.empty())
        vote_parser->set_full_string(reason);
    vote_parser->reset_pos();
    vote_parser->reset_argument_pos();
    CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, vote_parser);
    player_peer->kick();
    if (reason.empty())
    {
        ctx->nprintf("Kicked player \"%s\".", 512,
                player_name.c_str());
        Log::info(LOGNAME, "Kicked player \"%s\".",
                player_name.c_str());
    }
    else
    {
        ctx->nprintf("Kicked player \"%s\" for reason \"%s\".", 512,
                player_name.c_str(), reason.c_str());
        Log::info(LOGNAME, "Kicked player \"%s\" for reason \"%s\".",
                player_name.c_str(), reason.c_str());
    }
    ctx->flush();
    return true;
}
bool KickallCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish();

    auto peers = STKHost::get()->getPeers();
    for (unsigned int i = 0; i < peers.size(); i++)
    {
        if (stk_ctx->get_peer() && peers[i]->isSamePeer(stk_ctx->get_peer()))
            continue;
        peers[i]->kick();
    }

    ctx->write("Kicked everyone from the server.");
    ctx->flush();

    return true;
}
