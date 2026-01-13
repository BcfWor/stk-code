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

#include "results.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>
#include <string>
#include <sstream>

bool ResultsCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish(); // Do not allow more arguments

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();
    std::string result = lobby->get_elo_change_string();

    if (result.empty())
    {
        ctx->write("No ELO changes");
    }
    else
    {
        // Check if result contains only duration line (no ELO changes)
        bool has_elo_changes = false;
        bool parse_error = false;

        std::istringstream iss(result);
        std::string line;
        std::string duration_line;
        std::ostringstream formatted;

        while (std::getline(iss, line))
        {
            if (line.empty())
                continue;

            // duration line
            if (line.find("The game lasted") != std::string::npos)
            {
                duration_line = line;
                continue;
            }

            
            size_t last_space = line.find_last_of(' ');
            if (last_space == std::string::npos)
            {
                parse_error = true;
                break;
            }

            std::string player = line.substr(0, last_space);
            std::string change_str = line.substr(last_space + 1);

            int delta = 0;
            try
            {
                delta = std::stoi(change_str);
            }
            catch (...)
            {
                parse_error = true;
                break;
            }

            if (delta != 0)
            {
                has_elo_changes = true;
                formatted << player << " -> "
                          << (delta > 0 ? "+" : "") << delta
                          << "\n";
            }
        }

        // if something wrong -> clean fallback
        if (parse_error)
        {
            std::string msg = "No ELO changes";
            if (!duration_line.empty())
                msg += "\n" + duration_line;
            ctx->write(msg);
        }
        else if (!has_elo_changes)
        {
            std::string msg = "No ELO changes";
            if (!duration_line.empty())
                msg += "\n" + duration_line;
            ctx->write(msg);
        }
        else
        {
            ctx->write(formatted.str());
        }
    }

    ctx->flush();
    return true;
}
