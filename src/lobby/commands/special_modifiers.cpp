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

#include "special_modifiers_common.hpp"
#include "bowlparty.hpp"
#include "cakeparty.hpp"
#include "bowltrainingparty.hpp"
#include "itemless.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/server_config.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool BowlPartyCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    bool state;
    *parser >> state;
    parser->parse_finish(); // Do not allow more arguments

    return specialPowerupCommandFor(
            ctx, data,
            ServerConfig::m_allow_bowlparty && !ServerConfig::m_tiers_roulette,
            m_min_veto,
            Powerup::SpecialModifier::TSM_BOWLPARTY,
            state,
            "Bowlparty",
            "Bonus boxes will only give 3 bowling balls.");
}
bool CakePartyCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    bool state;
    *parser >> state;
    parser->parse_finish(); // Do not allow more arguments

    return specialPowerupCommandFor(
            ctx, data,
            ServerConfig::m_allow_cakeparty && !ServerConfig::m_tiers_roulette,
            m_min_veto,
            Powerup::SpecialModifier::TSM_CAKEPARTY,
            state,
            "Cakeparty",
            "Bonus boxes will only give 2 cakes.");
}
bool BowlTrainingPartyCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    bool state;
    *parser >> state;
    parser->parse_finish(); // Do not allow more arguments

    return specialPowerupCommandFor(
            ctx, data,
            ServerConfig::m_allow_bowlparty && !ServerConfig::m_tiers_roulette,
            m_min_veto,
            Powerup::SpecialModifier::TSM_BOWLTRAININGPARTY,
            state,
            "Bowltrainingparty",
            "Bonus boxes will only give a single bowling ball.");
}
bool ItemlessCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    bool state;
    *parser >> state;
    parser->parse_finish(); // Do not allow more arguments

    return specialPowerupCommandFor(
            ctx, data,
            ServerConfig::m_allow_itemless && !ServerConfig::m_tiers_roulette,
            m_min_veto,
            Powerup::SpecialModifier::TSM_ITEMLESS,
            state,
            "Itemless",
            "Bonus boxes won't give any item.");
}
// Implement other commands when needed, the pattern is the same
