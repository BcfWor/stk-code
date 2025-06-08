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

#include "customtext_reload.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/customtext_manager.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/server_config.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool CustomTextReloadCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish(); // Do not allow more arguments

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    auto manager = stk_ctx->get_lobby_commands()->getCustomtextManager();
    if (manager->loadCommands(ServerConfig::m_customtext_commands))
    {
        ctx->write("Reloaded commands.");
        ctx->flush();
        return true;
    }
    else
    {
        ctx->write("Could not reload the commands. Make sure the syntax is specified correctly.");
        ctx->flush();
        return false;
    }
}
