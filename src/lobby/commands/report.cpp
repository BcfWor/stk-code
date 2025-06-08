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

#include "report.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/server_config.hpp"
#include <parser/argline_parser.hpp>
#include <fstream>
#include <string>

bool ReportCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string message;
    parser->parse_full(message);

    parser->parse_finish(); // Do not allow more arguments

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    if (stk_ctx->hasRestriction(PRF_NOCHAT))
    {
        stk_ctx->sendNoPermission();
        return false;
    }

    if (message.size() < 5)
    {
        ctx->write("You need to specify the message that is at least 5 characters long.");
        ctx->flush();
        return false;
    }

    // open a file, for append
    std::fstream file(
            ServerConfig::m_reports_filepath, std::ios_base::app );
    if (file.fail() || file.bad())
    {
        ctx->write("Failed to record a report. Input/output error (1). Please inform the administrator.");
        ctx->flush();
        return false;
    }
    std::string player_name = stk_ctx->getProfileName();
    const std::time_t now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());

    char datetime[20];
    std::strftime(datetime, 20, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    file << datetime;

    file << " [" << player_name << "]: " << message << std::endl;

    file.flush();
    if (!file.good())
    {
        ctx->write("Failed to record a report. Input/output error (2). Please inform the administrator.");
        ctx->flush();
        return false;
    }

    // inform success
    ctx->write("Thank you for your report. We will review it at some point and take appropriate action if applicable.");
    ctx->flush();
    return true;
}
