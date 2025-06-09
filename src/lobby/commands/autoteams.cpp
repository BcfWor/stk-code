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

#include "autoteams.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include "utils/log.hpp"
#include "race/race_manager.hpp"
#include <parser/argline_parser.hpp>
#include <string>

static const char* const LOGNAME = "AutoteamsCommand";

// TODO: AutoTeams, this code is so big for no reason...

static
void display_team_into(std::vector<std::string>& team, std::stringstream& team_display)
{
	for (size_t i = 0; i < team.size(); i++) 
	{
		std::string display_name = team[i];
		if (display_name.length() > 10) 
		{
			display_name = display_name.substr(0, 5) + "...";
		}
		team_display << display_name;
		if (i < team.size() - 1) 
		{
			team_display << ", ";
		}
	}
}

bool AutoteamsCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish(); // Do not allow more arguments
    ServerLobby* const lobby = stk_ctx->get_lobby();

    if (RaceManager::get()->getMinorMode() != RaceManager::MINOR_MODE_SOCCER)
    {
        ctx->write("This command is only for soccer mode.");
        ctx->flush();
        return false;
    }
    else if (lobby->getCurrentState() != ServerLobby::WAITING_FOR_START_GAME)
    {
        ctx->write("Auto team generation not possible during game.");
        ctx->flush();
        return false;
    }

    CMD_VOTABLE(data, true);
    CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);

    auto elorank = std::make_pair(0U, 1500);
    std::string msg = "";
    auto peers = STKHost::get()->getPeers();
    std::vector <std::pair<std::string, int>> player_vec;
    for (auto peer : peers)
    {
        if (!peer->alwaysSpectate())
        {
            for (auto player : peer->getPlayerProfiles())
            {
                std::string username = StringUtils::wideToUtf8(player->getName());
                // TODO: use soccer_elo_ranking from ServerLobby from that,
                // which would be able to fetch ranking from the database
                std::ifstream file(ServerConfig::m_soccer_ranking_path.c_str());
                std::string line;
                bool found = false;
                int default_elo = 1500;
                while (std::getline(file, line))
                {
                    std::istringstream iss(line);
                    std::string name;
                    int games;
                    float team_size, goals_per_game, win_rate;
                    int elo;
                    iss >> name >> games >> team_size >> goals_per_game >> win_rate >> elo;
                    if (name == username)
                    {
                        player_vec.push_back(std::pair<std::string, int>(username, elo));
                        found = true;
                        break;
                    }
                }
                if (!found)
                    player_vec.push_back(std::pair<std::string, int>(username, default_elo));
                msg = "Player " + username + " will be sent into a team.";
                Log::info(LOGNAME, msg.c_str());
            }
        }
	}
    int min = 0;
    std::vector <std::pair<std::string, int>> player_copy = player_vec;
    if (player_vec.size() % 2 == 1)  // in this case the number of players in uneven. In this case ignore the worst noob.
    {
        for (int i3 = 0; i3 < player_copy.size(); i3++)
        {
            if (player_copy[i3].second <= player_copy[min].second)
            {
                min = i3;
            }
        }
        player_copy.erase(player_copy.begin() + min);
        int min_idx = std::min(min, (int)player_vec.size() - 1);
        msg = "Player " + player_vec[min_idx].first + " has minimal ELO.";
        Log::info(LOGNAME, msg.c_str());
    }
	lobby->m_team_option_a = lobby->createBalancedTeams(player_copy);
	lobby->m_team_option_b = lobby->createAlternativeTeams(player_copy);
	lobby->m_min_player_idx = min;
	lobby->m_player_vec = player_vec;
	std::stringstream team_display;
	team_display << "Proposed Team Options:\n\n";
	team_display << "Team Option A:\n";
	team_display << "Red Team: ";
    display_team_into(lobby->m_team_option_a.first, team_display);
	team_display << "\nBlue Team: ";
    display_team_into(lobby->m_team_option_a.second, team_display);
	team_display << "\n\nTeam Option B:\n";
	team_display << "Red Team: ";
    display_team_into(lobby->m_team_option_b.first, team_display);
	team_display << "\nBlue Team: ";
    display_team_into(lobby->m_team_option_b.second, team_display);
	team_display << "\n\nVote with /a for Team Option A or /b for Team Option B.";
        lobby->startTeamSelectionVote();
	std::string team_display_str = team_display.str();
	lobby->sendStringToAllPeers(team_display_str);
    return true;
}

bool AutoteamsVariantVoteCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish();

    ServerLobby* const lobby = stk_ctx->get_lobby();

    if (stk_ctx->getVeto() >= m_min_veto)
    {
        // forcefully apply
        lobby->applyTeamSelection(m_variant_one);
        return true;
    }

    STKPeer* const peer = stk_ctx->get_peer();

    if (peer)
    {
        lobby->handleTeamSelectionVote(peer, m_variant_one);
    }
    return true;
}
