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

#include "sample_command.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool SampleCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    double sum1, sum2;
    std::string word;

    *parser >> sum1; // Get arg 1
    *parser >> sum2; // Get arg 2
    *parser >> word; // Get arg 3

    parser->parse_finish(); // Do not allow more arguments

    // Do not allow voting for the command
    // Note that if the command's isVotable() method returns false, this isn't required
    // CMD_VOTABLE(data, false);
    // Specifying true will allow voting. Note that this can be put under conditions.

    // --- UNVOTABLE COMMANDS---
    // When the sensitive part of the command is reached, you can test player for the permission.
    // Method stk_ctx->testPermission(...) returns true if the sender has permission of at least this level,
    // otherwise sends "You are not allowed to access this command" message to the sender
    // and returns false. Use this:

    // CMD_REQUIRE_PERM(stk_ctx, PERM_ADMINISTRATOR);

    // Or, if the command can also be used by the crowned player, use this:
    // CMD_REQUIRE_CROWN_OR_PERM(stk_ctx, PERM_MODERATOR);
    
    // --- VOTABLE COMMANDS ---
    // When dealing with the commands that can be collectively decided upon,
    // not only the command needs to preempt itself with CMD_VOTABLE() macros at the parts of the code
    // before the actual thing happens, but also to commit self-vote before that.
    // CMD_SELFVOTE_PERMLOWER_CROWNLESS(stk_ctx, data, permlvl, parser);
    // Same thing, if the command can be used by the crowned one:
    // CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, permlvl, parser);

    // Interact with ServerLobby:
    // Note that if you want to interact with private members of that class, make sure to add
    // this command as a friend class in the ServerLobby.
    ServerLobby* const lobby = stk_ctx->get_lobby();

    // Or even with ServerLobbyCommands instance:
    // ServerLobbyCommands* const slc = stk_ctx->get_lobby_commands();
    // ...or you can do this instead
    // ... = ServerLobbyCommands::get(); or ServerLobbyCommands::get()->...();

    // Send the response to the sender. Sender can be a network console or the player.
    // While it's possible to prevent network console or the player from running the command,
    // this is done only for a special occasion.

    ctx->nprintf("Sum of %f and %f is %f. Your word is: \"%s\".", 127,
            sum1, sum2, sum1 + sum2, word.c_str());
    // STKCommandContext accumulates the buffer into the internal stringstream. To
    // actually send the message, use
    ctx->flush();
    // When the command is run successfully, return true, otherwise return false;

    return true;
}
