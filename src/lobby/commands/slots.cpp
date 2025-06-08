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

#include "slots.hpp"
#include "lobby/player_queue.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/server_config.hpp"
#include "utils/log.hpp"
#include <parser/argline_parser.hpp>
#include <stdexcept>
#include <string>

bool SlotsCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    unsigned char amount;

    // workaround for "status" argument, violating type hints:
    std::string try_status;

    parser->parse_string(try_status, true);
    if (try_status == "status")
    {
        CMD_VOTABLE(data, false);
        // the amount of slots are not specified here
        const unsigned int current =
            LobbyPlayerQueue::get()->getMaxPlayersInGame();
        ctx->nprintf("Current slots: %u", 18, 
                current > 10 ? 10 : current);
        ctx->flush();
        return true;
    }
    parser->reset_pos();
    parser->reset_argument_pos();

    *parser >> amount;

    parser->parse_finish();

    if (amount < ServerConfig::m_slots_min || amount > ServerConfig::m_slots_max)
    {
        if (stk_ctx->getPermissionLevel() < m_perm_unlimited)
        {
            ctx->nprintf("You can only specify the amount between %d and %d", 100,
                    (int)ServerConfig::m_slots_min, (int)ServerConfig::m_slots_max);
            ctx->flush();
            CMD_VOTABLE(data, false);
            return false;
        }
        CMD_VOTABLE(data, false);
    }
    else
    {
        CMD_VOTABLE(data, true);
        CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);
    }

    LobbyPlayerQueue::get()->setMaxPlayersInGame(amount);
    return true;
}
