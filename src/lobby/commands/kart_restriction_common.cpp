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

#include "parser/argline_parser.hpp"
#include "kart_restriction_common.hpp"
#include "network/server_config.hpp"
#include "network/protocols/server_lobby.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "lobby/server_lobby_commands.hpp"

bool kartRestrictionCommandOf(nnwcli::CommandExecutorContext* const ctx,
        void* const data,
        const ServerPermissionLevel min_veto,
        const KartRestrictionMode krm,
        const bool state,
        const std::string krm_displayname,
        const std::string kart_type_description)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    ServerLobby* const lobby = stk_ctx->get_lobby();

    irr::core::stringw response;
    if (state == (lobby->getKartRestrictionMode() ==
                KartRestrictionMode::HEAVY))
    {
        ctx->write(krm_displayname);
        ctx->write(" is already active or inactive.");
        ctx->flush();
        return false;
    }

    if (lobby->getCurrentState() != ServerLobby::WAITING_FOR_START_GAME)
    {
        ctx->write("Game is currently active.");
        ctx->flush();
        return false;
    }

    if (!ServerConfig::m_tiers_roulette && !ServerConfig::m_soccer_log)
    {
        // can be voted
        CMD_VOTABLE(data, true);
        CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, min_veto, parser);
    }
    else
    {
        CMD_REQUIRE_PERM(stk_ctx, PERM_ADMINISTRATOR);
    }
    lobby->setKartRestrictionMode(state ? HEAVY : NONE);

    // BROADCAST
    std::stringstream bc;
    bc << krm_displayname << " is now ";
    if (state)
    {
        bc << "ACTIVE. " << kart_type_description;
    }
    else
    {
        bc << "INACTIVE. All karts can be chosen.";
    }

    lobby->sendStringToAllPeers(bc.str());
    return true;
}
