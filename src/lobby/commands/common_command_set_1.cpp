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

// This file groups up standard small STK commands, such as spectate

#include "context.hpp"
#include "datetime.hpp"
#include "lobby/commands/speedstats.hpp"
#include "modes/soccer_world.hpp"
#include "quit.hpp"
#include "lobby/player_queue.hpp"
#include "lobby/stk_command.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "spectate.hpp"
#include "goalhistory.hpp"
#include "broadcast.hpp"
#include "lobby/stk_command_context.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "network/stk_peer.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/game_setup.hpp"
#include "utils/stk_process.hpp"
#include "utils/string_utils.hpp"
#include <unordered_map>

bool SpectateCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STKCommandContext* const stk_c = dynamic_cast<STKCommandContext*>(ctx);
    ServerLobbyCommands::DispatchData* const dispatch_data =
        reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);
    if (!stk_c || !data)
        return false;

    CMD_VOTABLE(data, false);

    if (!stk_c->testPlayerOnly()) return false;

    auto parser = ctx->get_parser();

    bool state = false;
    // spectate: y / n
    *parser >> state;

    parser->parse_finish();

    STKPeer* const peer = stk_c->get_peer();
    ServerLobby* const lobby = stk_c->get_lobby();

    if (lobby->getGameSetup()->isGrandPrix() || !ServerConfig::m_live_players)
    {
        ctx->write("Server doesn't support spectate");
        ctx->flush();
        return false;
    }

    // Delete the snippet below to allow spectate command during an active game
    if (lobby->getCurrentState() != ServerLobby::ServerState::WAITING_FOR_START_GAME)
    {
        ctx->write("Use this command before game started");
        ctx->flush();
        return false;
    }
    
    if (state == peer->alwaysSpectate())
    {
        ctx->write("You're already ");
        if (!state) ctx->write("not ");
        ctx->write("spectating.");
        ctx->flush();
        return false;
    }

    if (state)
    {
        // This command is a friend of the ServerLobby, and it can access its private members such as game setup
        if (lobby->m_process_type == PT_CHILD &&
                peer->getHostId() == lobby->m_client_server_host_id.load())
        {
            ctx->write("Graphical client server cannot spectate");
            ctx->flush();
            return false;
        }
        peer->setAlwaysSpectate(ASM_COMMAND);
    }
    else
        peer->setAlwaysSpectate(ASM_NONE);

    // Update hooks before the lobby updates the player list
    // When the player goes into spectator mode, they are no longer
    // eligible to play the game, obviously, spectators aren't playing.
    const PeerEligibility old_el = peer->getEligibility();
    peer->testEligibility();
    LobbyPlayerQueue::get()->onPeerEligibilityChange(dispatch_data->m_peer_wkptr.lock(), old_el);
    lobby->updatePlayerList();

    return true;
}

bool QuitCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STKCommandContext* const stk_c = dynamic_cast<STKCommandContext*>(ctx);
    if (!stk_c)
        return false;

    CMD_VOTABLE(data, false);

    if (!stk_c->testConsoleOnly()) return false;

    ctx->write("Shutting down the server.");
    ctx->flush();

    STKHost::get()->requestShutdown();

    return false;
}

