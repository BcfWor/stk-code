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
#include <string>
#include <cctype>
#include <cstdlib>

bool SlotsCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    static const char* const LOGNAME = "SlotsCommand";

    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    Log::verbose(LOGNAME, "debug: pos %d argpos %d.", parser->get_pos(), parser->get_argument_pos());

    unsigned char amount;

    // workaround for "status" argument, violating type hints:
    std::string first_arg;

    parser->parse_string(first_arg, true);
    if (first_arg == "status")
    {
        CMD_VOTABLE(data, false);
        // the amount of slots are not specified here
        const unsigned int current =
            LobbyPlayerQueue::get()->getMaxPlayersInGame();
        ctx->nprintf("Current slots: %u", 18,
                current);
        ctx->flush();
        return true;
    }
    parser->reset_pos();
    parser->reset_argument_pos();

    // Read the slots value as string first and validate
    std::string amount_str;
    parser->parse_string(amount_str, true);
    if (!parser->exhausted()) {
        std::string remanent;
        parser->parse_full(remanent);
        Log::verbose(LOGNAME, "Amount str: %s, Remnant: %s", amount_str.c_str(), remanent.c_str());
        return false;
    }
    parser->parse_finish();

    bool numeric = !amount_str.empty();
    for (char ch : amount_str)
    {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
        {
            numeric = false;
            break;
        }
    }

    if (!numeric)
    {
        ctx->write("You must specify a numeric slots amount, e.g. /slots 8.");
        ctx->flush();
        CMD_VOTABLE(data, false);
        return false;
    }

    unsigned long amount_ul = std::strtoul(amount_str.c_str(), nullptr, 10);
    if (amount_ul > 255UL)
    {
        ctx->write("The specified slots amount is too large.");
        ctx->flush();
        CMD_VOTABLE(data, false);
        return false;
    }

    amount = static_cast<unsigned char>(amount_ul);

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

    if (stk_ctx->getVeto() >= m_min_veto)
    {
        Log::info(LOGNAME, "%s sets the amount of slots to %d",
                stk_ctx->getProfileName().c_str(), amount);
    }

    LobbyPlayerQueue::get()->setMaxPlayersInGame(amount);
    return true;
}
