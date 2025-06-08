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

#include "special_modifiers_common.hpp"
#include "parser/argline_parser.hpp"
#include "network/server_config.hpp"
#include "network/protocols/server_lobby.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "lobby/server_lobby_commands.hpp"

bool specialPowerupCommandFor(
        nnwcli::CommandExecutorContext* const ctx,
        void* const data,
        bool votable,
        const ServerPermissionLevel min_veto,
        const Powerup::SpecialModifier tsm,
        const bool state,
        const std::string tsm_displayname,
        const std::string special_modifier_description)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

	if (stk_ctx->getPermissionLevel() < PERM_ADMINISTRATOR && ServerConfig::m_soccer_log)
	{
		ctx->write("You can only use this command on unranked TierS servers");
        ctx->flush();
		return false;
	}
    auto rm = RaceManager::get();

    if (state == (rm->getPowerupSpecialModifier() == tsm))
    {
        ctx->write(tsm_displayname);
        ctx->write(" is already ");
        if (state)
            ctx->write("active.");
        else
            ctx->write("inactive.");
        ctx->flush();
        return false;
    }

    // Votability is specified in the commands.
    //votable &= !ServerConfig::m_tiers_roulette;

    CMD_VOTABLE(data, votable);
    if (votable)
    {
        CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, min_veto, parser);
    }
    else
    {
        CMD_REQUIRE_CROWN_OR_PERM(stk_ctx, PERM_ADMINISTRATOR);
    }


    rm->setPowerupSpecialModifier(
            state ? tsm : Powerup::TSM_NONE);
    std::stringstream bc;
    bc << tsm_displayname << " is now ";
    if (state)
    {
        bc <<"ACTIVE. " << special_modifier_description;
    }
    else
    {
        bc << "INACTIVE. All standard items as normal.";
    }
    lobby->sendStringToAllPeers(bc.str());
    return true;
}
