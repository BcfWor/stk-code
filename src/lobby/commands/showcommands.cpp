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

#include "showcommands.hpp"
#include "command.hpp"
#include "command_executor.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ShowCommandsCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    auto executor = ctx->get_executor();
    parser->parse_finish(); // Do not allow more arguments

    auto it = executor->get_command_iter();

    for (;;)
    {
        ctx->write("/");
        ctx->write(it.first->get()->get_name());

        // alias scan
        auto alias_it = executor->get_alias_iter();
        write_aliases(stk_ctx->get_response_buffer(),
                alias_it.first, alias_it.second, it.first->get());

        it.first++;
        if (it.first == it.second)
            break;
        else
            ctx->write(" ");
    }
    ctx->flush();
    return true;
}
