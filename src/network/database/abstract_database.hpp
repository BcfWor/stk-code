//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2018 SuperTuxKart-Team
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

#ifndef ABSTRACT_DATABASE_HPP
#define ABSTRACT_DATABASE_HPP

#include "utils/singleton.hpp"

class AbstractDatabase : public Singleton<AbstractDatabase>
{
public:

    // Done at initialization, in the ServerLobby class or somewhere else.
    // The class is a singleton, and such can be accessed anywhere.
    // Instantiates the implmentation which can be get with getInstance()

    virtual void init() = 0;
    virtual void finalize() = 0;
};

#endif // ABSTRACT_DATABASE_HPP
