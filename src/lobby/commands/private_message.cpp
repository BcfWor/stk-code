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

#include "private_message.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/moderation_toolkit/player_restriction.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "network/network_console.hpp"
#include "network/network_player_profile.hpp"
#include "utils/string_utils.hpp"
#include "utils/log.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool PrivateMessageCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string recipient;
    std::string message;

    *parser >> recipient;
    parser->parse_full(message, true); // no empty messages

    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    CMD_REQUIRE_PERM(stk_ctx, PERM_PRISONER);
    if (stk_ctx->hasRestriction(PRF_NOPCHAT))
    {
        ctx->write("You are not allowed to send private messages..");
        ctx->flush();
        Log::verbose("NetworkConsole", "%d", stk_ctx->get_peer()->getRestrictions());
        return false;
    }

    STKCommandContext* recipient_ctx;

    const std::string recipient_lowercase = StringUtils::toLowerCase(recipient);
    const bool recipient_console =  recipient_lowercase == "@server" || recipient_lowercase == "@console" ||
            recipient_lowercase == "@s" || recipient_lowercase == "@";

    if (!recipient_console)
    {
        STKPeer* const target = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(recipient), true/*ignoreCase*/, true/*onlyPrefix*/).get();
        if (!target)
        {
            ctx->write("Recipient is not online.");
            ctx->flush();
            return false;
        }
        recipient_ctx = target->getCommandContext().get();
        if (!recipient_ctx) return false;
    }
    else
    {
        recipient_ctx = NetworkConsole::network_console_context.get();
    }

    if (stk_ctx == recipient_ctx)
    {
        ctx->write("🔒: ");
        ctx->write(message);
        ctx->flush();
        return true;
    }

    // To send messages to the peer we can utilize STKCommandContext the same way.

    // it's possible the lobby is not set for the context yet
    recipient_ctx->set_lobby(lobby);
    recipient_ctx->set_lobby_commands(stk_ctx->get_lobby_commands());

    // make the message for recipient
    recipient_ctx->write("🔒 from ");
    stk_ctx->write("🔒 to ");

    recipient_ctx->write(stk_ctx->getProfileName());
    stk_ctx->write(recipient_ctx->getProfileName());

    recipient_ctx->write(": ");
    stk_ctx->write(": ");

    recipient_ctx->write(message);
    stk_ctx->write(message);

    stk_ctx->flush();
    recipient_ctx->flush();

    return true;
}
