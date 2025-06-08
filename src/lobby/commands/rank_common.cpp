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

#include "rank_common.hpp"
#include "network/server_config.hpp"
#include <fstream>
#include <sstream>

// TODO: Use database backend for ranking instead

bool dumpSoccerRankingListInto(nnwcli::CommandExecutorContext* ctx, unsigned int page, unsigned int element_per_page)
{
	ctx->nprintf("Soccer rankings (page %u):\n", 64, page);
	std::ifstream file(ServerConfig::m_soccer_ranking_path.c_str());
	std::string line;
	int rank = 1;
	int start = element_per_page * (page - 1);
	int count = 0;
	while (std::getline(file, line) && count < element_per_page)
	{
		if (rank <= start)
		{
			rank++;
			continue;
		}
		std::istringstream iss(line);
		std::string name;
		float games, avg_team, goals, winrate;
		int elo;
		iss >> name >> games >> avg_team >> goals >> winrate >> elo;
		ctx->nprintf("%s #%d: %s (ELO %d)", 255,
				"", rank, name.c_str(), elo);
		if (count != element_per_page - 1)
			*ctx << "\n";
		count++;
		rank++;
	}
	if (count == 0)
		ctx->write("Rankings are currently unavailable.");
    ctx->flush();

    return true;
}
bool dumpSoccerRankingEntryInto(nnwcli::CommandExecutorContext* ctx, const std::string playername)
{
    std::ifstream file(ServerConfig::m_soccer_ranking_path.c_str());
    std::string line;
    int rank = 1;
    bool found = false;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string name;
        std::string games_str, team_str, goals_str, win_str, elo_str;
        iss >> name >> games_str >> team_str >> goals_str >> win_str >> elo_str;
        if (name == playername)
        {
            ctx->nprintf(
                    "Player: %s\n"
                    "Played Games: %s\n"
                    "Average team size: %s\n"
                    "Goals per Game: %s\n"
                    "Winning rate: %s\n"
                    "ELO: %s", 1024,
                    name.c_str(),
                    games_str.c_str(),
                    team_str.c_str(),
                    goals_str.c_str(),
                    win_str.c_str(),
                    elo_str.c_str());
            found = true;
            break;
        }
        rank++;
    }
    if (!found)
        ctx->write("No records for the player.");
    ctx->flush();

    return true;
}
