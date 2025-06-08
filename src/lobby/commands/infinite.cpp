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

#include "infinite.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "race/race_manager.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool InfiniteCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    bool state;
    *parser >> state;

    parser->parse_finish(); // Do not allow more arguments
    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    RaceManager::get()->setInfiniteMode(state);

    ctx->write("Set infinite mode to ");

    if (state)
        ctx->write("on.");
    else
        ctx->write("off.");

    ctx->flush();
    return true;
}