bool DatetimeCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
   const std::unordered_map<std::string, double> TZ_OFFSETS = {
      // America (DST and Std.)
      {"EST", -5}, {"EDT", -4},
      {"CST", -6}, {"CDT", -5},
      {"MST", -7}, {"MDT", -6},
      {"PST", -8}, {"PDT", -7},
      {"AKST", -9}, {"AKDT", -8},
      {"HST", -10}, {"HDT", -9},
      
      // europe (DST and Std.)
      {"GMT",0}, {"BST",1},
      {"CET",1}, {"CEST",2},
      {"EET",2}, {"EEST",3},
      {"WET", 0}, {"WEST", 1},
      {"MSK", 3},

      // Asia
      {"AFT", 4.5},
      {"IST", 5.5},
      {"PKT", 5},
      {"IRST", 3.5 }, { "IRDT", 4.5 },
      {"CST", 8},
      {"CDT", 8},
      {"JST", 9},
      {"KST", 9},
      {"SGT", 8},
      {"ICT", 7},
      {"MST", 5.5},
      {"PST", 8},
      {"LKT", 5.5},
      
      // countries
      {"BR", -3},
      {"IN", 5.5},
      {"AR", -3},
      {"CH", 8},
      {"FR", 1},
      {"IT", 1},
      {"RU", 3},
      {"ES", 1},
      {"PL", 1},
      {"MX", -6},
      {"NL", 1},
      {"CA", -5},
      {"DE", 1},
      {"US", -5},
      {"JP", 9},
      {"GB", 0},
      {"AU", 10},
      {"ZA", 2},
      {"KR", 9},
      {"EG", 2},
      {"NG", 1},
      {"TR", 3},
      {"SE", 1},
      {"FI", 2},
      {"BE", 1},
      {"PT", 0},
      {"DK", 1} 
   };

   auto parser = ctx->get_parser();

   // Use argv[1] as timezone
   std::string tz;

   *parser >> tz;

   parser->parse_finish();

   std::transform(tz.begin(), tz.end(), tz.begin(), ::toupper);

   auto it = TZ_OFFSETS.find(tz);
   if (it != TZ_OFFSETS.end())
   {
   // get time from server
   time_t now = time(nullptr);
       tm *gmtm = gmtime(&now);
       // apply manually
       int hours_offset = static_cast<int>(it->second);
       int minutes_offset = static_cast<int>((it->second - hours_offset) * 60);

       gmtm->tm_hour += hours_offset;
       gmtm->tm_min += minutes_offset;
       time_t adjusted = mktime(gmtm);
       gmtm = gmtime(&adjusted);
       char buffer[256];
       strftime(buffer, sizeof(buffer), "%H:%M:%S | %a, %d-%m-%Y", gmtm);

       ctx->nprintf("[TIME] %s (UTC%+d:%02d) %s", 550, tz.c_str(), hours_offset, abs(minutes_offset), buffer);
       // send msg
       ctx->flush();
       return true;
   }
   else
   {
       std::string valid_tz;

       ctx->write("Invalid timezone or country code. \n"
           "Valid codes: ");
       bool nfirst = false;
       for (const auto& pair : TZ_OFFSETS) {
           if (nfirst)
               ctx->write(", ");
           ctx->write(pair.first);
           nfirst = true;
       }
       ctx->flush();
       return false;
   }
}
bool GoalHistoryCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    std::string team_variant;
    *parser >> team_variant;
    parser->parse_finish();

    team_variant = StringUtils::toLowerCase(team_variant);

    if (team_variant.empty())
    {
        ctx->write("Specify team name.");
        ctx->flush();
        return false;
    }

    std::stringstream ss;
    KartTeam team = KART_TEAM_NONE;

    if (team_variant[0] == 'r')
        team = KART_TEAM_RED;
    else if (team_variant[0] == 'b')
        team = KART_TEAM_BLUE;
    else
    {
        ctx->write("Specify red or blue.");
        ctx->flush();
        return false;
    }

    GoalHistory::showTeamGoalHistory(ss, team);
    *ctx << ss;
    ctx->flush();
    return true;
}
bool BroadcastCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    std::string message;
    parser->parse_full(message, true);

    ServerLobby* const lobby = stk_ctx->get_lobby();

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);
    if (message.empty())
    {
        ctx->write("No message to broadcast.");
        ctx->flush();
        return false;
    }

    lobby->sendStringToAllPeers(message);
    return true;
}
bool SpeedStatsCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    auto parser = ctx->get_parser();
    parser->parse_finish();
    STKHost* const host = STKHost::get();

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);
    ctx->nprintf("Upload speed (KBps): %f   Download speed (KBps): %f", 512,
        (float)host->getUploadSpeed() / 1024.0f,
        (float)host->getDownloadSpeed() / 1024.0f);
    ctx->flush();

    return true;
}
