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

#ifndef LOBBY_SOCCER_ELO_RANKING_HPP
#define LOBBY_SOCCER_ELO_RANKING_HPP

#include <string>

struct SoccerELOEntry
{
    int games, elo;
    float team_size, goals_per_game, win_rate;
};

class AbstractSoccerELORanking
{
public:
    // TODO: later
    virtual void init() = 0;
    virtual void finalize() = 0;

    virtual SoccerELOEntry fetch(std::string playername) = 0;
};

#endif // LOBBY_SOCCER_ELO_RANKING_HPP
