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

#ifndef LOBBY_RPS_CHALLENGE_HPP
#define LOBBY_RPS_CHALLENGE_HPP

#include <cstdint>
#include <string>

enum RPSChoice : uint8_t
{
    RPS_NONE = 0,
    RPS_ROCK = 1,
    RPS_PAPER = 2,
    RPS_SCISSORS = 3
};

struct RPSChallenge
{
    uint32_t challenger_id;
    uint32_t challenged_id;
    std::string challenger_name;
    std::string challenged_name;
    RPSChoice challenger_choice = RPS_NONE;
    RPSChoice challenged_choice = RPS_NONE;
    uint64_t timeout;
    bool accepted;
};

// TODO
class RockPaperScissors
{
    static RockPaperScissors* g_instance;


public:
    void create();
    void destroy();

    RockPaperScissors* get() const { return g_instance; }

    static RPSChoice rpsFromString(const std::string& arg);
    static const std::string& rpsToString(RPSChoice arg);
    static RPSChoice winsThis(RPSChoice choice);
    static bool wins(RPSChoice choice, RPSChoice winner);
};

#endif // LOBBY_RPS_CHALLENGE_HPP
