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

#ifndef LOBBY_STK_COMMAND_HPP
#define LOBBY_STK_COMMAND_HPP

#include <command.hpp>
#include <memory>

#define STK_CTX(stk_ctx, ctx)\
    STKCommandContext* const stk_ctx = dynamic_cast<STKCommandContext*>(ctx);\
    if (!stk_ctx)\
        return false;


#define CMD_VOTABLE(data, votable) do{\
    auto dispatch_data = reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);\
    if (dispatch_data && dispatch_data->m_is_vote)\
    {\
        dispatch_data->m_can_vote = votable;\
        return false;\
    }} while(0);

// this will submit the vote to the ServerLobbyCommands and the call will proceed to post the currently executed command as a vote
// Self-voting always sets m_can_vote to true, it does not need a second command run
#define CMD_SELFVOTE_INTERNAL(ctx, dispatch_data, parser_shptr) do {\
    dispatch_data->m_is_vote = true;\
    dispatch_data->m_can_vote = true;\
    dispatch_data->m_vote_positive = true;\
    dispatch_data->m_voted_command = nullptr;\
    dispatch_data->m_voted_alias = ctx->get_alias();\
    dispatch_data->m_voted_args = parser_shptr;\
    parser_shptr->reset_pos();\
    parser_shptr->reset_argument_pos();\
    return false;\
    } while(0);
#define CMD_SELFVOTE(ctx, data, parser_shptr) do {\
    auto dispatch_data = reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);\
    if (dispatch_data)\
    {\
        CMD_SELFVOTE_INTERNAL(ctx, dispatch_data, parser_shptr)\
    }}

#define CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, permlvl, parser_shptr) do {\
    auto dispatch_data = reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);\
    if (dispatch_data)\
    {\
        if (stk_ctx->getVeto() < permlvl && (!stk_ctx->isCrowned()))\
        {\
            CMD_SELFVOTE_INTERNAL(stk_ctx, dispatch_data, parser_shptr)\
        }\
    }} while(0);

#define CMD_SELFVOTE_PERMLOWER_CROWNLESS(stk_ctx, data, permlvl, parser_shptr) do {\
    auto dispatch_data = reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);\
    if (dispatch_data)\
    {\
        if (stk_ctx->getVeto() < permlvl)\
        {\
            CMD_SELFVOTE_INTERNAL(dispatch_data, parser_shptr)\
        }\
    }} while(0);

#define CMD_REQUIRE_PERM(stk_ctx, permlvl) if (!stk_ctx->testPermission(permlvl)) return false;
#define CMD_REQUIRE_CROWN_OR_PERM(stk_ctx, permlvl) if (!stk_ctx->isCrowned() && !stk_ctx->testPermission(permlvl)) return false;

class STKCommand : public nnwcli::Command
{
    bool m_votable;
public:
    STKCommand(bool votable) { m_votable = votable; };
    ~STKCommand() {};

    //-------------------------------------------------------------------
    virtual bool isVotable() const              { return m_votable; }
    virtual void setVotable(bool votable)       { m_votable = votable; }
    //-------------------------------------------------------------------
};

#endif // LOBBY_STK_COMMAND_HPP
