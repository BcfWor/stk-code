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

#include "rank.hpp"
#include "lobby/commands/rank_common.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include <parser/argline_parser.hpp>
#include <stdexcept>
#include <string>

bool RankCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    bool is_playername = false;
    unsigned int page = 1;
    std::string playername;

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);
    // try getting the page number first
    try
    {
        parser->parse_unsigned_integer(page, false);
    }
    catch (const std::invalid_argument& e)
    {
        parser->reset_pos();
        parser->reset_argument_pos();
        is_playername = true; // this was not a valid number, therefore a player name instead
        playername = parser->parse_string(playername);
    }

    parser->parse_finish(); // Do not allow more arguments

    if (is_playername)
    {
        if (playername[0] == '$')
            playername.erase(0, 1);
        
        return dumpSoccerRankingEntryInto(ctx, playername);
    }

    if (page == 0)
        page = 1;

    return dumpSoccerRankingListInto(ctx, page, m_element_per_page);
}
