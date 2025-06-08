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

#include "setperm.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/database/abstract_database.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <string>

static
bool setPermCommand(
        nnwcli::CommandExecutorContext* const ctx, void* const data,
        const ServerPermissionLevel lvl,
        const std::string& playername,
        const ServerPermissionLevel min_perm)
{
    STK_CTX(stk_ctx, ctx);
    CMD_REQUIRE_PERM(stk_ctx, min_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();
    AbstractDatabase* const db = lobby->getDatabase();
    if (!lobby)
        return false;

    if (stk_ctx->get_peer() && lvl >= stk_ctx->getPermissionLevel())
    {
        ctx->write("Cannot set rank of at least your own, specify lower values.");
        ctx->flush();
        return false;
    }
    auto peer = STKHost::get()->findPeerByName(
        StringUtils::utf8ToWide(playername), true, true
    );
    uint32_t oid = db->lookupOID(playername);
    int current;
    if (oid)
        current = db->loadPermissionLevelForOID(oid);
    else
        current = PERM_PLAYER;

    if (peer)
        current = std::max(
                current,
                peer->getPermissionLevel());
    if (stk_ctx->get_peer() && current >= stk_ctx->getPermissionLevel())
    {
        ctx->write("Cannot demote or promote someone with rank of at least your level.");
        ctx->flush();
        return false;
    }
    if (peer)
    {
        peer->setPermissionLevel(lvl);
    }
    if (!oid)
    {
        ctx->write("Player has no recorded online id, changes are temporary.\n");
    }
    else
        db->writePermissionLevelForOID(oid, lvl);
    return true;
}

bool SetPermCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    auto parser = ctx->get_parser();

    std::string playername;
    int lvl;

    *parser >> lvl;
    *parser >> playername;

    parser->parse_finish(); // Do not allow more arguments

    if (setPermCommand(ctx, data, (ServerPermissionLevel)lvl, playername, m_min_perm))
    {
        ctx->nprintf("Set %s to %d.", 512, playername.c_str(), lvl);
        ctx->flush();
        return true;
    }
    return false;
}
bool SetPermAliasCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    auto parser = ctx->get_parser();

    std::string playername;

    *parser >> playername;

    parser->parse_finish(); // Do not allow more arguments

    if (setPermCommand(ctx, data, m_level, playername, m_min_perm))
    {
        ctx->nprintf("Set %s as %s (%d).", 512, playername.c_str(), m_rankname.c_str(), m_level);
        ctx->flush();
        return true;
    }
    return false;
}
