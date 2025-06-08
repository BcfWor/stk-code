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

#include "replay.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ReplayCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    
    bool state;
    *parser >> state;

    parser->parse_finish(); // Do not allow more arguments

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (lobby->m_replay_requested == state)
    {
        ctx->write("/replay has already been enabled or disabled.");
        ctx->flush();
        return false;
    }

    if (RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_TIME_TRIAL)
    {
        ctx->write("/replay is only available in TierS-World Record race");
        ctx->flush();
        return false;
    }
    lobby->m_replay_requested = state;
    RaceManager::get()->setRecordRace(state);

    std::string msg;
    if (state)
        msg = "Next race will be recorded into the new replay.";
    else
        msg = "Recording of the new replay is cancelled.";
    lobby->sendStringToAllPeers(msg);

    return true;
}
