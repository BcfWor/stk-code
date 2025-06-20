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

#include "rps_challenge.hpp"
#include "utils/string_utils.hpp"
#include <string>
#include <vector>

RPSChoice RockPaperScissors::rpsFromString(const std::string& arg)
{
    std::string arg_lowercase = StringUtils::toLowerCase(arg);

    if (arg_lowercase.empty())
        return RPS_NONE;

    if (arg_lowercase == "r" || arg_lowercase == "rock")
        return RPS_ROCK;
    if (arg_lowercase == "p" || arg_lowercase == "paper")
        return RPS_PAPER;
    if (arg_lowercase == "s" || arg_lowercase == "scissors")
        return RPS_SCISSORS;
    
    return RPS_NONE;
}
const std::string& RockPaperScissors::rpsToString(RPSChoice arg)
{
    static const std::vector<std::string> constants = {
        "unspecified",
        "rock",
        "paper",
        "scissors"
    };

    return constants[arg];
}
RPSChoice RockPaperScissors::winsThis(const RPSChoice choice)
{
    switch (choice)
    {
        case RPS_NONE:
            return RPS_NONE;
        case RPS_ROCK:
            return RPS_PAPER;
        case RPS_PAPER:
            return RPS_SCISSORS;
        case RPS_SCISSORS:
            return RPS_ROCK;
    }
}
bool RockPaperScissors::wins(const RPSChoice choice, const RPSChoice winner)
{
    return winner == winsThis(choice);
}
