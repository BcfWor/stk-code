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

#include "score.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "modes/soccer_world.hpp"
#include "modes/world.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ScoreCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish();

    CMD_VOTABLE(data, false);

    World* const world = World::getWorld();
    if (!world)
    {
        ctx->write("No on-going game!");
        ctx->flush();
        return false;
    }
    SoccerWorld* const sw = dynamic_cast<SoccerWorld*>(world);
    if (!sw)
    {
        ctx->write("The current game is not a soccer game!");
        ctx->flush();
        return false;
    }

    const int red_score = sw->getScore(KART_TEAM_RED);
    const int blue_score = sw->getScore(KART_TEAM_BLUE);
    std::string msg = "\U0001f7e5 Red " + std::to_string(red_score)+ " : " + std::to_string(blue_score) + " Blue \U0001f7e6";

    *ctx << msg;
    ctx->flush();
    return true;
}
