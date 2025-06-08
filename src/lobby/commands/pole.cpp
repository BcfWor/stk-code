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

#include "pole.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool PoleCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    auto parser = ctx->get_parser();

    bool state;
    *parser >> state;
    parser->parse_finish(); // Do not allow more arguments

    if (
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_SOCCER &&
        RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
    {
        ctx->write("Pole only applies to team games.");
        ctx->flush();
        return false;
    }

    if (state == lobby->isPoleEnabled())
    {
        ctx->write("Pole voting is already active or inactive.");
        ctx->flush();
        return false;
    }

    CMD_VOTABLE(data, true);
    CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);

    lobby->setPoleEnabled(state);
    return true;
}
