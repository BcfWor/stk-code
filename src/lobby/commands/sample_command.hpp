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

#ifndef LOBBY_COMMAND_SAMPLE_HPP
#define LOBBY_COMMAND_SAMPLE_HPP

#include "lobby/stk_command.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
#include "utils/cpp2011.hpp"

// This is an example command.
// Replace the header guard macros with a different name and rename this class to a new command.
// Then implement the underlying command in the source file.
class SampleCommand : public STKCommand
{
    // Minimum permission level in order to skip voting,
    // if the command is even votable in the first place
    ServerPermissionLevel m_min_veto = PERM_PLAYER;
public:
    // The argument for the parent STKCommand constructor
    // is if the command can be voted at all
    SampleCommand() : STKCommand(false)
    {
        // The command's "main" alias.
        m_name = "sample-command";

        // Arguments that are required for this command. Command needs to parse
        // the arguments in the specified order either by parser->parse_*(target); or
        // by using >> operator: *parser >> target;
        m_args = {{nnwcli::CT_DOUBLE, "sum1", "Value 1 for summation"},
                  {nnwcli::CT_DOUBLE, "sum2", "Value 2 for summation"},
                  {nnwcli::CT_STRING, "word", "Test word"}};
        // Optional arguments. Command needs to only parse these arguments in the specified order
        // by only using: if (parser->parse_*(target, false)) 
        // { argument exists behavior } else { argument not specified behavior }
        // m_optargs = {{nnwcli::CT_UTINYINT, "mul", "My optional argument that can be left unspecified"}};
        m_description = "Description of the command. In this sample it prints out a summation.";
    }

    // Commands can be implemented in the source file, and it doesn't have to be the separate source
    // file if the command is not that big
    //
    // Implemented in sample_command.cpp
    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};

#endif // LOBBY_COMMAND_SAMPLE_HPP
