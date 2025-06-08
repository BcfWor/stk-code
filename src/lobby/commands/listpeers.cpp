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

#include "listpeers.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/socket_address.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ListPeersCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish();

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);
    ServerLobby* const lobby = stk_ctx->get_lobby();

    auto peers = STKHost::get()->getPeers();
    if (peers.empty())
    {
        ctx->write("No peers exist");
        ctx->flush();
        return true;
    }
    for (unsigned int i = 0; i < peers.size(); i++)
    {
        ctx->nprintf("%u: %s %s %s\n", peers[i]->getHostId(),
            peers[i]->getAddress().toString().c_str(),
            StringUtils::wideToUtf8(peers[i]->getPlayerProfiles()[0]->getName()).c_str(),
            peers[i]->getUserVersion().c_str());
    }
    ctx->flush();
    return true;
}
