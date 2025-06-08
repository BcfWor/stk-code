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

#include "serverhasaddon.hpp"
#include "addons/addon.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ServerHasAddonCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string addon_id;
    *parser >> addon_id;
    parser->parse_finish(); // Do not allow more arguments

    // Do not allow voting for the command
    // Note that if the command's isVotable() method returns false, this isn't required
    CMD_VOTABLE(data, false);

    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    // Friend class access
    std::set<std::string> total_addons;
    total_addons.insert(lobby->m_addon_kts.first.begin(), lobby->m_addon_kts.first.end());
    total_addons.insert(lobby->m_addon_kts.second.begin(), lobby->m_addon_kts.second.end());
    total_addons.insert(lobby->m_addon_arenas.begin(), lobby->m_addon_arenas.end());
    total_addons.insert(lobby->m_addon_soccers.begin(), lobby->m_addon_soccers.end());
    std::string addon_id_test = Addon::createAddonId(addon_id);
    bool found = total_addons.find(addon_id_test) != total_addons.end();
    if (found)
        ctx->nprintf("Server has addon %s", 255, addon_id_test.c_str());
    else
        ctx->nprintf("Server has no addon %s", 255, addon_id_test.c_str());
    ctx->flush();
    return true;
}
