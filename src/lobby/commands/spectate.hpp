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

#ifndef LOBBY_COMMAND_SPECTATE_HPP
#define LOBBY_COMMAND_SPECTATE_HPP

#include "argument_types.hpp"
#include "lobby/stk_command.hpp"
#include "utils/cpp2011.hpp"

class SpectateCommand : public STKCommand
{
public:
    SpectateCommand() : STKCommand(false)
    {
        m_name = "spectate";
        m_args = {{nnwcli::CT_BOOL, "status", "spectate next game?"}};
        m_description = "Change spectating status for the next game.";
    }

    // Implemented in common_command_set_1.cpp
    virtual bool execute(nnwcli::CommandExecutorContext* const context, void* data) OVERRIDE;
};

#endif // LOBBY_COMMAND_SPECTATE_HPP
