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

#ifndef LOBBY_COMMAND_SHOWCOMMANDS_HPP
#define LOBBY_COMMAND_SHOWCOMMANDS_HPP

#include "lobby/stk_command.hpp"
#include "utils/cpp2011.hpp"
#include <memory>
#include <map>

class ShowCommandsCommand : public STKCommand
{
public:
    ShowCommandsCommand() : STKCommand(false)
    {
        m_name = "showcommands";
        m_args = {};
        m_description = "Like /help, but shows a plain list of commands with their aliases.";
    }

    void write_aliases(
            std::ostream& stream,
            std::map<std::string, std::shared_ptr<Command>>::const_iterator& start,
            std::map<std::string, std::shared_ptr<Command>>::const_iterator& end,
            Command* const cmd)
    {
        for(; start != end; start++)
        {
            if (start->first == start->second->get_name())
                // only aliases
                continue;
            if(start->second.get() != cmd)
                continue;
            stream << "|";

            stream << start->first;
        }
    }
    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};

#endif // LOBBY_COMMAND_SHOWCOMMANDS_HPP
