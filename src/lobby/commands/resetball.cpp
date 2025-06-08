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

#include "resetball.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "race/race_manager.hpp"
#include "modes/world.hpp"
#include "modes/soccer_world.hpp"
#include "tracks/track_object.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ResetBallCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish(); // Do not allow more arguments

    // CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, permlvl, parser);

    ServerLobby* const lobby = stk_ctx->get_lobby();

    if (RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_SOCCER)
    {
        ctx->write("This is only available for soccer.");
        ctx->flush();
        return false;
    }
    if (lobby->getCurrentState() != ServerLobby::RACING)
    {
        ctx->write("This can only be used when the game is active.");
        ctx->flush();
        return false;
    }

    World* const world = World::getWorld();
    SoccerWorld* const sw = dynamic_cast<SoccerWorld*>(world);

    if (!sw)
    {
        ctx->write("The world is not loaded yet. Please wait.");
        ctx->flush();
        return false;
    }

    CMD_VOTABLE(data, true);
    CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);

    TrackObject* ball = sw->getBall();
    if (ball)
    {
        ball->setEnabled(true);
        ball->reset();
        std::string msg = "The ball/puck has been reset";
        lobby->sendStringToAllPeers(msg);
    }
    return true;
}
