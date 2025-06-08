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

#include "endgame.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "modes/world.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool EndGameCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish(); // Do not allow more arguments

    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    if (lobby->getCurrentState() != ServerLobby::RACING)
    {
        ctx->write("Game is not active.");
        ctx->flush();
        return false;
    }

    CMD_VOTABLE(data, true);
    CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);

    World* w = World::getWorld();
    if (!w) return false;

    w->scheduleInterruptRace();

    lobby->sendStringToAllPeers("The game has been interrupted.");

	if (ServerConfig::m_soccer_log || ServerConfig::m_race_log)
	{
	    std::ofstream log(ServerConfig::m_live_soccer_log_path, std::ios::app);
	    log << "/end is used";
	    log.close();
	    Log::verbose("EndGameCommand", "/end log");
	}

    return true;
}
