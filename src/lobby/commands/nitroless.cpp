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

#include "nitroless.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "network/server_config.hpp"
#include <parser/argline_parser.hpp>
#include <sstream>
#include <string>

// This is not a special powerup modifier, since nitro is not a powerup,
// it is not given by the bonus box
bool NitrolessCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    bool state;

    *parser >> state;

    parser->parse_finish(); // Do not allow more arguments

    if (ServerConfig::m_soccer_log && stk_ctx->getPermissionLevel() < PERM_ADMINISTRATOR)
    {
        ctx->write("You can only use this command on unranked TierS servers");
        ctx->flush();
        return false;
    }
    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;
    if (lobby->getCurrentState() != ServerLobby::WAITING_FOR_START_GAME)
    {
        ctx->write("Nitroless mode can only be set before race starts.");
        ctx->flush();
        return false;
    }
    auto rm = RaceManager::get();

    if (state == rm->getNitrolessMode())
    {
        ctx->write("Nitroless mode is already in that state.");
        ctx->flush();
        return false;
    }
    CMD_VOTABLE(data, true);

    CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);

    rm->setNitrolessMode(state);

    std::stringstream bc;
    bc << "Nitroless mode is now ";

    if (state)
        bc << "ACTIVE. No nitro can be collected.";
    else
        bc << "INACTIVE. Nitro can be collected as normal.";
    lobby->sendStringToAllPeers(bc.str());

    return true;
}
