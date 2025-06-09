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

#include "heavyparty.hpp"
#include "mediumparty.hpp"
#include "lightparty.hpp"
#include "randomkarts.hpp"
#include "kart_restriction_common.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>

bool HeavyPartyCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    bool state;
    *parser >> state;
    parser->parse_finish(); // Do not allow more arguments
    return kartRestrictionCommandOf(
            ctx, data,
            m_min_veto,
            KartRestrictionMode::HEAVY,
            state,
            "Heavyparty",
            "Only heavy karts can be chosen.");
}
bool MediumPartyCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    bool state;
    *parser >> state;
    parser->parse_finish(); // Do not allow more arguments
    return kartRestrictionCommandOf(
            ctx, data,
            m_min_veto,
            KartRestrictionMode::MEDIUM,
            state,
            "Mediumparty",
            "Only medium karts can be chosen.");
}
bool LightPartyCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    bool state;
    *parser >> state;
    parser->parse_finish(); // Do not allow more arguments
    return kartRestrictionCommandOf(
            ctx, data,
            m_min_veto,
            KartRestrictionMode::LIGHT,
            state,
            "Lightparty",
            "Only light karts can be chosen.");
}

// other cases

bool RandomkartsCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    bool state;
    *parser >> state;
    parser->parse_finish(); // Do not allow more arguments

    ServerLobby* const lobby = stk_ctx->get_lobby();
    
    if (lobby->getRandomKartsEnabled() == state)
    {
        ctx->write("Randomkarts is already ");
        if (state)
            ctx->write("enabled.");
        else
            ctx->write("disabled.");

        ctx->flush();
        CMD_VOTABLE(data, false);
        return false;
    }
    if (lobby->getCurrentState() != ServerLobby::WAITING_FOR_START_GAME)
    {
        ctx->write("Game is currently active.");
        ctx->flush();
        return false;
    }

    CMD_VOTABLE(data, true);
    CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);
    lobby->setRandomKartsEnabled(state);

    return true;
}
