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

#ifndef LOBBY_COMMAND_RESTRICT_HPP
#define LOBBY_COMMAND_RESTRICT_HPP

#include "lobby/stk_command.hpp"
#include "network/moderation_toolkit/player_restriction.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
#include "utils/cpp2011.hpp"

// These commands trigger eligibility change

class RestrictCommand : public STKCommand
{
    ServerPermissionLevel m_required_perm = PERM_MODERATOR;
public:
    RestrictCommand() : STKCommand(false)
    {
        m_name = "restrict";
        m_args = {{nnwcli::CT_BOOL, "state", "State of restriction: on or off"},
                  {nnwcli::CT_STRING, "restriction", "Type: nospec/nogame/nochat/nopchat/noteam/handicap/track"},
                  {nnwcli::CT_STRING, "player", "Name of player"}};
        m_description = "Applies a restriction to a player, or removes it.";
    }

    // Implemented in moderation_toolkit.cpp
    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};

// For commands like /nogame on <player>, /nochat on <player>...
class RestrictAliasCommand : public STKCommand
{
    ServerPermissionLevel m_required_perm = PERM_MODERATOR;
    PlayerRestriction m_restriction;
public:
    RestrictAliasCommand(
            const std::string& restriction_name,
            const PlayerRestriction restriction) : STKCommand(false)
    {
        m_name = restriction_name;
        m_args = {{nnwcli::CT_BOOL, "state", "State of restriction: on or off"},
                  {nnwcli::CT_STRING, "player", "Name of player"}};
        m_description = "Applies a " + restriction_name + " restriction to a player, or removes it.";
        m_restriction = restriction;
    }

    // Implemented in moderation_toolkit.cpp
    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};

#endif // LOBBY_COMMAND_RESTRICT_HPP
