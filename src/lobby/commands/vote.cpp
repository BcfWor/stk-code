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

#include "vote.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "network/server_config.hpp"
#include "parser/argline_parser.hpp"
#include <memory>

bool VoteCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* data)
{
    STKCommandContext* const stk_c = dynamic_cast<STKCommandContext*>(ctx);
    if (!stk_c)
        return false;

    // If this command tries to vote itself, reject
    CMD_VOTABLE(data, false);
    
    if (!stk_c->testPlayerOnly()) return false;

    if (ServerConfig::m_command_voting == false)
    {
        ctx->write("Command voting is disabled on this server.");
        ctx->flush();
        return false;
    }

    // get the cmdline for that
    std::string cmdname;
    std::string argline;
    auto parser = ctx->get_parser();
    *parser >> cmdname;
    parser->parse_full(argline);
    // nnwcli::ArglineParser* a_parser = dynamic_cast<nnwcli::ArglineParser*>(parser.get());

    const auto dispatchdata = reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);
    try
    {
        std::shared_ptr<nnwcli::Command>& cmd = ctx->get_executor()->get_command(cmdname);

        dispatchdata->m_is_vote = true;
        // m_can_vote is determined by the target command later
        dispatchdata->m_voted_command = cmd;
        dispatchdata->m_voted_argline = argline;
        dispatchdata->m_voted_alias = cmdname;
        dispatchdata->m_vote_positive = true; // TODO: antivote
        // this is the only non-selfvotable command, it needs to use an external parser instead of the current
        // In other commands that can commit self-vote the parser is reused and reset to the beginning with
        // parser->reset_pos();
        dispatchdata->m_voted_args = std::make_shared<nnwcli::ArglineParser>(argline);
    }
    catch (const nnwcli::command_not_found& e)
    {
        *ctx << "Unknown command: " << cmdname;
        ctx->flush();
        return false;
    }

    // the further vote algorithm is then handled in the server_lobby_commands
    return true;
#if 0
    // deadlock workaround
    ctx->get_executor()->m_mutex.unlock();
    try
    {
        dispatchdata->m_is_vote = true;
        ctx->get_executor()->dispatch_line(cmdline, ctx->get_executor()->get_latest_context(), data);
    }
    catch(const std::exception& e) {}
    const bool _relocked = ctx->get_executor()->m_mutex.try_lock();

    if (dispatchdata && dispatchdata->m_can_vote)
    {
        stk_c->get_lobby_commands()->submitVote(stk_c->getProfileName(), cmdline);
    }
    else
    {
        ctx->write("This command is not available for voting.");
        ctx->flush();
    }
#endif
}
