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

#include "listserveraddon.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ListServerAddonCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish();
    CMD_VOTABLE(data, false);
    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby)
        return false;

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    std::string search_pattern;

    unsigned int opt_flags = 0;

    // get the type of the addon
    // receive as much options as possible
    while (true)
    {
        std::string option;

        if (!parser->parse_string(option, false))
            break;
        else if (opt_flags == (1 | 2 | 4 | 8))
        {
            search_pattern = option;
            break;
        }
        else if (option == "-track" && ~opt_flags&1)
            opt_flags |= 1;
        else if (option == "-arena" && ~opt_flags&2)
            opt_flags |= 2;
        else if (option == "-kart" && ~opt_flags&4)
            opt_flags |= 4;
        else if (option == "-soccer" && ~opt_flags&8)
            opt_flags |= 8;
        else if (option.size() > 6 && option.substr(0, 6) == "addon_")
        {
            search_pattern = option;
            break;
        }
        else
        {
            ctx->nprintf("Unrecognized option: %s. Specify one of the following: "
                    "-track/-arena/-kart/-soccer (and optionally addon_... prefix)\n", 255,
                    option.c_str());
            std::stringstream ss;
            format_usage_into(ss, ctx->get_alias());
            *ctx << ss;
            ctx->flush();
            return false;
        }
    }
    if (!opt_flags)
        opt_flags = 1 | 2 | 4 | 8;

    std::set<std::string> total_addons;
    if (opt_flags&4) // kart
    {
        total_addons.insert(lobby->m_addon_kts.first.begin(),
                lobby->m_addon_kts.first.end());
    }
    if (opt_flags&1) // track
    {
        total_addons.insert(lobby->m_addon_kts.second.begin(),
                lobby->m_addon_kts.second.end());
    }
    if (opt_flags&2) // arena
    {
        total_addons.insert(lobby->m_addon_arenas.begin(),
                lobby->m_addon_arenas.end());
    }
    if (opt_flags&8) // soccer
    {
        total_addons.insert(lobby->m_addon_soccers.begin(),
                lobby->m_addon_soccers.end());
    }
    if (total_addons.empty())
        ctx->write("Addon not found");
    else
        ctx->write("Server addon: ");

    for (auto addon_i = total_addons.cbegin();
            addon_i != total_addons.cend();
            addon_i++)
    {
        // addon_ (6 letters)
        if (!search_pattern.empty() && addon_i->find(search_pattern, 6) == std::string::npos)
            continue;

        ctx->write(addon_i->substr(6));
        addon_i++;
        if (addon_i != total_addons.cend())
        {
            ctx->write(", ");
        }
        else
            break;
        addon_i--;
    }
    ctx->flush();
    return true;
}
