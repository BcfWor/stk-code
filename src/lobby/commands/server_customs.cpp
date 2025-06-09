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

#include "lobby/commands/setdifficulty.hpp"
#include "lobby/commands/setgoaltarget.hpp"
#include "setmode.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool SetModeCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    if (ServerConfig::m_ranked)
    {
        ctx->write("This command does not apply to ranked servers.");
        ctx->flush();
        return false;
    }

    auto parser = ctx->get_parser();

    std::string modename;
    int mode;
    bool goaltarget;

    *parser >> modename; 
    parser->parse_bool(goaltarget, false);

    parser->parse_finish();

    ServerLobby* const lobby = stk_ctx->get_lobby();

    if (!ServerConfig::getLocalGameModeFromName(
            modename, &mode, false, false))
    {
        ctx->write("Unknown mode. Please specify one of the following: "
               "standard, time-trial, ffa, soccer, ctf");
        ctx->flush();
        return false;
    }

    if (ServerConfig::m_server_configurable)
    {
        CMD_VOTABLE(data, true);
        CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);
    }
    else
    {
        CMD_REQUIRE_CROWN_OR_PERM(stk_ctx, m_override_perm);
    }

    lobby->updateServerConfiguration(-1, mode, goaltarget);

    ctx->nprintf("Changed mode to %s.", 512, RaceManager::get()->getMinorModeName().c_str());
    ctx->flush();
    return true;
}
bool SetDifficultyCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    if (ServerConfig::m_ranked)
    {
        ctx->write("This command does not apply to ranked servers.");
        ctx->flush();
        return false;
    }

    auto parser = ctx->get_parser();

    std::string diffname;

    *parser >> diffname; 

    parser->parse_finish();

    ServerLobby* const lobby = stk_ctx->get_lobby();

    RaceManager::Difficulty diff;

    if (!RaceManager::getDifficultyFromName(
            diffname, &diff))
    {
        ctx->write("Unknown difficulty. Please specify one of the following: "
               "novice, intermediate, expert, supertux");
        ctx->flush();
        return false;
    }
    if (ServerConfig::m_server_configurable)
    {
        CMD_VOTABLE(data, true);
        CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);
    }
    else
    {
        CMD_REQUIRE_CROWN_OR_PERM(stk_ctx, m_override_perm);
    }

    lobby->updateServerConfiguration(diff, -1, -1);

    ctx->nprintf("Changed mode to %s.", 512,
            RaceManager::get()->getDifficultyAsString(
            RaceManager::get()->getDifficulty()).c_str());
    ctx->flush();
    return true;
}
bool SetGoalTargetCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    if (ServerConfig::m_ranked)
    {
        ctx->write("This command does not apply to ranked servers.");
        ctx->flush();
        return false;
    }

    auto parser = ctx->get_parser();

    bool state;
    *parser >> state; 
    parser->parse_finish();

    ServerLobby* const lobby = stk_ctx->get_lobby();

    if (ServerConfig::m_server_configurable)
    {
        CMD_VOTABLE(data, true);
        CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);
    }
    else
    {
        CMD_REQUIRE_CROWN_OR_PERM(stk_ctx, m_override_perm);
    }

    lobby->updateServerConfiguration(-1, -1, state ? 1 : 0);

    ctx->write("Goal target is now ");
    ctx->write(state ? "on." : "off.");

    ctx->flush();
    return true;
}
