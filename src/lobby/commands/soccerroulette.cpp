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

#include "network/server_config.hpp"
#include "soccerroulette.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "modes/soccer_roulette.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool SoccerRouletteCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string subcmd;
    std::string track_id;

    *parser >> subcmd; // Subcommand support will be added in future versions of nnwcli
    parser->parse_string(track_id, false);

    parser->parse_finish(); // Do not allow more arguments

    ServerLobby* const lobby = stk_ctx->get_lobby();

    if (!ServerConfig::m_soccer_roulette)
    {
        ctx->write("These soccerroulette commands are only possible in special roulette servers");
        ctx->flush();
        return false;
    }

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    if (subcmd == "status")
    {
        bool is_enabled = ServerConfig::m_soccer_roulette;
        std::string current = SoccerRoulette::get()->getCurrentField();
        ctx->write("Soccer Roulette is ");
        if (is_enabled)
        {
            ctx->write("enabled. Current field: ");
            ctx->write(current);
        }
        else
            ctx->write("disabled.");
        ctx->flush();
    }
    else if (subcmd == "add" && !track_id.empty())
    {
        SoccerRoulette::get()->addField(track_id);
        ctx->nprintf("Added field %s to Soccer Roulette", 512,
                track_id.c_str());
        ctx->flush();
    }
    else if (subcmd == "remove" && !track_id.empty())
    {
        SoccerRoulette::get()->removeField(track_id);
        ctx->nprintf("Removed field %s from Soccer Roulette", 512,
                track_id.c_str());
        ctx->flush();
    }
    else if (subcmd == "list")
    {
        const std::vector<std::string>& fields = SoccerRoulette::get()->getFields();
        ctx->write("Soccer Roulette fields: ");
        for (size_t i = 0; i < fields.size(); i++)
        {
            ctx->write(fields[i]);
            if (i < fields.size() - 1)
                ctx->write(", ");
        }
        ctx->flush();
    }
    else if (subcmd == "reload")
    {
        SoccerRoulette::get()->reload();
        ctx->write("Soccer Roulette fields reloaded from configuration");
        ctx->flush();
    }
    else if (subcmd == "teams")
    {
        ctx->write(SoccerRoulette::get()->getTeamsInfo());
        ctx->flush();
    }
    else if (subcmd == "start")
    {
        std::string next_field = SoccerRoulette::get()->getNextField();
        bool success = lobby->setForcedTrack(next_field, 10, false, true, true);
        std::string msg;
        if (success)
        {
            msg = "Soccer Roulette started with field: " + next_field;
        }
        else
        {
            msg = "Failed to set field: " + next_field;
        }
        lobby->sendStringToAllPeers(msg);
        lobby->setPoleEnabled(true);
    }
    else if (subcmd == "reset")
    {
        SoccerRoulette::get()->resetFieldIndex();
        std::string current_field = SoccerRoulette::get()->getCurrentField();
        ctx->write("Soccer Roulette field index reset. Next field will be: ");
        ctx->write(current_field);
        ctx->flush();
    }
    else if (subcmd == "kick" && !track_id.empty())
    {
        SoccerRoulette::get()->kickPlayer(track_id, stk_ctx);
    }
    else if (subcmd == "reassign" && track_id == "teams")
    {
        SoccerRoulette::get()->loadTeamsFromXML();
        SoccerRoulette::get()->reassignTeams(stk_ctx);
    }
    else
    {
        ctx->write("Unknown Soccer Roulette command. Format: /soccerroulette on|off|status|add <field>|remove <field>|list|reload|reset");
        ctx->flush();
        return false;
    }
    return true;
}
