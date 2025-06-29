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

#include "network/protocols/server_lobby.hpp"

#include "addons/addon.hpp"
#include "config/user_config.hpp"
#include "irrString.h"
#include "items/network_item_manager.hpp"
#include "items/powerup.hpp"
#include "items/powerup_manager.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/player_controller.hpp"
#include "karts/kart_properties.hpp"
#include "karts/kart_properties_manager.hpp"
#include "karts/official_karts.hpp"
#include "lobby/player_queue.hpp"
#include "lobby/rps_challenge.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "modes/capture_the_flag.hpp"
#include "modes/linear_world.hpp"
#include "modes/soccer_world.hpp"
#include "network/crypto.hpp"
#include <cstdint>

#ifdef ENABLE_SQLITE3
#include "network/database/sqlite_database.hpp"
#endif

#include "network/event.hpp"
#include "network/game_setup.hpp"
#include "network/network.hpp"
#include "network/network_config.hpp"
#include "network/network_player_profile.hpp"
#include "network/peer_vote.hpp"
#include "network/protocol_manager.hpp"
#include "network/protocols/connect_to_peer.hpp"
#include "network/protocols/game_protocol.hpp"
#include "network/protocols/game_events_protocol.hpp"
#include "network/protocols/global_log.hpp"
#include "network/protocols/ranking.hpp"
#include "network/race_event_manager.hpp"
#include "network/remote_kart_info.hpp"
#include "network/server_config.hpp"
#include "network/socket_address.hpp"
#include "network/server.hpp"
#include "network/stk_host.hpp"
#include "network/stk_ipv6.hpp"
#include "network/stk_peer.hpp"
#include "online/online_profile.hpp"
#include "online/request_manager.hpp"
#include "online/xml_request.hpp"
#include "race/tiers_roulette.hpp"
#include "tracks/check_manager.hpp"
#include "tracks/track.hpp"
#include "tracks/track_object.hpp"
#include "tracks/track_manager.hpp"
#include "utils/log.hpp"
#include "utils/random_generator.hpp"
#include "utils/string_utils.hpp"
#include "utils/time.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <array>
#include <memory>
#include <cstdio>
#include "replay/replay_recorder.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include "modes/world.hpp"
#include "io/file_manager.hpp"
#include "io/file_manager.hpp"
#include "io/file_manager.hpp"
#include "utils/time.hpp"
#include "modes/soccer_roulette.hpp"
#include <algorithm>
#include <fstream>
#include <random>
int ServerLobby::m_fixed_laps = -1;
// ========================================================================
class SubmitRankingRequest : public Online::XMLRequest
{
public:
    SubmitRankingRequest(const RankingEntry& entry,
                         const std::string& country_code)
        : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY)
    {
        addParameter("id", entry.online_id);
        addParameter("scores", entry.score);
        addParameter("max-scores", entry.max_score);
        addParameter("num-races-done", entry.races);
        addParameter("raw-scores", entry.raw_score);
        addParameter("rating-deviation", entry.deviation);
        addParameter("disconnects", entry.disconnects);
        addParameter("country-code", country_code);
    }
    virtual void afterOperation()
    {
        Online::XMLRequest::afterOperation();
        const XMLNode* result = getXMLData();
        std::string rec_success;
        if (!(result->get("success", &rec_success) &&
            rec_success == "yes"))
        {
            Log::error("ServerLobby", "Failed to submit scores.");
        }
    }
};   // UpdatePlayerRankingRequest

// ========================================================================

// We use max priority for all server requests to avoid downloading of addons
// icons blocking the poll request in all-in-one graphical client server

/** This is the central game setup protocol running in the server. It is
 *  mostly a finite state machine. Note that all nodes in ellipses and light
 *  grey background are actual states; nodes in boxes and white background 
 *  are functions triggered from a state or triggering potentially a state
 *  change.
 \dot
 digraph interaction {
 node [shape=box]; "Server Constructor"; "playerTrackVote"; "connectionRequested"; 
                   "signalRaceStartToClients"; "startedRaceOnClient"; "loadWorld";
 node [shape=ellipse,style=filled,color=lightgrey];

 "Server Constructor" -> "INIT_WAN" [label="If WAN game"]
 "Server Constructor" -> "WAITING_FOR_START_GAME" [label="If LAN game"]
 "INIT_WAN" -> "GETTING_PUBLIC_ADDRESS" [label="GetPublicAddress protocol callback"]
 "GETTING_PUBLIC_ADDRESS" -> "WAITING_FOR_START_GAME" [label="Register server"]
 "WAITING_FOR_START_GAME" -> "connectionRequested" [label="Client connection request"]
 "connectionRequested" -> "WAITING_FOR_START_GAME"
 "WAITING_FOR_START_GAME" -> "SELECTING" [label="Start race from authorised client"]
 "SELECTING" -> "SELECTING" [label="Client selects kart, #laps, ..."]
 "SELECTING" -> "playerTrackVote" [label="Client selected track"]
 "playerTrackVote" -> "SELECTING" [label="Not all clients have selected"]
 "playerTrackVote" -> "LOAD_WORLD" [label="All clients have selected; signal load_world to clients"]
 "LOAD_WORLD" -> "loadWorld"
 "loadWorld" -> "WAIT_FOR_WORLD_LOADED" 
 "WAIT_FOR_WORLD_LOADED" -> "WAIT_FOR_WORLD_LOADED" [label="Client or server loaded world"]
 "WAIT_FOR_WORLD_LOADED" -> "signalRaceStartToClients" [label="All clients and server ready"]
 "signalRaceStartToClients" -> "WAIT_FOR_RACE_STARTED"
 "WAIT_FOR_RACE_STARTED" ->  "startedRaceOnClient" [label="Client has started race"]
 "startedRaceOnClient" -> "WAIT_FOR_RACE_STARTED" [label="Not all clients have started"]
 "startedRaceOnClient" -> "DELAY_SERVER" [label="All clients have started"]
 "DELAY_SERVER" -> "DELAY_SERVER" [label="Not done waiting"]
 "DELAY_SERVER" -> "RACING" [label="Server starts race now"]
 }
 \enddot


 *  It starts with detecting the public ip address and port of this
 *  host (GetPublicAddress).
 */
ServerLobby::ServerLobby() : LobbyProtocol()
{
    m_client_server_host_id.store(0);
    m_lobby_players.store(0);
    m_current_ai_count.store(0);
    std::vector<int> all_t =
        track_manager->getTracksInGroup("standard");
    std::vector<int> all_arenas =
        track_manager->getArenasInGroup("standard", false);
    std::vector<int> all_soccers =
        track_manager->getArenasInGroup("standard", true);
    all_t.insert(all_t.end(), all_arenas.begin(), all_arenas.end());
    all_t.insert(all_t.end(), all_soccers.begin(), all_soccers.end());

    m_official_kts.first = OfficialKarts::getOfficialKarts();
    for (int track : all_t)
    {
        Track* t = track_manager->getTrack(track);
        if (!t->isAddon())
            m_official_kts.second.insert(t->getIdent());
    }
    updateAddons();

    m_rs_state.store(RS_NONE);
    m_last_success_poll_time.store(StkTime::getMonoTimeMs() + 30000);
    m_last_unsuccess_poll_time = StkTime::getMonoTimeMs();
    m_server_owner_id.store(-1);
    m_registered_for_once_only = false;
    setHandleDisconnections(true);
    m_state = SET_PUBLIC_ADDRESS;
    m_save_server_config = true;
    if (ServerConfig::m_ranked)
    {
        Log::info("ServerLobby", "This server will submit ranking scores to "
            "the STK addons server. Don't bother hosting one without the "
            "corresponding permissions, as they would be rejected.");

        m_ranking = std::make_shared<Ranking>();
    }
    m_result_ns = getNetworkString();
    m_result_ns->setSynchronous(true);
    m_items_complete_state = new BareNetworkString();
    m_server_id_online.store(0);
    m_max_players         = ServerConfig::m_server_max_players;
    m_powerupper_active   = false;
    m_difficulty.store(ServerConfig::m_server_difficulty);
    m_game_mode.store(ServerConfig::m_server_mode);
    m_default_vote = new PeerVote();
    m_allow_powerupper = ServerConfig::m_allow_powerupper;
    m_show_elo = ServerConfig::m_show_elo;
    m_show_rank = ServerConfig::m_show_rank;
    m_last_wanrefresh_res = nullptr;
    m_last_wanrefresh_requester.reset();
    m_last_wanrefresh_is_peer.store(false);
    m_random_karts_enabled = false;
    RaceManager::get()->setInfiniteMode(ServerConfig::m_infinite_game, false);

#ifdef ENABLE_SQLITE3
    m_db = new SQLiteDatabase();
    m_db->init();
#endif

    LobbyPlayerQueue::create();
    m_rps_challenges.clear();

    m_jumble_rng.seed(std::random_device{}());
    loadJumbleWordList();
}   // ServerLobby

//-----------------------------------------------------------------------------
/** Destructor.
 */
ServerLobby::~ServerLobby()
{
    LobbyPlayerQueue::destroy();
    if (m_server_id_online.load() != 0)
    {
        // For child process the request manager will keep on running
        unregisterServer(m_process_type == PT_MAIN ? true : false/*now*/);
    }
    delete m_result_ns;
    delete m_items_complete_state;
    if (m_save_server_config)
        ServerConfig::writeServerConfigToDisk();
    delete m_default_vote;

    // This to be moved into the destructor of the network/database/abstract_database.hpp
    // and the sqlite3 implementation as well, to their respective abstraction layers
#ifdef ENABLE_SQLITE3
    delete m_db;
#endif
}   // ~ServerLobby

//-----------------------------------------------------------------------------
void ServerLobby::initServerStatsTable()
{
    // This to be moved into the method for constructing the network/database/abstract_database.hpp
    // and the sqlite3 implementation as well, to their respective abstraction layers

#ifdef ENABLE_SQLITE3
    m_db->initServerStatsTable();
#endif
}   // initServerStatsTable

void ServerLobby::updateAddons()
{
    m_addon_kts.first.clear();
    m_addon_kts.second.clear();
    m_addon_arenas.clear();
    m_addon_soccers.clear();

    std::set<std::string> total_addons;
    for (unsigned i = 0; i < kart_properties_manager->getNumberOfKarts(); i++)
    {
        const KartProperties* kp =
            kart_properties_manager->getKartById(i);
        if (kp->isAddon())
            total_addons.insert(kp->getIdent());
    }
    for (unsigned i = 0; i < track_manager->getNumberOfTracks(); i++)
    {
        const Track* track = track_manager->getTrack(i);
        if (track->isAddon())
            total_addons.insert(track->getIdent());
    }

    for (auto& addon : total_addons)
    {
        const KartProperties* kp = kart_properties_manager->getKart(addon);
        if (kp && kp->isAddon())
        {
            m_addon_kts.first.insert(kp->getIdent());
            continue;
        }
        Track* t = track_manager->getTrack(addon);
        if (!t || !t->isAddon() || t->isInternal())
            continue;
        if (t->isArena())
            m_addon_arenas.insert(t->getIdent());
        else if (t->isSoccer())
            m_addon_soccers.insert(t->getIdent());
        else
            m_addon_kts.second.insert(t->getIdent());
    }

    std::vector<std::string> all_k;
    for (unsigned i = 0; i < kart_properties_manager->getNumberOfKarts(); i++)
    {
        const KartProperties* kp = kart_properties_manager->getKartById(i);
        if (kp->isAddon())
            all_k.push_back(kp->getIdent());
    }
    std::set<std::string> oks = OfficialKarts::getOfficialKarts();
    if (all_k.size() >= 65536 - (unsigned)oks.size())
        all_k.resize(65535 - (unsigned)oks.size());
    for (const std::string& k : oks)
        all_k.push_back(k);
    if (ServerConfig::m_live_players)
        m_available_kts.first = m_official_kts.first;
    else
        m_available_kts.first = { all_k.begin(), all_k.end() };
}   // updateAddons

//-----------------------------------------------------------------------------
/** Called whenever server is reset or game mode is changed.
 */
void ServerLobby::updateTracksForMode()
{
    auto all_t = track_manager->getAllTrackIdentifiers();
    if (all_t.size() >= 65536)
        all_t.resize(65535);
    m_available_kts.second = { all_t.begin(), all_t.end() };
    RaceManager::MinorRaceModeType m =
        ServerConfig::getLocalGameMode(m_game_mode.load()).first;
    switch (m)
    {
        case RaceManager::MINOR_MODE_NORMAL_RACE:
        case RaceManager::MINOR_MODE_TIME_TRIAL:
        case RaceManager::MINOR_MODE_FOLLOW_LEADER:
        {
            auto it = m_available_kts.second.begin();
            while (it != m_available_kts.second.end())
            {
                Track* t =  track_manager->getTrack(*it);
                if (t->isArena() || t->isSoccer() || t->isInternal())
                {
                    it = m_available_kts.second.erase(it);
                }
                else
                    it++;
            }
            break;
        }
        case RaceManager::MINOR_MODE_FREE_FOR_ALL:
        case RaceManager::MINOR_MODE_CAPTURE_THE_FLAG:
        {
            auto it = m_available_kts.second.begin();
            while (it != m_available_kts.second.end())
            {
                Track* t =  track_manager->getTrack(*it);
                if (RaceManager::get()->getMinorMode() ==
                    RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
                {
                    if (!t->isCTF() || t->isInternal())
                    {
                        it = m_available_kts.second.erase(it);
                    }
                    else
                        it++;
                }
                else
                {
                    if (!t->isArena() ||  t->isInternal())
                    {
                        it = m_available_kts.second.erase(it);
                    }
                    else
                        it++;
                }
            }
            break;
        }
        case RaceManager::MINOR_MODE_SOCCER:
        {
            auto it = m_available_kts.second.begin();
            while (it != m_available_kts.second.end())
            {
                Track* t =  track_manager->getTrack(*it);
                if (!t->isSoccer() || t->isInternal())
                {
                    it = m_available_kts.second.erase(it);
                }
                else
                    it++;
            }
            break;
        }
        default:
            assert(false);
            break;
    }

}   // updateTracksForMode

//-----------------------------------------------------------------------------
void ServerLobby::setup()
{
    LobbyProtocol::setup();
    m_battle_hit_capture_limit = 0;
    m_battle_time_limit = 0.0f;
    m_item_seed = 0;
    m_winner_peer_id = 0;
    m_client_starting_time = 0;
    m_ai_count = 0;
    auto players = STKHost::get()->getPlayersForNewGame();
    if (m_game_setup->isGrandPrix() && !m_game_setup->isGrandPrixStarted())
    {
        for (auto player : players)
            player->resetGrandPrixData();
    }
    if (!m_game_setup->isGrandPrix() || !m_game_setup->isGrandPrixStarted())
    {
        for (auto player : players)
            player->setKartName("");
    }
    if (auto ai = m_ai_peer.lock())
    {
        for (auto player : ai->getPlayerProfiles())
            player->setKartName("");
    }
    for (auto ai : m_ai_profiles)
        ai->setKartName("");

    StateManager::get()->resetActivePlayers();
    // We use maximum 16bit unsigned limit
    auto all_k = kart_properties_manager->getAllAvailableKarts();
    if (all_k.size() >= 65536)
        all_k.resize(65535);
    if (ServerConfig::m_live_players)
        m_available_kts.first = m_official_kts.first;
    else
        m_available_kts.first = { all_k.begin(), all_k.end() };
    NetworkConfig::get()->setTuxHitboxAddon(ServerConfig::m_live_players);
    updateTracksForMode();

    m_server_has_loaded_world.store(false);

    // Initialise the data structures to detect if all clients and 
    // the server are ready:
    resetPeersReady();
    setPoleEnabled(false);
    RaceManager::get()->setRecordRace(m_replay_requested);
    m_timeout.store(std::numeric_limits<int64_t>::max());
    m_server_started_at = m_server_delay = 0;
    Log::info("ServerLobby", "Resetting the server to its initial state.");

    if (ServerConfig::m_tiers_roulette)
        tiers_roulette->applyChanges(this, RaceManager::get(), nullptr);
}   // setup

//-----------------------------------------------------------------------------
bool ServerLobby::notifyEvent(Event* event)
{
    assert(m_game_setup); // assert that the setup exists
    if (event->getType() != EVENT_TYPE_MESSAGE)
        return true;

    NetworkString &data = event->data();
    assert(data.size()); // message not empty
    uint8_t message_type;
    message_type = data.getUInt8();
    Log::info("ServerLobby", "Synchronous message of type %d received.",
              message_type);
    switch (message_type)
    {
    case LE_RACE_FINISHED_ACK: playerFinishedResult(event);   break;
    case LE_LIVE_JOIN:         liveJoinRequest(event);        break;
    case LE_CLIENT_LOADED_WORLD: finishedLoadingLiveJoinClient(event); break;
    case LE_KART_INFO: handleKartInfo(event); break;
    case LE_CLIENT_BACK_LOBBY: clientInGameWantsToBackLobby(event); break;
    default: Log::error("ServerLobby", "Unknown message of type %d - ignored.",
                        message_type);
             break;
    }   // switch message_type
    return true;
}   // notifyEvent

//-----------------------------------------------------------------------------
void ServerLobby::handleChat(Event* event)
{
    if (!checkDataSize(event, 1) || !ServerConfig::m_chat) return;

    // Update so that the peer is not kicked
    event->getPeer()->updateLastActivity();
    const bool sender_in_lobby = event->getPeer()->isWaitingForGame();

    int64_t last_message = event->getPeer()->getLastMessage();
    int64_t elapsed_time = (int64_t)StkTime::getMonoTimeMs() - last_message;

    // Read ServerConfig for formula and details
    if (ServerConfig::m_chat_consecutive_interval > 0 &&
        elapsed_time < ServerConfig::m_chat_consecutive_interval * 1000)
        event->getPeer()->updateConsecutiveMessages(true);
    else
        event->getPeer()->updateConsecutiveMessages(false);

    if (ServerConfig::m_chat_consecutive_interval > 0 &&
        event->getPeer()->getConsecutiveMessages() >
        ServerConfig::m_chat_consecutive_interval / 2)
    {
        NetworkString* chat = getNetworkString();
        chat->setSynchronous(true);
        core::stringw warn = "Spam detected";
        chat->addUInt8(LE_CHAT).encodeString16(warn);
        event->getPeer()->sendPacket(chat, true/*reliable*/);
        delete chat;
        return;
    }

    core::stringw message;
    core::stringw sender_name;
    NetworkPlayerProfile* sender_profile = nullptr;

    event->data().decodeString16(&message, 360/*max_len*/);

    // Check if the message starts with "(the name of main profile): " to prevent
    // impersonation, see #5121.
    std::string message_utf8 = StringUtils::wideToUtf8(message);
    std::string prefix = StringUtils::wideToUtf8(
        event->getPeer()->getPlayerProfiles()[0]->getName()) + ": ";
    
    if (!StringUtils::startsWith(message_utf8, prefix))
    {
        NetworkString* chat = getNetworkString();
        chat->setSynchronous(true);
        core::stringw warn = "Don't try to impersonate others!";
        chat->addUInt8(LE_CHAT).encodeString16(warn);
        event->getPeer()->sendPacket(chat, true/*reliable*/);
        delete chat;
        return;
    }

    KartTeam target_team = KART_TEAM_NONE;

    if (event->data().size() > 0)
        target_team = (KartTeam)event->data().getUInt8();

    // determine and verify the name of the sender
    STKPeer* sender = event->getPeer();
    int msg_at = message.find(L": ");
    if (msg_at != -1)
    {
        sender_name = message.subString(0, msg_at);
    }
    else if (!sender->hasPlayerProfiles())
        // Peer cannot send messages without player profiles
        return;
    else
    {
        sender_name = sender->getPlayerProfiles()[0]->getName();
        message = sender_name + L": " + message;
    }

    if (message.size() > 0)
    {
        // Red or blue square emoji
        if (target_team == KART_TEAM_RED)
            message = StringUtils::utf32ToWide({0x1f7e5, 0x20}) + message;
        else if (target_team == KART_TEAM_BLUE)
            message = StringUtils::utf32ToWide({0x1f7e6, 0x20}) + message;

        // teamchat
        bool team_speak = m_team_speakers.find(sender) != m_team_speakers.end();

        // make a function of it for god sake, or at least a macro
        team_speak &= (
            RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER ||
            RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_CAPTURE_THE_FLAG
            );
        std::set<KartTeam> teams;
        for (auto& profile : sender->getPlayerProfiles())
        {
            if (!sender_profile && sender_name == profile->getName())
            {
                sender_profile = profile.get();
            }
            teams.insert(profile->getTeam());
        }

        // check if the sender_profile is authorised to send the message
        if (sender->hasRestriction(PRF_NOCHAT) ||
                sender->getPermissionLevel() <= PERM_NONE)
        {
            // very evil chat log
            Log::info("ServerLobby", "[MUTED] %s", StringUtils::wideToUtf8(message).c_str());
            NetworkString* const response = getNetworkString();
            response->setSynchronous(true);
            response->addUInt8(LE_CHAT);

            // very evil if you ask
            if (ServerConfig::m_shadow_nochat)
                response->encodeString16(message);
            else
                response->encodeString16(L"You are not allowed to send chat messages.");

            sender->sendPacket(response, true/*reliable*/);
            delete response;

            return;
        }
        // evil chat log
        Log::info("ServerLobby", "[CHAT] %s", StringUtils::wideToUtf8(message).c_str());

        NetworkString* chat = getNetworkString();
        chat->setSynchronous(true);
        chat->addUInt8(LE_CHAT).encodeString16(message);
        const bool game_started = m_state.load() != WAITING_FOR_START_GAME;
        const bool global_chat = ServerConfig::m_global_chat;

        STKHost::get()->sendPacketToAllPeersWith(
            [game_started, global_chat, sender_in_lobby, target_team, sender_name, team_speak, teams, this]
            (STKPeer* p)
            {
                if (game_started)
                {
                    // separates the chat between lobby peers, and ingame players/spectators
                    if (!global_chat && p->isWaitingForGame() != sender_in_lobby)
                        return false;
                    // when targeted towards one team, send it.
                    if (target_team != KART_TEAM_NONE)
                    {
                        if (p->isSpectator())
                            return false;
                        for (auto& player : p->getPlayerProfiles())
                        {
                            if (player->getTeam() == target_team
                                )
                                return true;
                        }
                        return false;
                    }
                }
                // /teamchat restrictions
                if (team_speak)
                {
                    for (auto& profile : p->getPlayerProfiles())
                        if (teams.count(profile->getTeam()) > 0
                            )
                            return true;
                    return false;
                }
                for (auto& peer : m_peers_muted_players)
                {
                    if (auto peer_sp = peer.first.lock())
                    {
                        if (peer_sp.get() == p &&
                            peer.second.find(sender_name) != peer.second.end())
                            return false;
                    }
                }
                return true;
            }, chat);
            event->getPeer()->updateLastMessage();
        delete chat;
    }
}   // handleChat

//-----------------------------------------------------------------------------
// FIXME: add "force" argument that avoids the check for manual team choosing
void ServerLobby::changeTeam(Event* event)
{
    if (!ServerConfig::m_team_choosing ||
        !RaceManager::get()->teamEnabled())
        return;
    if (!checkDataSize(event, 1)) return;
    NetworkString& data = event->data();
    uint8_t local_id = data.getUInt8();
    auto& player = event->getPeer()->getPlayerProfiles().at(local_id);

    // check if player can change teams
    if (event->getPeer()->hasRestriction(PRF_NOTEAM))
    {
        Log::info("ServerLobby",
                "Player %s tried to change teams without permission.",
                StringUtils::wideToUtf8(player->getName()).c_str());

        NetworkString* const response = getNetworkString();
        response->setSynchronous(true);
        response->addUInt8(LE_CHAT).encodeString16(
                L"You are not allowed to change teams.");
        event->getPeer()->sendPacket(response, true/*reliable*/);
        delete response;
        return;
    }

    auto red_blue = STKHost::get()->getAllPlayersTeamInfo();
    const auto team = player->getTeam();

    // reset pole voting if any
    bool has_pole = false;
    auto peer = event->getPeer();
    auto b = m_blue_pole_votes.find(peer);
    auto r = m_red_pole_votes.find(peer);
    if (b != m_blue_pole_votes.cend())
    {
        m_blue_pole_votes.erase(b);
        has_pole = true;
    }
    if (r != m_red_pole_votes.cend())
    {
        m_red_pole_votes.erase(r);
        has_pole = true;
    }

    if (has_pole)
    {
        core::stringw text = L"Your voting has been reset since you've changed your team."
            L" Please vote again:\n";
        // TODO:
        NetworkString* msg = getNetworkString();
        msg->setSynchronous(true);
        msg->addUInt8(LE_CHAT);
        text += formatTeammateList(
                STKHost::get()->getPlayerProfilesOfTeam(
                    team == KART_TEAM_BLUE ? KART_TEAM_RED : KART_TEAM_BLUE
                    ));
        msg->encodeString16(text);
        peer->sendPacket(msg, true/*reliable*/);
        delete msg;
    }

    // At most 7 players on each team (for live join)
    if (player->getTeam() == KART_TEAM_BLUE)
    {
        if (red_blue.first >= 7)
            return;
        player->setTeam(KART_TEAM_RED);
    }
    else
    {
        if (red_blue.second >= 7)
            return;
        player->setTeam(KART_TEAM_BLUE);
    }
    updatePlayerList();
}   // changeTeam
void ServerLobby::forceChangeTeam(NetworkPlayerProfile* const player, const KartTeam team)
{
    // reset pole voting if any
    bool has_pole = false;
    auto peer = player->getPeer().get();
    auto b = m_blue_pole_votes.find(peer);
    auto r = m_red_pole_votes.find(peer);

    player->setTeam(team);
    if (peer && peer->alwaysSpectate() && team != KART_TEAM_NONE)
        peer->setAlwaysSpectate(ASM_NONE);
    else if (peer && !peer->alwaysSpectate() && team == KART_TEAM_NONE)
        peer->setAlwaysSpectate(ASM_FULL);

    if (b != m_blue_pole_votes.cend())
    {
        m_blue_pole_votes.erase(b);
        has_pole = true;
    }
    if (r != m_red_pole_votes.cend())
    {
        m_red_pole_votes.erase(r);
        has_pole = true;
    }

    if (peer && has_pole)
    {
        core::stringw text = L"Your voting has been reset since your team has been changed. Please vote again:\n";
	text += formatTeammateList(STKHost::get()->getPlayerProfilesOfTeam(team));
	NetworkString* msg = getNetworkString();
	msg->setSynchronous(true);
	msg->addUInt8(LE_CHAT);
	msg->encodeString16(text);
	peer->sendPacket(msg, true/*reliable*/);
	delete msg;
    }

    updatePlayerList();

}  // forceChangeTeam

//-----------------------------------------------------------------------------
void ServerLobby::kickHost(Event* event)
{
    if (m_server_owner.lock() != event->getPeerSP())
        return;
    if (!checkDataSize(event, 4)) return;
    NetworkString& data = event->data();
    uint32_t host_id = data.getUInt32();
    std::shared_ptr<STKPeer> peer = STKHost::get()->findPeerByHostId(host_id);
    // Ignore kicking ai peer if ai handling is on
    if (peer && (!ServerConfig::m_ai_handling || !peer->isAIPeer()))
        peer->kick();
}   // kickHost

//-----------------------------------------------------------------------------
bool ServerLobby::notifyEventAsynchronous(Event* event)
{
    assert(m_game_setup); // assert that the setup exists
    if (event->getType() == EVENT_TYPE_MESSAGE)
    {
        NetworkString &data = event->data();
        assert(data.size()); // message not empty
        uint8_t message_type;
        message_type = data.getUInt8();
        Log::info("ServerLobby", "Message of type %d received.",
                  message_type);
        switch(message_type)
        {
	// le paquet envoyé au serveur
        case LE_CONNECTION_REQUESTED: connectionRequested(event); break;
        case LE_KART_SELECTION: kartSelectionRequested(event);    break;
        case LE_CLIENT_LOADED_WORLD: finishedLoadingWorldClient(event); break;
        case LE_VOTE: handlePlayerVote(event);                    break;
        case LE_KICK_HOST: kickHost(event);                       break;
        case LE_CHANGE_TEAM: changeTeam(event);                   break;
        case LE_REQUEST_BEGIN: startSelection(event);             break;
        case LE_CHAT: handleChat(event);                          break;
        case LE_CONFIG_SERVER: handleServerConfiguration(event);  break;
        case LE_CHANGE_HANDICAP: changeHandicap(event);           break;
        case LE_CLIENT_BACK_LOBBY:
            clientSelectingAssetsWantsToBackLobby(event);         break;
        case LE_REPORT_PLAYER: writePlayerReport(event);          break;
        case LE_ASSETS_UPDATE:
            handleAssets(event->data(), event->getPeerSP());        break;
        case LE_COMMAND:
            handleServerCommand(event, event->getPeerSP());       break;
        default:                                                  break;
        }   // switch
    } // if (event->getType() == EVENT_TYPE_MESSAGE)
    else if (event->getType() == EVENT_TYPE_DISCONNECTED)
    {
        clientDisconnected(event);
    } // if (event->getType() == EVENT_TYPE_DISCONNECTED)
    return true;
}   // notifyEventAsynchronous

//-----------------------------------------------------------------------------
#ifdef ENABLE_SQLITE3
/* Every 1 minute STK will poll database:
 * 1. Set disconnected time to now for non-exists host.
 * 2. Clear expired player reports if necessary
 * 3. Kick active peer from ban list
 */
void ServerLobby::pollDatabase()
{
    if (!ServerConfig::m_sql_management || !m_db->hasDatabase())
        return;

    if (!m_db->isTimeToPoll())
        return;

    m_db->updatePollTime();

    std::vector<AbstractDatabase::IpBanTableData> ip_ban_list =
            m_db->getIpBanTableData();
    std::vector<AbstractDatabase::Ipv6BanTableData> ipv6_ban_list =
            m_db->getIpv6BanTableData();
    std::vector<AbstractDatabase::OnlineIdBanTableData> online_id_ban_list =
            m_db->getOnlineIdBanTableData();

    for (std::shared_ptr<STKPeer>& p : STKHost::get()->getPeers())
    {
        if (p->isAIPeer())
            continue;
        bool is_kicked = false;
        std::string address = "";
        std::string reason = "";
        std::string description = "";

        if (p->getAddress().isIPv6())
        {
            address = p->getAddress().toString(false);
            if (address.empty())
                continue;
            for (auto& item: ipv6_ban_list)
            {
                if (insideIPv6CIDR(item.ipv6_cidr.c_str(), address.c_str()) == 1)
                {
                    is_kicked = true;
                    reason = item.reason;
                    description = item.description;
                    break;
                }
            }
        }
        else
        {
            uint32_t peer_addr = p->getAddress().getIP();
            address = p->getAddress().toString();
            for (auto& item: ip_ban_list)
            {
                if (item.ip_start <= peer_addr && item.ip_end >= peer_addr)
                {
                    is_kicked = true;
                    reason = item.reason;
                    description = item.description;
                    break;
                }
            }
        }
        if (!is_kicked && !p->getPlayerProfiles().empty())
        {
            uint32_t online_id = p->getPlayerProfiles()[0]->getOnlineId();
            for (auto& item: online_id_ban_list)
            {
                if (item.online_id == online_id)
                {
                    is_kicked = true;
                    reason = item.reason;
                    description = item.description;
                    break;
                }
            }
        }
        if (is_kicked)
        {
            Log::info("ServerLobby", "Kick %s, reason: %s, description: %s",
                address.c_str(), reason.c_str(), description.c_str());
            p->kick();
        }
    } // for p in peers

    m_db->clearOldReports();

    auto peers = STKHost::get()->getPeers();
    std::vector<uint32_t> hosts;
    if (!peers.empty())
    {
        for (auto& peer : peers)
        {
            if (!peer->isValidated())
                continue;
            hosts.push_back(peer->getHostId());
        }
    }
    m_db->setDisconnectionTimes(hosts);
}   // pollDatabase
#endif
//-----------------------------------------------------------------------------
void ServerLobby::writePlayerReport(Event* event)
{
#ifdef ENABLE_SQLITE3
    if (!m_db->hasDatabase() || !m_db->hasPlayerReportsTable())
        return;
    STKPeer* reporter = event->getPeer();
    if (!reporter->hasPlayerProfiles())
        return;
    auto reporter_npp = reporter->getPlayerProfiles()[0];

    uint32_t reporting_host_id = event->data().getUInt32();
    core::stringw info;
    event->data().decodeString16(&info);
    if (info.empty())
        return;

    auto reporting_peer = STKHost::get()->findPeerByHostId(reporting_host_id);
    if (!reporting_peer || !reporting_peer->hasPlayerProfiles())
        return;
    auto reporting_npp = reporting_peer->getPlayerProfiles()[0];

    bool written = m_db->writeReport(reporter, reporter_npp,
            reporting_peer.get(), reporting_npp, info);
    if (written)
    {
        NetworkString* success = getNetworkString();
        success->setSynchronous(true);
        success->addUInt8(LE_REPORT_PLAYER).addUInt8(1)
            .encodeString(reporting_npp->getName());
        event->getPeer()->sendPacket(success, true/*reliable*/);
        delete success;
    }
#endif
}   // writePlayerReport

void ServerLobby::sendWANListToPeer(std::shared_ptr<STKPeer> peer)
{
    Log::verbose("ServerLobby", "sendWANListToPeer");
    core::stringw responseMsg = L"[Currently played servers]\n";
    std::lock_guard<std::mutex> wr_lock(m_wanrefresh_lock);
    // send a message to whoever requested the message
    for (std::shared_ptr<Server> serverPtr : m_last_wanrefresh_res->m_servers)
    {
        auto players = serverPtr->getPlayers();
        if (players.empty() || serverPtr->isPasswordProtected())
            continue;

        RaceManager::MinorRaceModeType m =
            ServerConfig::getLocalGameMode(serverPtr->getServerMode()).first;

        int playerCount = players.size();

        responseMsg += StringUtils::getCountryFlag(serverPtr->getCountryCode());
        responseMsg += serverPtr->getName();
        responseMsg += L" (";
        responseMsg += playerCount;
        responseMsg += L"/";
        responseMsg += serverPtr->getMaxPlayers();
        responseMsg += L"), ";
        responseMsg += StringUtils::utf8ToWide(
                RaceManager::getIdentOf(m));
        responseMsg += L", ";
        if (serverPtr->isGameStarted())
        {
            Track* track = serverPtr->getCurrentTrack();
            if (track)
                responseMsg += StringUtils::utf8ToWide(track->getIdent());
            else
                responseMsg += L"(unknown track)";
        }
        else {
            responseMsg += L"(waiting for game)";
        }
        responseMsg += L"\n";
        
        // player list
        for (auto player : players)
        {
            responseMsg += std::get<1>(player);
            responseMsg += L" | ";
        }
        responseMsg += L"\n\n";
    }

    if (peer)
        sendStringToPeer(responseMsg, peer);
    else
    {
        auto ctx = ServerLobbyCommands::getNetworkConsoleContext();
        ctx->write(StringUtils::wideToUtf8(responseMsg));
        ctx->flush();
    }
}
//-----------------------------------------------------------------------------
/** Find out the public IP server or poll STK server asynchronously. */
void ServerLobby::asynchronousUpdate()
{
    if (m_rs_state.load() == RS_ASYNC_RESET)
    {
        resetVotingTime();
        resetServer();
        m_rs_state.store(RS_NONE);
    }

    for (auto it = m_peers_muted_players.begin();
        it != m_peers_muted_players.end();)
    {
        if (it->first.expired())
            it = m_peers_muted_players.erase(it);
        else
            it++;
    }

#ifdef ENABLE_SQLITE3
    pollDatabase();
#endif

    // Check if server owner has left
    updateServerOwner();

    if (ServerConfig::m_ranked && m_state.load() == WAITING_FOR_START_GAME)
        m_ranking->cleanup();

    if (allowJoinedPlayersWaiting() || (m_game_setup->isGrandPrix() &&
        m_state.load() == WAITING_FOR_START_GAME))
    {
        // Only poll the STK server if server has been registered.
        if (m_server_id_online.load() != 0 &&
            m_state.load() != REGISTER_SELF_ADDRESS)
            checkIncomingConnectionRequests();
        handlePendingConnection();
    }

    if (m_server_id_online.load() != 0 &&
        allowJoinedPlayersWaiting() &&
        StkTime::getMonoTimeMs() > m_last_unsuccess_poll_time &&
        StkTime::getMonoTimeMs() > m_last_success_poll_time.load() + 30000)
    {
        Log::warn("ServerLobby", "Trying auto server recovery.");
        // For auto server recovery wait 3 seconds for next try
        m_last_unsuccess_poll_time = StkTime::getMonoTimeMs() + 3000;
        registerServer(false/*first_time*/);
    }

    // Respond to asynchronous events
    if (m_last_wanrefresh_res && m_last_wanrefresh_res->m_servers.size() &&
            m_last_wanrefresh_res->m_list_updated.load())
    {
        if (m_last_wanrefresh_is_peer.load() &&
                m_last_wanrefresh_requester.expired())
            Log::verbose("ServerLobby", "last wanrefresh requester is expired");
        else
            sendWANListToPeer(m_last_wanrefresh_requester.lock());
        m_last_wanrefresh_res->m_list_updated.store(false);
    }

    switch (m_state.load())
    {
    case SET_PUBLIC_ADDRESS:
    {
        // In case of LAN we don't need our public address or register with the
        // STK server, so we can directly go to the accepting clients state.
        if (NetworkConfig::get()->isLAN())
        {
            m_state = WAITING_FOR_START_GAME;
            updatePlayerList();
            STKHost::get()->startListening();
            return;
        }
        auto ip_type = NetworkConfig::get()->getIPType();
        // Set the IPv6 address first for possible IPv6 only server
        if (isIPv6Socket() && ip_type >= NetworkConfig::IP_V6)
        {
            STKHost::get()->setPublicAddress(AF_INET6);
        }
        if (ip_type == NetworkConfig::IP_V4 ||
            ip_type == NetworkConfig::IP_DUAL_STACK)
        {
            STKHost::get()->setPublicAddress(AF_INET);
        }
        if (STKHost::get()->getPublicAddress().isUnset() &&
            STKHost::get()->getPublicIPv6Address().empty())
        {
            m_state = ERROR_LEAVE;
        }
        else
        {
            STKHost::get()->startListening();
            m_state = REGISTER_SELF_ADDRESS;
        }
        break;
    }
    case REGISTER_SELF_ADDRESS:
    {
        if (m_game_setup->isGrandPrixStarted() || m_registered_for_once_only)
        {
            m_state = WAITING_FOR_START_GAME;
            updatePlayerList();
            break;
        }
        // Register this server with the STK server. This will block
        // this thread, because there is no need for the protocol manager
        // to react to any requests before the server is registered.
        if (m_server_registering.expired() && m_server_id_online.load() == 0)
            registerServer(true/*first_time*/);

        if (m_server_registering.expired())
        {
            // Finished registering server
            if (m_server_id_online.load() != 0)
            {
                // For non grand prix server we only need to register to stk
                // addons once
                if (allowJoinedPlayersWaiting())
                    m_registered_for_once_only = true;
                m_state = WAITING_FOR_START_GAME;
                updatePlayerList();
            }
        }
        break;
    }
    case WAITING_FOR_START_GAME:
    {
        if (ServerConfig::m_owner_less)
        {
            // Ensure that a game can auto-start if the server meets the config's starting limit or if it's already full.
            int starting_limit = std::min((int)ServerConfig::m_min_start_game_players, (int)ServerConfig::m_server_max_players);
            if (ServerConfig::m_max_players_in_game > 0) // 0 here means it's not the limit
                starting_limit = std::min(starting_limit, (int)ServerConfig::m_max_players_in_game);

            unsigned players = 0;
            STKHost::get()->updatePlayers(&players);

            if (((int)players >= ServerConfig::m_min_start_game_players ||
                m_game_setup->isGrandPrixStarted()) &&
                m_timeout.load() == std::numeric_limits<int64_t>::max())
            {
                m_timeout.store((int64_t)StkTime::getMonoTimeMs() +
                    (int64_t)
                    (ServerConfig::m_start_game_counter * 1000.0f));
            }
            else if ((int)players < starting_limit &&
                !m_game_setup->isGrandPrixStarted())
            {
                resetPeersReady();
                if (m_timeout.load() != std::numeric_limits<int64_t>::max())
                    updatePlayerList();
                m_timeout.store(std::numeric_limits<int64_t>::max());
            }

	    const char all_ready_play = checkPeersCanPlayAndReady(true/*ignore_ai_peer*/);
            if (((m_timeout.load() < (int64_t)StkTime::getMonoTimeMs()) &&
	 	  (all_ready_play&2)) ||
                ((all_ready_play==3) &&
                (int)players >= starting_limit))
            {
                resetPeersReady();
                startSelection();
                return;
            }
        }
        // You can implement anything of interesting when there is a crowned player
        break;
    }
    case ERROR_LEAVE:
    {
        requestTerminate();
        m_state = EXITING;
        STKHost::get()->requestShutdown();
        break;
    }
    case WAIT_FOR_WORLD_LOADED:
    {
        // For WAIT_FOR_WORLD_LOADED and SELECTING make sure there are enough
        // players to start next game, otherwise exiting and let main thread
        // reset
        if (m_end_voting_period.load() == 0)
            return;

        unsigned player_in_game = 0;
        STKHost::get()->updatePlayers(&player_in_game);
        // Reset lobby will be done in main thread
        if ((player_in_game == 1 && ServerConfig::m_ranked) ||
            player_in_game == 0)
        {
            resetVotingTime();
            return;
        }

        // m_server_has_loaded_world is set by main thread with atomic write
        if (m_server_has_loaded_world.load() == false)
            return;
        if (!checkPeersReady(
            ServerConfig::m_ai_handling && m_ai_count == 0/*ignore_ai_peer*/))
            return;
        // Reset for next state usage
        resetPeersReady();
        configPeersStartTime();
        break;
    }
    case SELECTING:
    {
        if (m_end_voting_period.load() == 0)
            return;
        unsigned player_in_game = 0;
        STKHost::get()->updatePlayers(&player_in_game);
        if ((player_in_game == 1 && ServerConfig::m_ranked) ||
            player_in_game == 0)
        {
            resetVotingTime();
            return;
        }

        PeerVote winner_vote;
        m_winner_peer_id = std::numeric_limits<uint32_t>::max();
        bool go_on_race = false;
        bool track_voting = ServerConfig::m_track_voting;

        if (track_voting && !m_set_field.empty())
            track_voting = false;

        if (track_voting)
            go_on_race = handleAllVotes(&winner_vote, &m_winner_peer_id);

        else if (m_game_setup->isGrandPrixStarted() || isVotingOver())
        {
            winner_vote = *m_default_vote;
            go_on_race = true;
        }
        if (go_on_race)
        {
            // pole
            if (isPoleEnabled() && (!m_red_pole_votes.empty() || !m_blue_pole_votes.empty()))
            {
                auto pole = decidePoles();

                if (pole.first && pole.first->getTeam() == KART_TEAM_BLUE)
                {
                    STKHost::get()->setForcedSecondPlayer(pole.first);
                    announcePoleFor(pole.first, KART_TEAM_BLUE);
                    Log::info("ServerLobby", "Pole player for team blue is %s.",
                            StringUtils::wideToUtf8(pole.first->getName()).c_str());
                }
                else
                    Log::info("ServerLobby", "No pole player for first pos.");

                if (pole.second && pole.second->getTeam() == KART_TEAM_RED)
                {
                    STKHost::get()->setForcedFirstPlayer(pole.second);
                    announcePoleFor(pole.second, KART_TEAM_RED);
                    Log::info("ServerLobby", "Pole player for team red is %s.",
                            StringUtils::wideToUtf8(pole.second->getName()).c_str());
                }
                else
                    Log::info("ServerLobby", "No pole player for second pos.");
            }
            *m_default_vote = winner_vote;
            m_item_seed = (uint32_t)StkTime::getTimeSinceEpoch();
            ItemManager::updateRandomSeed(m_item_seed);
            m_game_setup->setRace(winner_vote);

            // For spectators that don't have the track, remember their
            // spectate mode and don't load the track
            std::string track_name = winner_vote.m_track_name;
            auto peers = STKHost::get()->getPeers();
            std::map<std::shared_ptr<STKPeer>,
                    AlwaysSpectateMode> previous_spectate_mode;
            for (auto peer : peers)
            {
                if (peer->alwaysSpectate() &&
                    peer->getClientAssets().second.count(track_name) == 0)
                {
                    previous_spectate_mode[peer] = peer->getAlwaysSpectate();
                    peer->setAlwaysSpectate(ASM_NONE);
                    peer->setWaitingForGame(true);
                    m_peers_ready.erase(peer);
                }
            }
            bool has_always_on_spectators = false;
            auto players = STKHost::get()
                ->getPlayersForNewGame(&has_always_on_spectators);
            for (auto& p: previous_spectate_mode)
                if (p.first)
                    p.first->setAlwaysSpectate(p.second);
            auto ai_instance = m_ai_peer.lock();
            if (supportsAI())
            {
                if (ai_instance)
                {
                    auto ai_profiles = ai_instance->getPlayerProfiles();
                    if (m_ai_count > 0)
                    {
                        ai_profiles.resize(m_ai_count);
                        players.insert(players.end(), ai_profiles.begin(),
                            ai_profiles.end());
                    }
                }
                else if (!m_ai_profiles.empty())
                {
                    players.insert(players.end(), m_ai_profiles.begin(),
                        m_ai_profiles.end());
                }
            }
            m_game_setup->sortPlayersForGrandPrix(players);
            m_game_setup->sortPlayersForGame(players, 0/*ignoreLeading*/, !isPoleEnabled()/*shuffle*/);

            if (players.size() > 0)
            {
                auto player1 = players[0];
            }
            if (players.size() > 1)
            {
                auto player2 = players[1];
            }

            // Add placeholder players for live join
            addLiveJoinPlaceholder(players);

            for (unsigned i = 0; i < players.size(); i++)
            {
                std::shared_ptr<NetworkPlayerProfile>& player = players[i];
                std::shared_ptr<STKPeer> peer = player->getPeer();
                if (peer)
                    peer->clearAvailableKartIDs();
            }
            for (unsigned i = 0; i < players.size(); i++)
            {
                std::shared_ptr<NetworkPlayerProfile>& player = players[i];
                std::shared_ptr<STKPeer> peer = player->getPeer();
                if (peer)
                    peer->addAvailableKartID(i);
            }
            getHitCaptureLimit();

            // If player chose random / hasn't chose any kart
            for (unsigned i = 0; i < players.size(); i++)
            {
                if (!players[i]->getForcedKart().empty())
                {
                    players[i]->setKartName(players[i]->getForcedKart());
                }
                else if (players[i]->getKartName().empty())
                {
                    RandomGenerator rg;
                    std::set<std::string>::iterator it =
                        m_available_kts.first.begin();
                    std::advance(it,
                        rg.get((int)m_available_kts.first.size()));
                    players[i]->setKartName(*it);
                }
                // log the current players
                if (players[i]->getTeam() != KART_TEAM_NONE)
                    Log::verbose("ServerLobby", "%s joined the %s team at 0.0",
                            StringUtils::wideToUtf8(players[i]->getName()).c_str(),
                            players[i]->getTeam() == KART_TEAM_BLUE ? "blue" : "red");
            }

            if (players.size() > 0)
            {
                auto player1 = players[0];
            }
            if (players.size() > 1)
            {
                auto player2 = players[1];
            }

            NetworkString* load_world_message = getLoadWorldMessage(players,
                false/*live_join*/);
            m_game_setup->setHitCaptureTime(m_battle_hit_capture_limit,
                m_battle_time_limit);
            uint16_t flag_return_time = (uint16_t)stk_config->time2Ticks(
                ServerConfig::m_flag_return_timeout);
            RaceManager::get()->setFlagReturnTicks(flag_return_time);
            uint16_t flag_deactivated_time = (uint16_t)stk_config->time2Ticks(
                ServerConfig::m_flag_deactivated_time);
            RaceManager::get()->setFlagDeactivatedTicks(flag_deactivated_time);
            configRemoteKart(players, 0);
           
            std::string log_msg;
            if (ServerConfig::m_soccer_log || ServerConfig::m_race_log)
            {
                if ((RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_NORMAL_RACE) ||
                    (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_TIME_TRIAL) ||
                    (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_LAP_TRIAL))
                {
                    log_msg = "Addon: ";
                    log_msg += std::to_string(winner_vote.m_reverse) + " ";
                    log_msg += winner_vote.m_track_name + " "; 
                    log_msg += std::to_string(winner_vote.m_num_laps);
                }
                else
                    log_msg = "Addon: " + winner_vote.m_track_name;
                GlobalLog::writeLog(log_msg + "\n", GlobalLogTypes::POS_LOG);
                Log::info("AddonLog", log_msg.c_str());
            }


            // Reset for next state usage
            resetPeersReady();
            m_state = LOAD_WORLD;

            // Only those that have the addon will receive the message
            STKHost::get()->sendPacketToAllPeersWith(
                    [&](STKPeer* peer)
                    {
                        const auto tracks = peer->getClientAssets().second;
                        return !peer->isWaitingForGame() &&
                            peer->getPermissionLevel() >= PERM_SPECTATOR &&
                            peer->notRestrictedBy(PRF_NOSPEC) &&
                            tracks.find(winner_vote.m_track_name) != tracks.cend();
                    }, load_world_message);
            // updatePlayerList so the in lobby players (if any) can see always
            // spectators join the game
            if (has_always_on_spectators)
                updatePlayerList();
            delete load_world_message;
        }
        break;
    }
    default:
        break;
    }

}   // asynchronousUpdate

//-----------------------------------------------------------------------------
void ServerLobby::encodePlayers(BareNetworkString* bns,
        std::vector<std::shared_ptr<NetworkPlayerProfile> >& players) const
{
    bns->addUInt8((uint8_t)players.size());
    for (unsigned i = 0; i < players.size(); i++)
    {
        std::shared_ptr<NetworkPlayerProfile>& player = players[i];
        bns->encodeString(player->getName())
            .addUInt32(player->getHostId())
            .addFloat(player->getDefaultKartColor())
            .addUInt32(player->getOnlineId())
            .addUInt8(player->getHandicap())
            .addUInt8(player->getLocalPlayerId())
            .addUInt8(
            RaceManager::get()->teamEnabled() ? player->getTeam() : KART_TEAM_NONE)
            .encodeString(player->getCountryCode());
        bns->encodeString(player->getKartName());
    }
}   // encodePlayers

//-----------------------------------------------------------------------------
NetworkString* ServerLobby::getLoadWorldMessage(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
    bool live_join) const
{
    NetworkString* load_world_message = getNetworkString();
    load_world_message->setSynchronous(true);
    load_world_message->addUInt8(LE_LOAD_WORLD);
    load_world_message->addUInt32(m_winner_peer_id);
    m_default_vote->encode(load_world_message);
    load_world_message->addUInt8(live_join ? 1 : 0);
    encodePlayers(load_world_message, players);
    load_world_message->addUInt32(m_item_seed);
    if (RaceManager::get()->isBattleMode())
    {
        if (RaceManager::get()->isInfiniteMode())
            load_world_message->addUInt32(std::numeric_limits<std::uint32_t>::max())
                .addFloat(std::numeric_limits<float>::infinity());
        else
            load_world_message->addUInt32(m_battle_hit_capture_limit)
                .addFloat(m_battle_time_limit);
        uint16_t flag_return_time = (uint16_t)stk_config->time2Ticks(
            ServerConfig::m_flag_return_timeout);
        load_world_message->addUInt16(flag_return_time);
        uint16_t flag_deactivated_time = (uint16_t)stk_config->time2Ticks(
            ServerConfig::m_flag_deactivated_time);
        load_world_message->addUInt16(flag_deactivated_time);
    }
    for (unsigned i = 0; i < players.size(); i++)
        players[i]->getKartData().encode(load_world_message);
    return load_world_message;
}   // getLoadWorldMessage

//-----------------------------------------------------------------------------
/** Returns true if server can be live joined or spectating
 */
bool ServerLobby::canLiveJoinNow() const
{
    bool live_join = ServerConfig::m_live_players && worldIsActive();
    if (!live_join)
        return false;
    if (RaceManager::get()->modeHasLaps())
    {
        // No spectate when fastest kart is nearly finish, because if there
        // is endcontroller the spectating remote may not be knowing this
        // on time
        LinearWorld* w = dynamic_cast<LinearWorld*>(World::getWorld());
        if (!w)
            return false;
        AbstractKart* fastest_kart = NULL;
        for (unsigned i = 0; i < w->getNumKarts(); i++)
        {
            fastest_kart = w->getKartAtPosition(i + 1);
            if (fastest_kart && !fastest_kart->isEliminated())
                break;
        }
        if (!fastest_kart)
            return false;
        float progress = w->getOverallDistance(
            fastest_kart->getWorldKartId()) /
            (Track::getCurrentTrack()->getTrackLength() *
            (float)RaceManager::get()->getNumLaps());
        if (progress > 0.9f)
            return false;
    }
    return live_join;
}   // canLiveJoinNow

//-----------------------------------------------------------------------------
/** Returns true if world is active for clients to live join, spectate or
 *  going back to lobby live
 */
bool ServerLobby::worldIsActive() const
{
    return World::getWorld() && RaceEventManager::get()->isRunning() &&
        !RaceEventManager::get()->isRaceOver() &&
        World::getWorld()->getPhase() == WorldStatus::RACE_PHASE;
}   // worldIsActive

//-----------------------------------------------------------------------------
/** \ref STKPeer peer will be reset back to the lobby with reason
 *  \ref BackLobbyReason blr
 */
void ServerLobby::rejectLiveJoin(STKPeer* peer, BackLobbyReason blr)
{
    NetworkString* reset = getNetworkString(2);
    reset->setSynchronous(true);
    reset->addUInt8(LE_BACK_LOBBY).addUInt8(blr);
    peer->sendPacket(reset, /*reliable*/true);
    delete reset;
    updatePlayerList();
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    peer->sendPacket(server_info, /*reliable*/true);
    delete server_info;

    // everywhere where addServerInfo is used, sendRandomInstalladdonLine be used too.
    sendRandomInstalladdonLine(peer);
    sendCurrentModifiers(peer);
}   // rejectLiveJoin

//-----------------------------------------------------------------------------
/** This message is like kartSelectionRequested, but it will send the peer
 *  load world message if he can join the current started game.
 */
void ServerLobby::liveJoinRequest(Event* event)
{
    STKPeer* peer = event->getPeer();
    const NetworkString& data = event->data();

    if (!canLiveJoinNow())
    {
        rejectLiveJoin(peer, BLR_NO_GAME_FOR_LIVE_JOIN);
        return;
    }
    bool spectator = data.getUInt8() == 1;
    if (RaceManager::get()->modeHasLaps() && !spectator)
    {
        // No live join for linear race
        rejectLiveJoin(peer, BLR_NO_GAME_FOR_LIVE_JOIN);
        return;
    }

    if (spectator && (peer->hasRestriction(PRF_NOSPEC) ||
                (peer->getPermissionLevel() < PERM_SPECTATOR)))
    {
        rejectLiveJoin(peer, BLR_NO_PLACE_FOR_LIVE_JOIN);
        sendStringToPeer(L"You are not allowed to spectate.", peer);
        return;
    }
    else if (!spectator)
    {
        const PeerEligibility old_eligibility = peer->getEligibility();
        const PeerEligibility new_eligibility = peer->testEligibility();
        LobbyPlayerQueue::get()->onPeerEligibilityChange(event->getPeerSP(), old_eligibility);
        // test eligibility for not spectating
        switch (new_eligibility)
        {
            case PELG_YES:
                break;
            case PELG_NO_FORCED_TRACK:
                break;
            case PELG_SPECTATOR:
                // alwaysSpectate is set to ASM_COMMAND or ASM_FULL, but the player still wants to live join
                break;
            case PELG_PRESET_KART_REQUIRED:
            {
                rejectLiveJoin(peer, BLR_NO_PLACE_FOR_LIVE_JOIN);
                sendStringToPeer(L"You need to set your kart with /setkart [name].", peer);
                return;
            }
            default:
            {
                rejectLiveJoin(peer, BLR_NO_PLACE_FOR_LIVE_JOIN);
                sendStringToPeer(L"You are not allowed to participate in the game.", peer);
                return;
            }
        }
    }


    peer->clearAvailableKartIDs();
    if (!spectator)
    {
        setPlayerKarts(data, peer);

        std::vector<int> used_id;
        for (unsigned i = 0; i < peer->getPlayerProfiles().size(); i++)
        {
            int id = getReservedId(peer->getPlayerProfiles()[i], i);
            if (id == -1)
                break;
            used_id.push_back(id);
        }
        if ((used_id.size() != peer->getPlayerProfiles().size()) ||
            LobbyPlayerQueue::get()->isSpectatorByLimit(peer))
        {
            for (unsigned i = 0; i < peer->getPlayerProfiles().size(); i++)
                peer->getPlayerProfiles()[i]->setKartName("");
            for (unsigned i = 0; i < used_id.size(); i++)
            {
                RemoteKartInfo& rki = RaceManager::get()->getKartInfo(used_id[i]);
                rki.makeReserved();
            }
            Log::info("ServerLobby", "Too many players (%d) try to live join",
                (int)peer->getPlayerProfiles().size());
            rejectLiveJoin(peer, BLR_NO_PLACE_FOR_LIVE_JOIN);
            return;
        }

        for (int id : used_id)
        {
            Log::info("ServerLobby", "%s live joining with reserved kart id %d.",
                peer->getAddress().toString().c_str(), id);
            peer->addAvailableKartID(id);
        }
    }
    else
    {
        Log::info("ServerLobby", "%s spectating now.",
            peer->getAddress().toString().c_str());
    }

    std::vector<std::shared_ptr<NetworkPlayerProfile> > players =
        getLivePlayers();
    NetworkString* load_world_message = getLoadWorldMessage(players,
        true/*live_join*/);
    peer->sendPacket(load_world_message, true/*reliable*/);
    delete load_world_message;
    peer->updateLastActivity();
}   // liveJoinRequest

//-----------------------------------------------------------------------------
/** Get a list of current ingame players for live join or spectate.
 */
std::vector<std::shared_ptr<NetworkPlayerProfile> >
                                            ServerLobby::getLivePlayers() const
{
    std::vector<std::shared_ptr<NetworkPlayerProfile> > players;
    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
        std::shared_ptr<NetworkPlayerProfile> player =
            rki.getNetworkPlayerProfile().lock();
        if (!player)
        {
            if (RaceManager::get()->modeHasLaps())
            {
                player = std::make_shared<NetworkPlayerProfile>(
                    nullptr, rki.getPlayerName(),
                    std::numeric_limits<uint32_t>::max(),
                    rki.getDefaultKartColor(),
                    rki.getOnlineId(), rki.getHandicap(),
                    rki.getLocalPlayerId(), KART_TEAM_NONE,
                    rki.getCountryCode());
                player->setKartName(rki.getKartName());
            }
            else
            {
                player = NetworkPlayerProfile::getReservedProfile(
                    RaceManager::get()->getMinorMode() ==
                    RaceManager::MINOR_MODE_FREE_FOR_ALL ?
                    KART_TEAM_NONE : rki.getKartTeam());
            }
        }
        players.push_back(player);
    }
    return players;
}   // getLivePlayers

//-----------------------------------------------------------------------------
/** Decide where to put the live join player depends on his team and game mode.
 */
int ServerLobby::getReservedId(std::shared_ptr<NetworkPlayerProfile>& p,
                               unsigned local_id) const
{
    const bool is_ffa =
        RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL;
    int red_count = 0;
    int blue_count = 0;
    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
        if (rki.isReserved())
            continue;
        bool disconnected = rki.disconnected();
        if (RaceManager::get()->getKartInfo(i).getKartTeam() == KART_TEAM_RED &&
            !disconnected)
            red_count++;
        else if (RaceManager::get()->getKartInfo(i).getKartTeam() ==
            KART_TEAM_BLUE && !disconnected)
            blue_count++;
    }
    KartTeam target_team = red_count > blue_count ? KART_TEAM_BLUE :
        KART_TEAM_RED;

    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
        std::shared_ptr<NetworkPlayerProfile> player =
            rki.getNetworkPlayerProfile().lock();
        if (!player)
        {
            if (is_ffa)
            {
                rki.copyFrom(p, local_id);
                return i;
            }
            if (ServerConfig::m_team_choosing)
            {
                if ((p->getTeam() == KART_TEAM_RED &&
                    rki.getKartTeam() == KART_TEAM_RED) ||
                    (p->getTeam() == KART_TEAM_BLUE &&
                    rki.getKartTeam() == KART_TEAM_BLUE))
                {
                    rki.copyFrom(p, local_id);
                    return i;
                }
            }
            else
            {
                if (rki.getKartTeam() == target_team)
                {
                    p->setTeam(target_team);
                    rki.copyFrom(p, local_id);
                    return i;
                }
            }
        }
    }
    return -1;
}   // getReservedId

//-----------------------------------------------------------------------------
/** Finally put the kart in the world and inform client the current world
 *  status, (including current confirmed item state, kart scores...)
 */
void ServerLobby::finishedLoadingLiveJoinClient(Event* event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    if (!canLiveJoinNow())
    {
        rejectLiveJoin(peer.get(), BLR_NO_GAME_FOR_LIVE_JOIN);
        return;
    }
    bool live_joined_in_time = true;
    for (const int id : peer->getAvailableKartIDs())
    {
        const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(id);
        if (rki.isReserved())
        {
            live_joined_in_time = false;
            break;
        }
    }
    if (!live_joined_in_time)
    {
        Log::warn("ServerLobby", "%s can't live-join in time.",
            peer->getAddress().toString().c_str());
        rejectLiveJoin(peer.get(), BLR_NO_GAME_FOR_LIVE_JOIN);
        return;
    }
    World* w = World::getWorld();
    assert(w);

    uint64_t live_join_start_time = STKHost::get()->getNetworkTimer();

    // Instead of using getTicksSinceStart we caculate the current world ticks
    // only from network timer, because if the server hangs in between the
    // world ticks may not be up to date
    // 2000 is the time for ready set, remove 3 ticks after for minor
    // correction (make it more looks like getTicksSinceStart if server has no
    // hang
    int cur_world_ticks = stk_config->time2Ticks(
        (live_join_start_time - m_server_started_at - 2000) / 1000.f) - 3;
    // Give 3 seconds for all peers to get new kart info
    m_last_live_join_util_ticks =
        cur_world_ticks + stk_config->time2Ticks(3.0f);
    live_join_start_time -= m_server_delay;
    live_join_start_time += 3000;

    bool spectator = false;
    std::string msg;
    for (const int id : peer->getAvailableKartIDs())
    {
        World::getWorld()->addReservedKart(id);
        const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(id);
        addLiveJoiningKart(id, rki, m_last_live_join_util_ticks);
        Log::info("ServerLobby", "%s succeeded live-joining with kart id %d.",
            peer->getAddress().toString().c_str(), id);
        if (ServerConfig::m_soccer_log || ServerConfig::m_race_log)
	{
            GlobalLog::addIngamePlayer(id, StringUtils::wideToUtf8(rki.getPlayerName()), rki.getOnlineId() == 0);

            World* w = World::getWorld();
            if (w)
	    {
	        std::string time = std::to_string(w->getTime());
            auto kart_team = w->getKartTeam(id);
            std::string team =  kart_team==KART_TEAM_RED ? "red" : "blue";
            msg =  StringUtils::wideToUtf8(rki.getPlayerName()) + " joined the " + team + " team at "+ time + "\n";
            GlobalLog::writeLog(msg, GlobalLogTypes::POS_LOG);
            Log::verbose("ServerLobby", "%s", msg.c_str());
	    }
	}
    }
    if (peer->getAvailableKartIDs().empty())
    {
        Log::info("ServerLobby", "%s spectating succeeded.",
            peer->getAddress().toString().c_str());
        spectator = true;
    }

    const uint8_t cc = (uint8_t)Track::getCurrentTrack()->getCheckManager()->getCheckStructureCount();
    NetworkString* ns = getNetworkString(10);
    ns->setSynchronous(true);
    ns->addUInt8(LE_LIVE_JOIN_ACK).addUInt64(m_client_starting_time)
        .addUInt8(cc).addUInt64(live_join_start_time)
        .addUInt32(m_last_live_join_util_ticks);

    NetworkItemManager* nim = dynamic_cast<NetworkItemManager*>
        (Track::getCurrentTrack()->getItemManager());
    assert(nim);
    nim->saveCompleteState(ns);
    nim->addLiveJoinPeer(peer);

    w->saveCompleteState(ns, peer.get());
    if (RaceManager::get()->supportsLiveJoining())
    {
        // This isn't required for modes that don't support live joining, since
        // there won't be any new players in the world after the race has started.
        std::vector<std::shared_ptr<NetworkPlayerProfile> > players =
            getLivePlayers();
        encodePlayers(ns, players);
        for (unsigned i = 0; i < players.size(); i++)
            players[i]->getKartData().encode(ns);
    }

    m_peers_ready[peer] = false;
    peer->setWaitingForGame(false);
    peer->setSpectator(spectator);

    peer->sendPacket(ns, true/*reliable*/);
    delete ns;
    updatePlayerList();
    peer->updateLastActivity();
}   // finishedLoadingLiveJoinClient

//-----------------------------------------------------------------------------
/** Simple finite state machine.  Once this
 *  is known, register the server and its address with the stk server so that
 *  client can find it.
 */
void ServerLobby::update(int ticks)
{	
    World* w = World::getWorld();
    bool world_started = m_state.load() >= WAIT_FOR_WORLD_LOADED &&
        m_state.load() <= RACING && m_server_has_loaded_world.load();
    bool all_players_in_world_disconnected = (w != NULL && world_started);
    int sec = ServerConfig::m_kick_idle_player_seconds;
    if (m_state.load() == WAITING_FOR_START_GAME)
    {
	    checkTeamSelectionVoteTimeout();
	    checkRPSTimeouts();
    }
    if (world_started)
    {
        for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
        {
            RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
            std::shared_ptr<NetworkPlayerProfile> player =
                rki.getNetworkPlayerProfile().lock();
            if (player)
            {
                if (w)
                    all_players_in_world_disconnected = false;
            }
            else
                continue;
            auto peer = player->getPeer();
            if (!peer)
                continue;

            if (peer->idleForSeconds() > 60 && w &&
                w->getKart(i)->isEliminated())
            {
                // Remove loading world too long (60 seconds) live join peer
                Log::info("ServerLobby", "%s hasn't live-joined within"
                    " 60 seconds, remove it.",
                    peer->getAddress().toString().c_str());
                rki.makeReserved();
                continue;
            }
            if (!peer->isAIPeer() &&
                sec > 0 && peer->idleForSeconds() > sec &&
                !peer->isDisconnected() && NetworkConfig::get()->isWAN())
            {
                if (w && w->getKart(i)->hasFinishedRace())
                    continue;
                // Don't kick in game GUI server host so he can idle in game
                if (m_process_type == PT_CHILD &&
                    peer->getHostId() == m_client_server_host_id.load())
                    continue;
                Log::info("ServerLobby", "%s %s has been idle for more than"
                    " %d seconds, kick.",
                    peer->getAddress().toString().c_str(),
                    StringUtils::wideToUtf8(rki.getPlayerName()).c_str(), sec);
                peer->kick();
            }
        }
    }
    if (w)
        setGameStartedProgress(w->getGameStartedProgress());
    else
        resetGameStartedProgress();

    if (w && w->getPhase() == World::RACE_PHASE)
    {
        storePlayingTrack(RaceManager::get()->getTrackName());
    }
    else
        storePlayingTrack("");

    // Reset server to initial state if no more connected players
    if (m_rs_state.load() == RS_WAITING)
    {
        if ((RaceEventManager::get() &&
            !RaceEventManager::get()->protocolStopped()) ||
            !GameProtocol::emptyInstance())
            return;

        exitGameState();
        m_rs_state.store(RS_ASYNC_RESET);
    }

    STKHost::get()->updatePlayers();
    if (m_rs_state.load() == RS_NONE &&
        (m_state.load() > WAITING_FOR_START_GAME ||
        m_game_setup->isGrandPrixStarted()) &&
        (STKHost::get()->getPlayersInGame() == 0 ||
        all_players_in_world_disconnected))
    {
        if (RaceEventManager::get() &&
            RaceEventManager::get()->isRunning())
        {
            // Send a notification to all players who may have start live join
            // or spectate to go back to lobby
            NetworkString* back_to_lobby = getNetworkString(2);
            back_to_lobby->setSynchronous(true);
            back_to_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
            sendMessageToPeersInServer(back_to_lobby, /*reliable*/true);
            delete back_to_lobby;

            RaceEventManager::get()->stop();
            RaceEventManager::get()->getProtocol()->requestTerminate();
            GameProtocol::lock()->requestTerminate();
        }
        else if (auto ai = m_ai_peer.lock())
        {
            // Reset AI peer for empty server, which will delete world
            NetworkString* back_to_lobby = getNetworkString(2);
            back_to_lobby->setSynchronous(true);
            back_to_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
            ai->sendPacket(back_to_lobby, /*reliable*/true);
            delete back_to_lobby;
        }

        resetVotingTime();
        m_game_setup->stopGrandPrix();
        m_rs_state.store(RS_WAITING);
        return;
    }

    if (m_rs_state.load() != RS_NONE)
        return;

    // Reset for ranked server if in kart / track selection has only 1 player
    if (ServerConfig::m_ranked &&
        m_state.load() == SELECTING &&
        STKHost::get()->getPlayersInGame() == 1)
    {
        NetworkString* back_lobby = getNetworkString(2);
        back_lobby->setSynchronous(true);
        back_lobby->addUInt8(LE_BACK_LOBBY)
            .addUInt8(BLR_ONE_PLAYER_IN_RANKED_MATCH);
        sendMessageToPeers(back_lobby, /*reliable*/true);
        delete back_lobby;
        resetVotingTime();
        m_game_setup->stopGrandPrix();
        m_rs_state.store(RS_ASYNC_RESET);
    }

    handlePlayerDisconnection();

    std::string time;
    std::string time_msg;

    switch (m_state.load())
    {
    case SET_PUBLIC_ADDRESS:
    case REGISTER_SELF_ADDRESS:
    case WAITING_FOR_START_GAME:
    case WAIT_FOR_WORLD_LOADED:
    case WAIT_FOR_RACE_STARTED:
    {
        // Waiting for asynchronousUpdate
        break;
    }
    case SELECTING:
        // The function playerTrackVote will trigger the next state
        // once all track votes have been received.
        break;
    case LOAD_WORLD:
        Log::info("ServerLobbyRoom", "Starting the race loading.");
        // This will create the world instance, i.e. load track and karts
        loadWorld();
        m_state = WAIT_FOR_WORLD_LOADED;
        break;
    case RACING:
        if (World::getWorld() && RaceEventManager::get() &&
            RaceEventManager::get()->isRunning())	
            checkRaceFinished();
        break;
    case WAIT_FOR_RACE_STOPPED:
        if (!RaceEventManager::get()->protocolStopped() ||
            !GameProtocol::emptyInstance())
            return;

        if (ServerConfig::m_soccer_log || ServerConfig::m_race_log)
	{
        World* w = World::getWorld();
            
            if (w)
	    {
	        time = std::to_string(w->getTime());
            }
	    time_msg = "The game ended after " + time + " seconds.\n";
            GlobalLog::writeLog(time_msg, GlobalLogTypes::POS_LOG);
	}
        if ((m_replay_requested || RaceManager::get()->isRecordingRace())
                && World::getWorld() && World::getWorld()->isRacePhase())	
        {
           m_replay_dir = ServerConfig::m_replay_dir;

           ReplayRecorder::get()->save();
           std::string replay_path = file_manager->getReplayDir() + ReplayRecorder::get()->getReplayFilename();
           if (file_manager->fileExists(replay_path))
           {
               Log::info("ServerLobby", "Replay file verified at: %s", replay_path.c_str());
               irr::core::stringw msg = "The replay has been successfully recorded and properly saved!";
               broadcastMessageInGame(msg);
           }
           else
           {
               Log::error("ServerLobby", "Replay file not found at: %s", replay_path.c_str());
	       Log::error("ServerLobby", "Failed to save replay"); 
           }
           // This is no longer required since the replay recording can be turned off with the command /replay off
           // m_replay_requested = false;
           // RaceManager::get()->setRecordRace(false);
           ReplayRecorder::get()->reset();
        }
        // This will go back to lobby in server (and exit the current race)
        exitGameState();
        // Reset for next state usage
        resetPeersReady();
        // Set the delay before the server forces all clients to exit the race
        // result screen and go back to the lobby
        m_timeout.store((int64_t)StkTime::getMonoTimeMs() + 15000);
        m_state = RESULT_DISPLAY;
        sendMessageToPeers(m_result_ns, /*reliable*/ true);
        Log::info("ServerLobby", "End of game message sent");

        if (ServerConfig::m_soccer_log || ServerConfig::m_race_log)
	{
		GlobalLog::writeLog("GAME_END\n", GlobalLogTypes::POS_LOG);
		GlobalLog::closeLog(GlobalLogTypes::POS_LOG);
		// Execute a python script
		if (ServerConfig::m_race_log)
		{

			std::thread python_thread([&]()
					{
						std::string command = std::string("python3 ") + ServerConfig::m_update_script_path.c_str();
						FILE* pipe = popen(command.c_str(), "r");
						if (!pipe)
						{	
							Log::info("ServerLobby", "Failed to start python script");
							return;
						}
						char buffer[4096];
						while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
						{
							size_t len = strlen(buffer);
							if (len > 0 && buffer[len-1] == '\n')
							{
								buffer[len-1] = '\0';
							}
							Log::info("ServerLobby", "Update script: %s", buffer);
						}
						pclose(pipe);
					});
					python_thread.detach();
		}
	}
	break;
    case RESULT_DISPLAY:
        if (checkPeersReady(true/*ignore_ai_peer*/) ||
            (int64_t)StkTime::getMonoTimeMs() > m_timeout.load())
        {
            // Send a notification to all clients to exit
            // the race result screen
            NetworkString* back_to_lobby = getNetworkString(2);
            back_to_lobby->setSynchronous(true);
            back_to_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
            sendMessageToPeersInServer(back_to_lobby, /*reliable*/true);
            delete back_to_lobby;
            m_rs_state.store(RS_ASYNC_RESET);
        }
        break;
    case ERROR_LEAVE:
    case EXITING:
        break;
    }
}   // update

//-----------------------------------------------------------------------------
/** Register this server (i.e. its public address) with the STK server
 *  so that clients can find it. It blocks till a response from the
 *  stk server is received (this function is executed from the 
 *  ProtocolManager thread). The information about this client is added
 *  to the table 'server'.
 */
void ServerLobby::registerServer(bool first_time)
{
    // ========================================================================
    class RegisterServerRequest : public Online::XMLRequest
    {
    private:
        std::weak_ptr<ServerLobby> m_server_lobby;
        bool m_first_time;
    protected:
        virtual void afterOperation()
        {
            Online::XMLRequest::afterOperation();
            const XMLNode* result = getXMLData();
            std::string rec_success;
            auto sl = m_server_lobby.lock();
            if (!sl)
                return;

            if (result->get("success", &rec_success) &&
                rec_success == "yes")
            {
                const XMLNode* server = result->getNode("server");
                assert(server);
                const XMLNode* server_info = server->getNode("server-info");
                assert(server_info);
                unsigned server_id_online = 0;
                server_info->get("id", &server_id_online);
                assert(server_id_online != 0);
                bool is_official = false;
                server_info->get("official", &is_official);
                if (!is_official && ServerConfig::m_ranked)
                {
                    Log::fatal("ServerLobby", "You don't have permission to "
                        "host a ranked server.");
                }
                Log::info("ServerLobby",
                    "Server %d is now online.", server_id_online);
                sl->m_server_id_online.store(server_id_online);
                sl->m_last_success_poll_time.store(StkTime::getMonoTimeMs());
                return;
            }
            Log::error("ServerLobby", "%s",
                StringUtils::wideToUtf8(getInfo()).c_str());
            // Exit now if failed to register to stk addons for first time
            if (m_first_time)
                sl->m_state.store(ERROR_LEAVE);
        }
    public:
        RegisterServerRequest(std::shared_ptr<ServerLobby> sl, bool first_time)
        : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY),
        m_server_lobby(sl), m_first_time(first_time) {}
    };   // RegisterServerRequest

    auto request = std::make_shared<RegisterServerRequest>(
        std::dynamic_pointer_cast<ServerLobby>(shared_from_this()), first_time);
    NetworkConfig::get()->setServerDetails(request, "create");
    const SocketAddress& addr = STKHost::get()->getPublicAddress();
    request->addParameter("address",      addr.getIP()        );
    request->addParameter("port",         addr.getPort()      );
    request->addParameter("private_port",
                                    STKHost::get()->getPrivatePort()      );
    request->addParameter("name", m_game_setup->getServerNameUtf8());
    request->addParameter("max_players", ServerConfig::m_server_max_players);
    int difficulty = m_difficulty.load();
    request->addParameter("difficulty", difficulty);
    int game_mode = m_game_mode.load();
    request->addParameter("game_mode", game_mode);
    const std::string& pw = ServerConfig::m_private_server_password;
    request->addParameter("password", (unsigned)(!pw.empty()));
    request->addParameter("version", (unsigned)ServerConfig::m_server_version);

    bool ipv6_only = addr.isUnset();
    if (!ipv6_only)
    {
        Log::info("ServerLobby", "Public IPv4 server address %s",
            addr.toString().c_str());
    }
    if (!STKHost::get()->getPublicIPv6Address().empty())
    {
        request->addParameter("address_ipv6",
            STKHost::get()->getPublicIPv6Address());
        Log::info("ServerLobby", "Public IPv6 server address %s",
            STKHost::get()->getPublicIPv6Address().c_str());
    }
    request->queue();
    m_server_registering = request;
}   // registerServer

//-----------------------------------------------------------------------------
/** Unregister this server (i.e. its public address) with the STK server,
 *  currently when karts enter kart selection screen it will be done or quit
 *  stk.
 */
void ServerLobby::unregisterServer(bool now, std::weak_ptr<ServerLobby> sl)
{
    // ========================================================================
    class UnRegisterServerRequest : public Online::XMLRequest
    {
    private:
        std::weak_ptr<ServerLobby> m_server_lobby;
    protected:
        virtual void afterOperation()
        {
            Online::XMLRequest::afterOperation();
            const XMLNode* result = getXMLData();
            std::string rec_success;

            if (result->get("success", &rec_success) &&
                rec_success == "yes")
            {
                // Clear the server online for next register
                // For grand prix server
                if (auto sl = m_server_lobby.lock())
                    sl->m_server_id_online.store(0);
                return;
            }
            Log::error("ServerLobby", "%s",
                StringUtils::wideToUtf8(getInfo()).c_str());
        }
    public:
        UnRegisterServerRequest(std::weak_ptr<ServerLobby> sl)
        : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY),
        m_server_lobby(sl) {}
    };   // UnRegisterServerRequest
    auto request = std::make_shared<UnRegisterServerRequest>(sl);
    NetworkConfig::get()->setServerDetails(request, "stop");

    const SocketAddress& addr = STKHost::get()->getPublicAddress();
    request->addParameter("address", addr.getIP());
    request->addParameter("port", addr.getPort());
    bool ipv6_only = addr.isUnset();
    if (!ipv6_only)
    {
        Log::info("ServerLobby", "Unregister server address %s",
            addr.toString().c_str());
    }
    else
    {
        Log::info("ServerLobby", "Unregister server address %s",
            STKHost::get()->getValidPublicAddress().c_str());
    }

    // No need to check for result as server will be auto-cleared anyway
    // when no polling is done
    if (now)
    {
        request->executeNow();
    }
    else
        request->queue();

}   // unregisterServer

void ServerLobby::insertKartsIntoNotType(std::set<std::string>& set, const char* type) const
{
    // m_available_kts.first is not an addon list
    for (const std::string& kt : m_available_kts.first ) {
        const KartProperties* const props = kart_properties_manager->getKart(kt);
        const std::string& _type = props->getKartType();
        if (_type != type)
        {
            set.insert(kt);
        }
    }
}
std::set<std::string> ServerLobby::getOtherKartsThan(const std::string& name) const
{
    std::set<std::string> set;
    for (const std::string& kt : m_available_kts.first ) {
        const KartProperties* const props = kart_properties_manager->getKart(kt);
        const core::stringw& _name = props->getName();
        if (_name != StringUtils::utf8ToWide(name))
        {
            set.insert(kt);
        }
    }
    return set;
}

const char* ServerLobby::kartRestrictedTypeName(const enum KartRestrictionMode mode) const
{
    switch (mode) {
        case LIGHT:
            return "light";
        case MEDIUM:
            return "medium";
        case HEAVY:
            return "heavy";
        case NONE:
            break;
    }
    return "";
}

void ServerLobby::setKartRestrictionMode(const enum KartRestrictionMode mode)
{
    m_kart_restriction = mode;
}
//-----------------------------------------------------------------------------
/** Instructs all clients to start the kart selection. If event is NULL,
 *  the command comes from the owner less server.
 */
void ServerLobby::startSelection(const Event *event)
{		
	if (event)
	{
        // ready button pressed
		std::shared_ptr<STKPeer> peer = event->getPeerSP();
		if (m_state != WAITING_FOR_START_GAME)
		{
			Log::warn("ServerLobby",
					"Received startSelection while being in state %d.",
					m_state.load());
			return;
		}

		// check if player can play
        const PeerEligibility old_eligibility = peer->getEligibility();
        const PeerEligibility new_eligibility = peer->testEligibility();
        LobbyPlayerQueue::get()->onPeerEligibilityChange(peer, old_eligibility);
        switch (new_eligibility)
        {
            case PELG_ACCESS_DENIED:
                sendStringToPeer(L"You are not allowed to play the game.", peer);
                return;
            // check if player has standard assets
            case PELG_NO_STANDARD_CONTENT:
                sendStringToPeer(L"You cannot ready up because you are missing required standard tracks.", peer);
                return;
            // when the field is forced, check if the player has the field
            case PELG_NO_FORCED_TRACK:
            {
                std::string msg = "You need to install ";
                msg += m_set_field;
                msg += " in order to play. Click the link below to install it:\n/installaddon";
                msg += m_set_field;
                sendStringToPeer(msg, peer);
                return;
            }
            case PELG_PRESET_KART_REQUIRED:
            {
                const std::string msg = "Use /setkart (kart_name) to play the game.";
                sendStringToPeer(msg, peer);
                return;
            }
            case PELG_PRESET_TRACK_REQUIRED:
            {
                const std::string msg = "Use /settrack (track_name) - (reverse? on/off) to play the game.";
                sendStringToPeer(msg, peer);
                return;
            }
            case PELG_SPECTATOR:
            {
                const std::string msg = "You are the spectator.";
                sendStringToPeer(msg, peer);
                return;
            }
            case PELG_OTHER:
            {
                const std::string msg = "You cannot play the game.";
                sendStringToPeer(msg, peer);
                return;
            }
            case PELG_YES:
                break;
        }
        const bool is_sbl = LobbyPlayerQueue::get()->isSpectatorByLimit(peer.get());
        // this should give the privilege to immediately start the game
        const bool not_singleslot = LobbyPlayerQueue::get()->getMaxPlayersInGame() != 1;
	
        if (is_sbl)
        {
            const std::string msg = "You need to wait for the free spot for playing the game.";
            sendStringToPeer(msg, peer);
            return;
        }

        if (not_singleslot && ServerConfig::m_owner_less)
        {
            // toggle ready
            m_peers_ready.at(event->getPeerSP()) =
                !m_peers_ready.at(event->getPeerSP());
            updatePlayerList();
            return;
        }
        if (not_singleslot && event->getPeerSP() != m_server_owner.lock())
        {
            Log::warn("ServerLobby",
                "Client %d is not authorised to start selection.",
                event->getPeer()->getHostId());
            return;
        }
    }
    // ...otherwise (argument == nullptr) the server decides to start the game

    // Find if there are peers playing the game
    auto peers = STKHost::get()->getPeers();
    std::set<STKPeer*> always_spectate_peers = LobbyPlayerQueue::get()->getSpectatorsByLimit();
    {
        // We make those always spectate peer waiting for game so it won't
        // be able to vote, this will be reset in STKHost::getPlayersForNewGame
        // This will also allow a correct number of in game players for max
        // arena players handling
        for (STKPeer* peer : always_spectate_peers)
        {
            peer->setAlwaysSpectate(ASM_FULL);
            peer->setWaitingForGame(true);
        }
    }

    unsigned max_player = 0;
    STKHost::get()->updatePlayers(&max_player);
    
    if (ServerConfig::m_soccer_log || ServerConfig::m_race_log)
    {
        GlobalLog::writeLog("GAME_START\n", GlobalLogTypes::POS_LOG);
        time_t now;
        time(&now);
        char buf[sizeof "2011-10-08T07:07:09Z"];
        strftime(buf, sizeof buf, "%FT%TZ", gmtime(&now));
        std::string buf2;
        for (int i=0;i< sizeof buf - 1 ;i++)
            buf2 += buf[i];
        std::string msg = "Match started at " + buf2 + "\n";
        GlobalLog::writeLog(msg, GlobalLogTypes::POS_LOG);
    }

    if (always_spectate_peers.size() == peers.size())
    {
        Log::error("ServerLobby", "Too many players and cannot set "
            "spectate for late coming players!");
        return;
    }

    // Remove karts / tracks from server that are not supported
    // on all clients that are playing
    std::set<std::string> karts_erase, tracks_erase;
    switch (m_kart_restriction) {
        case NONE:
            break;
        case LIGHT:
            insertKartsIntoNotType(karts_erase, "light");
            break;
        case MEDIUM:
            insertKartsIntoNotType(karts_erase, "medium");
            break;
        case HEAVY:
            insertKartsIntoNotType(karts_erase, "heavy");
            break;
    }

    for (auto peer : peers)
    {
        // update eligibility of all peers except the one who pressed the button
        if ((!event || peer != event->getPeerSP()) && !peer->isEligibleForGame())
        {
            peer->setWaitingForGame(true);
            if (peer->getPermissionLevel() >= PERM_SPECTATOR &&
                    peer->notRestrictedBy(PRF_NOSPEC))
                peer->setAlwaysSpectate(ASM_FULL);
            always_spectate_peers.insert(peer.get());
            continue;
        }
        // Spectators won't remove maps as they are already waiting for game
        if (!peer->isValidated() || peer->isWaitingForGame())
            continue;
        if (!peer->isAIPeer() && peer->hasPlayerProfiles()
                && peer->isEligibleForGame())
        {
            peer->eraseServerKarts(m_available_kts.first, karts_erase);
            peer->eraseServerTracks(m_available_kts.second, tracks_erase);
        }
    }

    for (const std::string& kart_erase : karts_erase)
    {
        m_available_kts.first.erase(kart_erase);
    }
    for (const std::string& track_erase : tracks_erase)
    {
        m_available_kts.second.erase(track_erase);
    }
    max_player = 0;
    STKHost::get()->updatePlayers(&max_player);
    if (auto ai = m_ai_peer.lock())
    {
        if (supportsAI())
        {
            unsigned total_ai_available =
                (unsigned)ai->getPlayerProfiles().size();
            m_ai_count = max_player > total_ai_available ?
                0 : total_ai_available - max_player + 1;
            // Disable ai peer for this game
            if (m_ai_count == 0)
                ai->setValidated(false);
            else
                ai->setValidated(true);
        }
        else
        {
            ai->setValidated(false);
            m_ai_count = 0;
        }
    }
    else
        m_ai_count = 0;

    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        auto it = m_available_kts.second.begin();
        while (it != m_available_kts.second.end())
        {
            Track* t =  track_manager->getTrack(*it);
            if (t->getMaxArenaPlayers() < max_player)
            {
                it = m_available_kts.second.erase(it);
            }
            else
                it++;
        }
    }

    // These tracks will never be selected when track voting is disabled
    if (!ServerConfig::m_track_voting)
    {
	    std::vector<std::string> excluded = StringUtils::split(ServerConfig::m_excluded_tracks, ' ');
	    for (const std::string& track : excluded)
	    {
		    std::string addon_track = "addon_" + track;
		    m_available_kts.second.erase(addon_track);
	    }
    }

    RandomGenerator rg;
    std::set<std::string>::iterator it;
    bool track_voting = ServerConfig::m_track_voting;

    if (!m_set_field.empty())
    {
        Log::verbose("ServerLobby", "Disabling voting tracks: set field.");
        m_default_vote->m_track_name = m_set_field;
        m_default_vote->m_num_laps = m_set_laps;
        m_default_vote->m_reverse = m_set_specvalue;
        m_fixed_laps = m_set_laps;
        track_voting = false;
        // ensure that the m_available_kts.second has the said set field.
        m_available_kts.second.insert(m_set_field);
        goto skip_default_vote_randomizing;
    }

    if (m_available_kts.second.empty())
    {
        Log::error("ServerLobby", "No tracks for playing!");
        return;
    }

    it = m_available_kts.second.begin();
    std::advance(it, rg.get((int)m_available_kts.second.size()));
    m_default_vote->m_track_name = *it;
    switch (RaceManager::get()->getMinorMode())
    {
        case RaceManager::MINOR_MODE_NORMAL_RACE:
        case RaceManager::MINOR_MODE_TIME_TRIAL:
        case RaceManager::MINOR_MODE_FOLLOW_LEADER:
        {
            Track* t = track_manager->getTrack(*it);
            assert(t);
            m_default_vote->m_num_laps = t->getDefaultNumberOfLaps();
            if (ServerConfig::m_auto_game_time_ratio > 0.0f)
            {
                m_default_vote->m_num_laps =
                    (uint8_t)(fmaxf(1.0f, (float)t->getDefaultNumberOfLaps() *
                    ServerConfig::m_auto_game_time_ratio));
            }
            else if (m_fixed_laps != -1)
                m_default_vote->m_num_laps = m_fixed_laps;
            m_default_vote->m_reverse = rg.get(2) == 0;
            break;
        }
        case RaceManager::MINOR_MODE_FREE_FOR_ALL:
        {
            m_default_vote->m_num_laps = 0;
            m_default_vote->m_reverse = rg.get(2) == 0;
            break;
        }
        case RaceManager::MINOR_MODE_CAPTURE_THE_FLAG:
        {
            m_default_vote->m_num_laps = 0;
            m_default_vote->m_reverse = 0;
            break;
        }
        case RaceManager::MINOR_MODE_SOCCER:
        {
            if (m_game_setup->isSoccerGoalTarget())
            {
                m_default_vote->m_num_laps =
                    (uint8_t)(UserConfigParams::m_num_goals);
                if (m_default_vote->m_num_laps > 10)
                    m_default_vote->m_num_laps = (uint8_t)5;
            }
            else
            {
                m_default_vote->m_num_laps =
                    (uint8_t)(UserConfigParams::m_soccer_time_limit);
                if (m_default_vote->m_num_laps > 15)
                    m_default_vote->m_num_laps = (uint8_t)7;
            }
            m_default_vote->m_reverse = rg.get(2) == 0;
            break;
        }
        default:
            assert(false);
            break;
    }

skip_default_vote_randomizing:
    if (!allowJoinedPlayersWaiting())
    {
        ProtocolManager::lock()->findAndTerminate(PROTOCOL_CONNECTION);
        if (m_server_id_online.load() != 0)
        {
            unregisterServer(false/*now*/,
                std::dynamic_pointer_cast<ServerLobby>(shared_from_this()));
        }
    }

    float voting_timeout = ServerConfig::m_voting_timeout;
    startVotingPeriod(voting_timeout);
    const auto& all_k = m_available_kts.first;
    const auto& all_t = m_available_kts.second;

    for (auto peer : peers)
    {
        if (!peer->isValidated() || !peer->isEligibleForGame() ||
                peer->isWaitingForGame())
            continue;

        auto profiles = peer->getPlayerProfiles();
        bool hasEnforcedKart = 
            peer->hasPlayerProfiles() && profiles.size() == 1 
            && !profiles[0]->getForcedKart().empty();
        std::string forced_kart;
        if (hasEnforcedKart)
        {
            forced_kart = profiles[0]->getForcedKart();
        }
        // INSERT YOUR SETKART HERE
        NetworkString *ns = getNetworkString(1);
        // Start selection - must be synchronous since the receiver pushes
        // a new screen, which must be done from the main thread.
        ns->setSynchronous(true);
        ns->addUInt8(LE_START_SELECTION)
           .addFloat(voting_timeout)
           .addUInt8(m_game_setup->isGrandPrixStarted() ? 1 : 0)
           .addUInt8((ServerConfig::m_auto_game_time_ratio > 0.0f ||
            m_fixed_laps != -1 || RaceManager::get()->isInfiniteMode()) ? 1 : 0)
           .addUInt8(track_voting ? 1 : 0);


        if (!forced_kart.empty())
        {
            ns->addUInt16(1);
            ns->addUInt16((uint16_t)all_t.size());
            ns->encodeString(forced_kart);
        }
        else
        {
            ns->addUInt16((uint16_t)all_k.size());
            ns->addUInt16((uint16_t)all_t.size());
            for (const std::string& kart : all_k)
            {
                ns->encodeString(kart);
            }
        }
        for (const std::string& track : all_t)
        {
            ns->encodeString(track);
        }

        peer->sendPacket(ns, true/*reliable*/);
        delete ns;
    }
    m_state = SELECTING;    
    if (!always_spectate_peers.empty())
    {
        NetworkString* back_lobby = getNetworkString(2);
        back_lobby->setSynchronous(true);
        back_lobby->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_SPECTATING_NEXT_GAME);
        STKHost::get()->sendPacketToAllPeersWith(
            [](STKPeer* peer) {
            return peer->alwaysSpectate(); }, back_lobby, /*reliable*/true);
        delete back_lobby;
        updatePlayerList();
    }

    if (!allowJoinedPlayersWaiting())
    {
        // Drop all pending players and keys if doesn't allow joinning-waiting
        for (auto& p : m_pending_connection)
        {
            if (auto peer = p.first.lock())
                peer->disconnect();
        }
        m_pending_connection.clear();
        std::unique_lock<std::mutex> ul(m_keys_mutex);
        m_keys.clear();
        ul.unlock();
    }
    // Will be changed after the first vote received
    m_timeout.store(std::numeric_limits<int64_t>::max());


}   // startSelection
//----------------------------------------------------------------------------
// if time stamp needed
std::string ServerLobby::getTimeStamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
    return ss.str();
}

//-----------------------------------------------------------------------------
/** Query the STK server for connection requests. For each connection request
 *  start a ConnectToPeer protocol.
 */
void ServerLobby::checkIncomingConnectionRequests()
{
    // First poll every 5 seconds. Return if no polling needs to be done.
    const uint64_t POLL_INTERVAL = 5000;
    static uint64_t last_poll_time = 0;
    if (StkTime::getMonoTimeMs() < last_poll_time + POLL_INTERVAL ||
        StkTime::getMonoTimeMs() > m_last_success_poll_time.load() + 30000)
        return;

    // Keep the port open, it can be sent to anywhere as we will send to the
    // correct peer later in ConnectToPeer.
    if (ServerConfig::m_firewalled_server)
    {
        BareNetworkString data;
        data.addUInt8(0);
        const SocketAddress* stun_v4 = STKHost::get()->getStunIPv4Address();
        const SocketAddress* stun_v6 = STKHost::get()->getStunIPv6Address();
        if (stun_v4)
            STKHost::get()->sendRawPacket(data, *stun_v4);
        if (stun_v6)
            STKHost::get()->sendRawPacket(data, *stun_v6);
    }

    // Now poll the stk server
    last_poll_time = StkTime::getMonoTimeMs();

    // ========================================================================
    class PollServerRequest : public Online::XMLRequest
    {
    private:
        std::weak_ptr<ServerLobby> m_server_lobby;
        std::weak_ptr<ProtocolManager> m_protocol_manager;
    protected:
        virtual void afterOperation()
        {
            Online::XMLRequest::afterOperation();
            const XMLNode* result = getXMLData();
            std::string success;

            if (!result->get("success", &success) || success != "yes")
            {
                Log::error("ServerLobby", "Poll server request failed: %s",
                    StringUtils::wideToUtf8(getInfo()).c_str());
                return;
            }

            // Now start a ConnectToPeer protocol for each connection request
            const XMLNode * users_xml = result->getNode("users");
            std::map<uint32_t, KeyData> keys;
            auto sl = m_server_lobby.lock();
            if (!sl)
                return;
            sl->m_last_success_poll_time.store(StkTime::getMonoTimeMs());
            if (sl->m_state.load() != WAITING_FOR_START_GAME &&
	    		    !sl->allowJoinedPlayersWaiting())
            {
                sl->replaceKeys(keys);
                return;
            }

            sl->removeExpiredPeerConnection();
            for (unsigned int i = 0; i < users_xml->getNumNodes(); i++)
            {
                uint32_t addr, id;
                uint16_t port;
                std::string ipv6;
                users_xml->getNode(i)->get("ip", &addr);
                users_xml->getNode(i)->get("ipv6", &ipv6);
                users_xml->getNode(i)->get("port", &port);
                users_xml->getNode(i)->get("id", &id);
                users_xml->getNode(i)->get("aes-key", &keys[id].m_aes_key);
                users_xml->getNode(i)->get("aes-iv", &keys[id].m_aes_iv);
                users_xml->getNode(i)->get("username", &keys[id].m_name);
                users_xml->getNode(i)->get("country-code",
                    &keys[id].m_country_code);
                keys[id].m_tried = false;
                if (ServerConfig::m_firewalled_server)
                {
                    SocketAddress peer_addr(addr, port);
                    if (!ipv6.empty())
                        peer_addr.init(ipv6, port);
                    peer_addr.convertForIPv6Socket(isIPv6Socket());
                    std::string peer_addr_str = peer_addr.toString();
                    if (sl->m_pending_peer_connection.find(peer_addr_str) !=
                        sl->m_pending_peer_connection.end())
                    {
                        continue;
                    }
                    auto ctp = std::make_shared<ConnectToPeer>(peer_addr);
                    if (auto pm = m_protocol_manager.lock())
                        pm->requestStart(ctp);
                    sl->addPeerConnection(peer_addr_str);
                }
            }
            sl->replaceKeys(keys);
        }
    public:
        PollServerRequest(std::shared_ptr<ServerLobby> sl,
                          std::shared_ptr<ProtocolManager> pm)
        : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY),
        m_server_lobby(sl), m_protocol_manager(pm)
        {
            m_disable_sending_log = true;
        }
    };   // PollServerRequest
    // ========================================================================

    auto request = std::make_shared<PollServerRequest>(
        std::dynamic_pointer_cast<ServerLobby>(shared_from_this()),
        ProtocolManager::lock());
    NetworkConfig::get()->setServerDetails(request,
        "poll-connection-requests");
    const SocketAddress& addr = STKHost::get()->getPublicAddress();
    request->addParameter("address", addr.getIP()  );
    request->addParameter("port",    addr.getPort());
    request->addParameter("current-players", getLobbyPlayers());
    request->addParameter("current-ai", m_current_ai_count.load());
    request->addParameter("game-started",
        m_state.load() == WAITING_FOR_START_GAME ? 0 : 1);
    std::string current_track = getPlayingTrackIdent();
    if (!current_track.empty())
        request->addParameter("current-track", getPlayingTrackIdent());
    request->queue();

}   // checkIncomingConnectionRequests

//-----------------------------------------------------------------------------
/** Checks if the race is finished, and if so informs the clients and switches
 *  to state RESULT_DISPLAY, during which the race result gui is shown and all
 *  clients can click on 'continue'.
 */
void ServerLobby::checkRaceFinished()
{
    assert(RaceEventManager::get()->isRunning());
    assert(World::getWorld());
    if (!RaceEventManager::get()->isRaceOver()) return;

    Log::info("ServerLobby", "The game is considered finished.");
    // notify the network world that it is stopped
    RaceEventManager::get()->stop();

    // stop race protocols before going back to lobby (end race)
    RaceEventManager::get()->getProtocol()->requestTerminate();
    GameProtocol::lock()->requestTerminate();

    // Save race result before delete the world
    m_result_ns->clear();
    m_result_ns->addUInt8(LE_RACE_FINISHED);
    if (m_game_setup->isGrandPrix())
    {
        // fastest lap
        int fastest_lap =
            static_cast<LinearWorld*>(World::getWorld())->getFastestLapTicks();
        m_result_ns->addUInt32(fastest_lap);
        m_result_ns->encodeString(static_cast<LinearWorld*>(World::getWorld())
            ->getFastestLapKartName());

        // all gp tracks
        m_result_ns->addUInt8((uint8_t)m_game_setup->getTotalGrandPrixTracks())
            .addUInt8((uint8_t)m_game_setup->getAllTracks().size());
        for (const std::string& gp_track : m_game_setup->getAllTracks())
            m_result_ns->encodeString(gp_track);

        // each kart score and total time
        m_result_ns->addUInt8((uint8_t)RaceManager::get()->getNumPlayers());
        for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
        {
            int last_score = RaceManager::get()->getKartScore(i);
            int cur_score = last_score;
            float overall_time = RaceManager::get()->getOverallTime(i);
            if (auto player =
                RaceManager::get()->getKartInfo(i).getNetworkPlayerProfile().lock())
            {
                last_score = player->getScore();
                cur_score += last_score;
                overall_time = overall_time + player->getOverallTime();
                player->setScore(cur_score);
                player->setOverallTime(overall_time);
            }
            m_result_ns->addUInt32(last_score).addUInt32(cur_score)
                .addFloat(overall_time);            
        }
    }
    else if (RaceManager::get()->modeHasLaps())
    {
        int fastest_lap =
            static_cast<LinearWorld*>(World::getWorld())->getFastestLapTicks();
        m_result_ns->addUInt32(fastest_lap);
        m_result_ns->encodeString(static_cast<LinearWorld*>(World::getWorld())
            ->getFastestLapKartName());
    }

    uint8_t ranking_changes_indication = 0;
    if (ServerConfig::m_ranked && RaceManager::get()->modeHasLaps())
        ranking_changes_indication = 1;
    m_result_ns->addUInt8(ranking_changes_indication);

    if (ServerConfig::m_ranked)
    {
        computeNewRankings();
        submitRankingsToAddons();
    }
    m_state.store(WAIT_FOR_RACE_STOPPED);

    // Reset command votings
    ServerLobbyCommands::get()->clearAllVotes();
    // Remove kart restriction after the game is over
    setKartRestrictionMode(NONE);
    // Remove pole mode after the game is over
    setPoleEnabled(false);
    // no powerup modifiers after each game
    RaceManager::get()->setPowerupSpecialModifier(
            Powerup::TSM_NONE);
    if (ServerConfig::m_soccer_roulette)
    {
	    checkSoccerRoulette();
	    GoalHistory::saveGoalHistoryToFile();
	    SoccerRoulette::get()->calculateGameResult();
    } 
    if (ServerConfig::m_tiers_roulette)
    {
        tiers_roulette->rotate();
        tiers_roulette->applyChanges(this, RaceManager::get(), nullptr);
    }
    RaceManager::get()->setNitrolessMode(false);
    
}	// checkRaceFinished

//-----------------------------------------------------------------------------
/** Compute the new player's rankings used in ranked servers
 */
void ServerLobby::computeNewRankings()
{
    // No ranking for battle mode
    if (!RaceManager::get()->modeHasLaps())
        return;

    World* w = World::getWorld();
    assert(w);

    unsigned player_count = RaceManager::get()->getNumPlayers();

    // If all players quitted the race, we assume something went wrong
    // and skip entirely rating and statistics updates.
    for (unsigned i = 0; i < player_count; i++)
    {
        if (!w->getKart(i)->isEliminated())
            break;
        if ((i + 1) == player_count)
            return;
    }
    
    // Fill the results for the rankings to process
    std::vector<RaceResultData> data;
    for (unsigned i = 0; i < player_count; i++)
    {
        RaceResultData entry;
        entry.online_id = RaceManager::get()->getKartInfo(i).getOnlineId();
        entry.is_eliminated = w->getKart(i)->isEliminated();
        entry.time = RaceManager::get()->getKartRaceTime(i);
        entry.handicap = w->getKart(i)->getHandicap();
        data.push_back(entry);
    }

    m_ranking->computeNewRankings(data, RaceManager::get()->isTimeTrialMode());

    // Used to display rating change at the end of a race
    m_result_ns->addUInt8((uint8_t)player_count);
    for (unsigned i = 0; i < player_count; i++)
    {
        const uint32_t id = RaceManager::get()->getKartInfo(i).getOnlineId();
        double change = m_ranking->getDelta(id);
        m_result_ns->addFloat((float)change);
    }
}   // computeNewRankings
//-----------------------------------------------------------------------------
/** Called when a client disconnects.
 *  \param event The disconnect event.
 */
void ServerLobby::clientDisconnected(Event* event)
{
    auto players_on_peer = event->getPeer()->getPlayerProfiles();
    if (players_on_peer.empty())
        return;

    NetworkString* msg = getNetworkString(2);
    const bool waiting_peer_disconnected =
        event->getPeer()->isWaitingForGame();
    msg->setSynchronous(true);
    msg->addUInt8(LE_PLAYER_DISCONNECTED);
    msg->addUInt8((uint8_t)players_on_peer.size())
        .addUInt32(event->getPeer()->getHostId());
    for (auto p : players_on_peer)
    {
        std::string name = StringUtils::wideToUtf8(p->getName());
        msg->encodeString(name);
        Log::info("ServerLobby", "%s disconnected", name.c_str());
    }

    std::string msg2;
    std::string player_name;
    if(ServerConfig::m_soccer_log || ServerConfig::m_race_log)
    {
        World* w = World::getWorld();
        if (w)
        {
            std::string time = std::to_string(w->getTime());
            for (const auto id : event->getPeer()->getAvailableKartIDs())
            {
                player_name = GlobalLog::getPlayerName(id);
                msg2 =  player_name + " left the game at " + time + ". \n";
                GlobalLog::writeLog(msg2, GlobalLogTypes::POS_LOG);
                GlobalLog::removeIngamePlayer(id);
            }
        }
    }
    

    // Don't show waiting peer disconnect message to in game player
    STKHost::get()->sendPacketToAllPeersWith([waiting_peer_disconnected]
        (STKPeer* p)
        {
            if (!p->isValidated())
                return false;
            if (!p->isWaitingForGame() && waiting_peer_disconnected)
                return false;
            return true;
        }, msg);
    updatePlayerList();
    delete msg;

#ifdef ENABLE_SQLITE3
    m_db->writeDisconnectInfoTable(event->getPeer());
#endif

    auto peer = event->getPeer();
    // On last player
    if (!STKHost::get()->getPeerCount())
    {
        if (!ServerConfig::m_tiers_roulette)
        {
            setKartRestrictionMode(NONE);
            setPoleEnabled(false);
            RaceManager::get()->setPowerupSpecialModifier(
                Powerup::TSM_NONE);
        }
        m_blue_pole_votes.clear();
        m_red_pole_votes.clear();
        RaceManager::get()->setNitrolessMode(false);

        if (m_replay_requested && RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_TIME_TRIAL)
        {
            m_replay_requested = false;
        }
        if (m_random_karts_enabled)
        {
            m_random_karts_enabled = false;
        }
    }
    else 
    {
        auto b = m_blue_pole_votes.find(peer);
        auto r = m_red_pole_votes.find(peer);
        if (b != m_blue_pole_votes.cend())
            m_blue_pole_votes.erase(b);
        if (r != m_red_pole_votes.cend())
            m_red_pole_votes.erase(r);
    }
    RaceManager::get()->resetPoleProfile(event->getPeer());
    // reset player command votings. TODO: delete
    // send a message to the wrapper
    if (peer->hasPlayerProfiles())
        Log::verbose("ServerLobby", "playerleave %s %d",
            StringUtils::wideToUtf8(
                peer->getPlayerProfiles()[0]->getName()).c_str(),
                peer->getPlayerProfiles()[0]->getOnlineId());

    ServerLobbyCommands::get()->onPeerLeave(this, peer);
    LobbyPlayerQueue::get()->onPeerLeave(peer);

}   // clientDisconnected

//-----------------------------------------------------------------------------
void ServerLobby::kickPlayerWithReason(STKPeer* peer, const char* reason) const
{
    NetworkString *message = getNetworkString(2);
    message->setSynchronous(true);
    message->addUInt8(LE_CONNECTION_REFUSED).addUInt8(RR_BANNED);
    message->encodeString(std::string(reason));
    peer->cleanPlayerProfiles();
    peer->sendPacket(message, true/*reliable*/, false/*encrypted*/);
    peer->reset();
    delete message;
}   // kickPlayerWithReason

//-----------------------------------------------------------------------------
void ServerLobby::saveIPBanTable(const SocketAddress& addr)
{
#ifdef ENABLE_SQLITE3
    m_db->saveAddressToIpBanTable(addr);
#endif
}   // saveIPBanTable

//-----------------------------------------------------------------------------
void ServerLobby::removeIPBanTable(const SocketAddress& addr)
{
#ifdef ENABLE_SQLITE3
    if (addr.isIPv6() || !m_db || !m_db->hasIpBanTable())
        return;

    std::string query = StringUtils::insertValues(
        "DELETE FROM %s "
        "WHERE ip_start = %u AND ip_end = %u;",
        ServerConfig::m_ip_ban_table.c_str(), addr.getIP(), addr.getIP());
    SQLiteDatabase* sqlite_db = dynamic_cast<SQLiteDatabase*>(m_db);
    if (sqlite_db)
        sqlite_db->easySQLQuery(query);
#endif
}   // removeIPBanTable

//-----------------------------------------------------------------------------
bool ServerLobby::handleAssets(const NetworkString& ns,
        std::shared_ptr<STKPeer> peer)
{
    std::set<std::string> client_karts, client_tracks;
    const unsigned kart_num = ns.getUInt16();
    const unsigned track_num = ns.getUInt16();
    for (unsigned i = 0; i < kart_num; i++)
    {
        std::string kart;
        ns.decodeString(&kart);
        client_karts.insert(kart);
    }
    for (unsigned i = 0; i < track_num; i++)
    {
        std::string track;
        ns.decodeString(&track);
        client_tracks.insert(track);
    }

    // Drop this player if he doesn't have at least 1 kart / track the same
    // as server
    float okt = 0.0f;
    float ott = 0.0f;
    for (auto& client_kart : client_karts)
    {
        if (m_official_kts.first.find(client_kart) !=
            m_official_kts.first.end())
            okt += 1.0f;
    }
    okt = okt / (float)m_official_kts.first.size();
    for (auto& client_track : client_tracks)
    {
        if (m_official_kts.second.find(client_track) !=
            m_official_kts.second.end())
            ott += 1.0f;
    }
    ott = ott / (float)m_official_kts.second.size();

    std::set<std::string> karts_erase, tracks_erase;
    for (const std::string& server_kart : m_available_kts.first)
    {
        if (client_karts.find(server_kart) == client_karts.end())
        {
            karts_erase.insert(server_kart);
        }
    }
    for (const std::string& server_track : m_available_kts.second)
    {
        if (client_tracks.find(server_track) == client_tracks.end())
        {
            tracks_erase.insert(server_track);
        }
    }

    if (karts_erase.size() == m_available_kts.first.size() ||
        tracks_erase.size() == m_available_kts.second.size() ||
        okt < ServerConfig::m_official_karts_threshold ||
        ott < ServerConfig::m_official_tracks_threshold)
    {
        NetworkString *message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED)
            .addUInt8(RR_INCOMPATIBLE_DATA);
        peer->cleanPlayerProfiles();
        peer->sendPacket(message, true/*reliable*/, false/*encrypted*/);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player has incompatible karts / tracks.");
        return false;
    }

    std::array<int, AS_TOTAL> addons_scores = {{ -1, -1, -1, -1 }};
    size_t addon_kart = 0;
    size_t addon_track = 0;
    size_t addon_arena = 0;
    size_t addon_soccer = 0;

    for (auto& kart : m_addon_kts.first)
    {
        if (client_karts.find(kart) != client_karts.end())
            addon_kart++;
    }
    for (auto& track : m_addon_kts.second)
    {
        if (client_tracks.find(track) != client_tracks.end())
            addon_track++;
    }
    for (auto& arena : m_addon_arenas)
    {
        if (client_tracks.find(arena) != client_tracks.end())
            addon_arena++;
    }
    for (auto& soccer : m_addon_soccers)
    {
        if (client_tracks.find(soccer) != client_tracks.end())
            addon_soccer++;
    }

    if (!m_addon_kts.first.empty())
    {
        addons_scores[AS_KART] = int
            ((float)addon_kart / (float)m_addon_kts.first.size() * 100.0);
    }
    if (!m_addon_kts.second.empty())
    {
        addons_scores[AS_TRACK] = int
            ((float)addon_track / (float)m_addon_kts.second.size() * 100.0);
    }
    if (!m_addon_arenas.empty())
    {
        addons_scores[AS_ARENA] = int
            ((float)addon_arena / (float)m_addon_arenas.size() * 100.0);
    }
    if (!m_addon_soccers.empty())
    {
        addons_scores[AS_SOCCER] = int
            ((float)addon_soccer / (float)m_addon_soccers.size() * 100.0);
    }

    // Save available karts and tracks from clients in STKPeer so if this peer
    // disconnects later in lobby it won't affect current players
    peer->setAvailableKartsTracks(client_karts, client_tracks);
    peer->setAddonsScores(addons_scores);

    if (m_process_type == PT_CHILD &&
        peer->getHostId() == m_client_server_host_id.load())
    {
        // Update child process addons list too so player can choose later
        updateAddons();
        updateTracksForMode();
    }
    const PeerEligibility old_el = peer->getEligibility();
    peer->testEligibility();
    // eligibility hooks
    LobbyPlayerQueue::get()->onPeerEligibilityChange(peer, old_el);
    return true;
}   // handleAssets

//-----------------------------------------------------------------------------
void ServerLobby::connectionRequested(Event* event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    NetworkString& data = event->data();
    if (!checkDataSize(event, 14)) return;

    peer->cleanPlayerProfiles();

    // can we add the player ?
    if (!allowJoinedPlayersWaiting() &&
        (m_state.load() != WAITING_FOR_START_GAME ||
        m_game_setup->isGrandPrixStarted()))
    {
        NetworkString *message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED).addUInt8(RR_BUSY);
        // send only to the peer that made the request and disconnect it now
        peer->sendPacket(message, true/*reliable*/, false/*encrypted*/);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: selection started");
        return;
    }

    // Check server version
    int version = data.getUInt32();
    if (version < stk_config->m_min_server_version ||
        version > stk_config->m_max_server_version)
    {
        NetworkString *message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_INCOMPATIBLE_DATA);
        peer->sendPacket(message, true/*reliable*/, false/*encrypted*/);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: wrong server version");
        return;
    }
    std::string user_version;
    data.decodeString(&user_version);
    event->getPeer()->setUserVersion(user_version);

    unsigned list_caps = data.getUInt16();
    std::set<std::string> caps;
    for (unsigned i = 0; i < list_caps; i++)
    {
        std::string cap;
        data.decodeString(&cap);
        caps.insert(cap);
    }
    event->getPeer()->setClientCapabilities(caps);
    if (!handleAssets(data, event->getPeerSP()))
        return;

    unsigned player_count = data.getUInt8();
    uint32_t online_id = 0;
    uint32_t encrypted_size = 0;
    online_id = data.getUInt32();
    encrypted_size = data.getUInt32();

    // Will be disconnected if banned by IP
    testBannedForIP(peer.get());
    if (peer->isDisconnected())
        return;

    testBannedForIPv6(peer.get());
    if (peer->isDisconnected())
        return;

    if (online_id != 0)
        testBannedForOnlineId(peer.get(), online_id);
    // Will be disconnected if banned by online id
    if (peer->isDisconnected())
        return;

    unsigned total_players = 0;
    STKHost::get()->updatePlayers(NULL, NULL, &total_players);
    if (total_players + player_count + m_ai_profiles.size() >
        (unsigned)ServerConfig::m_server_max_players)
    {
        NetworkString *message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED).addUInt8(RR_TOO_MANY_PLAYERS);
        peer->sendPacket(message, true/*reliable*/, false/*encrypted*/);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: too many players");
        return;
    }

    // Reject non-valiated player joinning if WAN server and not disabled
    // encforement of validation, unless it's player from localhost or lan
    // And no duplicated online id or split screen players in ranked server
    // AIPeer only from lan and only 1 if ai handling
    std::set<uint32_t> all_online_ids =
        STKHost::get()->getAllPlayerOnlineIds();
    bool duplicated_ranked_player =
        all_online_ids.find(online_id) != all_online_ids.end();

    if (((encrypted_size == 0 || online_id == 0) &&
        !(peer->getAddress().isPublicAddressLocalhost() ||
        peer->getAddress().isLAN()) &&
        NetworkConfig::get()->isWAN() &&
        ServerConfig::m_validating_player) ||
        (ServerConfig::m_strict_players &&
        (player_count != 1 || online_id == 0 || duplicated_ranked_player)) ||
        (peer->isAIPeer() && !peer->getAddress().isLAN() &&!ServerConfig::m_ai_anywhere) ||
        (peer->isAIPeer() &&
        ServerConfig::m_ai_handling && !m_ai_peer.expired()))
    {
        NetworkString* message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED).addUInt8(RR_INVALID_PLAYER);
        peer->sendPacket(message, true/*reliable*/, false/*encrypted*/);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: invalid player");
        return;
    }

    if (ServerConfig::m_ai_handling && peer->isAIPeer())
        m_ai_peer = peer;

    if (encrypted_size != 0)
    {
        m_pending_connection[peer] = std::make_pair(online_id,
            BareNetworkString(data.getCurrentData(), encrypted_size));
    }
    else
    {
        core::stringw online_name;
        if (online_id > 0)
            data.decodeStringW(&online_name);
        handleUnencryptedConnection(peer, data, online_id, online_name,
            false/*is_pending_connection*/);
    }
}   // connectionRequested

//-----------------------------------------------------------------------------
void ServerLobby::handleUnencryptedConnection(std::shared_ptr<STKPeer> peer,
    BareNetworkString& data, uint32_t online_id,
    const core::stringw& online_name, bool is_pending_connection,
    std::string country_code)
{
    if (data.size() < 2) return;

    // Check for password
    std::string password;
    data.decodeString(&password);
    const std::string& server_pw = ServerConfig::m_private_server_password;
    if (password != server_pw)
    {
        NetworkString *message = getNetworkString(2);
        message->setSynchronous(true);
        message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_INCORRECT_PASSWORD);
        peer->sendPacket(message, true/*reliable*/, false/*encrypted*/);
        peer->reset();
        delete message;
        Log::verbose("ServerLobby", "Player refused: incorrect password");
        return;
    }

    // Check again max players and duplicated player in ranked server,
    // if this is a pending connection
    unsigned total_players = 0;
    unsigned player_count = data.getUInt8();

    if (is_pending_connection)
    {
        STKHost::get()->updatePlayers(NULL, NULL, &total_players);
        if (total_players + player_count >
            (unsigned)ServerConfig::m_server_max_players)
        {
            NetworkString *message = getNetworkString(2);
            message->setSynchronous(true);
            message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_TOO_MANY_PLAYERS);
            peer->sendPacket(message, true/*reliable*/, false/*encrypted*/);
            peer->reset();
            delete message;
            Log::verbose("ServerLobby", "Player refused: too many players");
            return;
        }

        std::set<uint32_t> all_online_ids =
            STKHost::get()->getAllPlayerOnlineIds();
        bool duplicated_ranked_player =
            all_online_ids.find(online_id) != all_online_ids.end();
        if (ServerConfig::m_ranked && duplicated_ranked_player)
        {
            NetworkString* message = getNetworkString(2);
            message->setSynchronous(true);
            message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_INVALID_PLAYER);
            peer->sendPacket(message, true/*reliable*/, false/*encrypted*/);
            peer->reset();
            delete message;
            Log::verbose("ServerLobby", "Player refused: invalid player");
            return;
        }
    }

#ifdef ENABLE_SQLITE3
    if (country_code.empty() && !peer->getAddress().isIPv6())
        country_code = m_db->ip2Country(peer->getAddress());
    if (country_code.empty() && peer->getAddress().isIPv6())
        country_code = m_db->ipv62Country(peer->getAddress());
#endif

    int permlvl;
    uint32_t restrictions;
    std::string set_kart;
    auto red_blue = STKHost::get()->getAllPlayersTeamInfo();
    if (ServerConfig::m_server_owner > 0 && 
            online_id == ServerConfig::m_server_owner)
    {
        permlvl = std::numeric_limits<int>::max();
    }
    else
    {
        permlvl = loadPermissionLevelForOID(online_id);
    }
    peer->setPermissionLevel(permlvl);
    auto restrictions_set_kart = loadRestrictionsForOID(online_id);
    restrictions = std::get<0>(restrictions_set_kart);
    set_kart = std::get<1>(restrictions_set_kart);
    peer->setRestrictions(restrictions);

    for (unsigned i = 0; i < player_count; i++)
    {
        core::stringw name;
        data.decodeStringW(&name);
        // 30 to make it consistent with stk-addons max user name length
        if (name.empty())
            name = L"unnamed";
        else if (name.size() > 30)
            name = name.subString(0, 30);
        float default_kart_color = data.getFloat();
        HandicapLevel handicap = (HandicapLevel)data.getUInt8();
        if (restrictions & PRF_HANDICAP)
            handicap = HANDICAP_MEDIUM;

        auto player = std::make_shared<NetworkPlayerProfile>
            (peer, i == 0 && !online_name.empty() && !peer->isAIPeer() ?
            online_name : name,
            peer->getHostId(), default_kart_color, i == 0 ? online_id : 0,
            handicap, (uint8_t)i, KART_TEAM_NONE,
            country_code);

        std::string utf8_online_name = StringUtils::wideToUtf8(online_name);

        if (!set_kart.empty())
            player->forceKart(set_kart);

        if (peer->hasRestriction(PRF_NOGAME) ||
                peer->getPermissionLevel() < PERM_PLAYER)
        {
            player->setTeam(KART_TEAM_NONE);
        }

        else if (ServerConfig::m_team_choosing)
        {
            KartTeam cur_team = KART_TEAM_NONE;
            if (red_blue.first > red_blue.second)
            {
                cur_team = KART_TEAM_BLUE;
                red_blue.second++;
            }
            else
	    {
		    cur_team = KART_TEAM_RED;
		    red_blue.first++;
	    }
	    player->setTeam(cur_team);
	}
        peer->addPlayer(player);
    }

    peer->setValidated(true);

    // send a message to the one that asked to connect
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    peer->sendPacket(server_info);
    delete server_info;

    sendRandomInstalladdonLine(peer);
    sendCurrentModifiers(peer);
    
    // Assign random kart
    if (m_random_karts_enabled && m_state.load() == WAITING_FOR_START_GAME)
    {
	    auto player = peer->getPlayerProfiles();
	    if (!player.empty())
	    {
		    RandomGenerator random_gen;
		    for (unsigned i = 0; i < player.size(); i++)
		    {
			    auto& player_profile = player[i];
			    std::set<std::string>::iterator it = m_available_kts.first.begin();
			    std::advance(it, random_gen.get((int)m_available_kts.first.size()));
			    std::string selected_kart = *it;
			    player_profile->forceKart(selected_kart);
			    std::string msg = "Random kart has been assigned because /randomkarts is currently enabled";
			    sendStringToPeer(msg, peer);
		    }
	    }
    }

    if (!checkAllStandardContentInstalled(peer.get()))
    {
	    std::string player_name;
	    if (!peer->getPlayerProfiles().empty())
		    player_name = StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName());
	    Log::info("ServerLobby", "Player %s doesn't have all standard content installed.",
			    player_name.c_str());
    }
    if (ServerConfig::m_soccer_roulette)
    {
	    std::shared_ptr<NetworkPlayerProfile> profile = peer->getPlayerProfiles()[0];
	    SoccerRoulette::get()->assignTeamToPlayer(profile.get());
            updatePlayerList();
    }


    const bool game_started = m_state.load() != WAITING_FOR_START_GAME;
    NetworkString* message_ack = getNetworkString(4);
    message_ack->setSynchronous(true);
    // connection success -- return the host id of peer
    float auto_start_timer = 0.0f;
    if (m_timeout.load() == std::numeric_limits<int64_t>::max())
        auto_start_timer = std::numeric_limits<float>::max();
    else
    {
        auto_start_timer =
            (m_timeout.load() - (int64_t)StkTime::getMonoTimeMs()) / 1000.0f;
    }
    message_ack->addUInt8(LE_CONNECTION_ACCEPTED).addUInt32(peer->getHostId())
        .addUInt32(ServerConfig::m_server_version);

    message_ack->addUInt16(
        (uint16_t)stk_config->m_network_capabilities.size());
    for (const std::string& cap : stk_config->m_network_capabilities)
        message_ack->encodeString(cap);

    message_ack->addFloat(auto_start_timer)
        .addUInt32(ServerConfig::m_state_frequency)
        .addUInt8(ServerConfig::m_chat ? 1 : 0)
        .addUInt8(m_db->hasPlayerReportsTable() ? 1 : 0);

    peer->setSpectator(false);

    // The 127.* or ::1/128 will be in charged for controlling AI
    if (m_ai_profiles.empty() && peer->getAddress().isLoopback())
    {
        unsigned ai_add = NetworkConfig::get()->getNumFixedAI();
        unsigned max_players = ServerConfig::m_server_max_players;
        // We need to reserve at least 1 slot for new player
        if (player_count + ai_add + 1 > max_players)
        {
            if (max_players >= player_count + 1)
                ai_add = max_players - player_count - 1;
            else
                ai_add = 0;
        }
        for (unsigned i = 0; i < ai_add; i++)
        {
            core::stringw name = L"Bot";
            name += core::stringw(" ") + StringUtils::toWString(i + 1);
            
            m_ai_profiles.push_back(std::make_shared<NetworkPlayerProfile>
                (peer, name, peer->getHostId(), 0.0f, 0, HANDICAP_NONE,
                player_count + i, KART_TEAM_NONE, ""));
        }
    }

    if (game_started)
    {
        peer->setWaitingForGame(true);
        updatePlayerList();
        peer->sendPacket(message_ack);
        delete message_ack;
    }
    else
    {
        peer->setWaitingForGame(false);
        m_peers_ready[peer] = false;
        if (!ServerConfig::m_sql_management)
        {
            for (std::shared_ptr<NetworkPlayerProfile>& npp :
                peer->getPlayerProfiles())
            {
                Log::info("ServerLobby",
                    "New player %s with online id %u from %s with %s.",
                    StringUtils::wideToUtf8(npp->getName()).c_str(),
                    npp->getOnlineId(), peer->getAddress().toString().c_str(),
                    peer->getUserVersion().c_str());
            }
        }
        updatePlayerList();
        peer->sendPacket(message_ack);
        delete message_ack;

        if (ServerConfig::m_ranked)
        {
            getRankingForPlayer(peer->getPlayerProfiles()[0]);
        }
    }

    if (peer->hasPlayerProfiles())
        Log::verbose("ServerLobby", "playerjoin %s %d",
                StringUtils::wideToUtf8(
                    peer->getPlayerProfiles()[0]->getName()).c_str(),
                peer->getPlayerProfiles()[0]->getOnlineId());
    ServerLobbyCommands::get()->onPeerJoin(this, peer);
    LobbyPlayerQueue::get()->onPeerJoin(peer);
#ifdef ENABLE_SQLITE3
    m_db->onPlayerJoinQueries(peer, online_id, player_count, country_code);
#endif
}   // handleUnencryptedConnection

//-----------------------------------------------------------------------------
/** Called when any players change their setting (team for example), or
 *  connection / disconnection, it will use the game_started parameter to
 *  determine if this should be send to all peers in server or just in game.
 *  \param update_when_reset_server If true, this message will be sent to
 *  all peers.
 */
void ServerLobby::updatePlayerList(bool update_when_reset_server)
{
    const bool game_started = m_state.load() != WAITING_FOR_START_GAME &&
        !update_when_reset_server;

    auto all_profiles = STKHost::get()->getAllPlayerProfiles();
    size_t all_profiles_size = all_profiles.size();
    for (auto& profile : all_profiles)
    {
        if (profile->getPeer()->alwaysSpectate())
            all_profiles_size--;
    }

    // N - 1 AI
    auto ai_instance = m_ai_peer.lock();
    if (supportsAI())
    {
        if (ai_instance)
        {
            auto ai_profiles = ai_instance->getPlayerProfiles();
            if (m_state.load() == WAITING_FOR_START_GAME ||
                update_when_reset_server)
            {
                if (all_profiles_size > ai_profiles.size())
                    ai_profiles.clear();
                else if (all_profiles_size != 0)
                {
                    ai_profiles.resize(
                        ai_profiles.size() - all_profiles_size + 1);
                }
            }
            else
            {
                // Use fixed number of AI calculated when started game
                ai_profiles.resize(m_ai_count);
            }
            all_profiles.insert(all_profiles.end(), ai_profiles.begin(),
                ai_profiles.end());
            m_current_ai_count.store((int)ai_profiles.size());
        }
        else if (!m_ai_profiles.empty())
        {
            all_profiles.insert(all_profiles.end(), m_ai_profiles.begin(),
                m_ai_profiles.end());
            m_current_ai_count.store((int)m_ai_profiles.size());
        }
    }
    else
        m_current_ai_count.store(0);

    m_lobby_players.store((int)all_profiles.size());

    // No need to update player list (for started grand prix currently)
    if (!allowJoinedPlayersWaiting() &&
        m_state.load() > WAITING_FOR_START_GAME && !update_when_reset_server)
        return;

    NetworkString* pl = getNetworkString();
    pl->setSynchronous(true);
    pl->addUInt8(LE_UPDATE_PLAYER_LIST)
        .addUInt8((uint8_t)(game_started ? 1 : 0))
        .addUInt8((uint8_t)all_profiles.size());
    for (auto profile : all_profiles)
    {
        const bool is_spectator_by_limit = LobbyPlayerQueue::get()->isSpectatorByLimit(profile->getPeer().get());
        auto profile_name = profile->getName();
        auto user_name = StringUtils::wideToUtf8(profile->getName());

        // get OS information
        auto version_os = StringUtils::extractVersionOS(profile->getPeer()->getUserVersion());
        std::string os_type_str = version_os.second;
        // if mobile OS
        if (os_type_str == "iOS" || os_type_str == "Android")
            // Add a Mobile emoji for mobile OS
            profile_name = StringUtils::utf32ToWide({ 0x1F4F1 }) + profile_name;

        // Add an hourglass emoji for players waiting because of the player limit
        if (is_spectator_by_limit) 
            profile_name = StringUtils::utf32ToWide({ 0x231B }) + profile_name;

        // Show the Player Elo in case the server have enabled it
	std::pair<unsigned int, int> elorank;
	if (m_show_elo || m_show_rank)
		elorank = getSoccerRanking(user_name);
	if (m_show_elo)
	{
		int display_elo = (elorank.second == 0) ? 1000 : elorank.second;
		profile_name = profile_name + L" [" + std::to_wstring(display_elo).c_str() + L"]";
	}
	if (m_show_rank)
	{
		core::stringw rankstr(L"#");
		if (elorank.first == std::numeric_limits<unsigned int>::max())
			rankstr.append(L"?");
		else
			rankstr.append(irr::core::stringw(elorank.first));
		profile_name = rankstr + L" " + profile_name;
	}

	if (checkXmlEmoji(user_name))
	{
		profile_name = StringUtils::utf32ToWide({0x1f3c6}) + " " + profile_name;
	}
        pl->addUInt32(profile->getHostId()).addUInt32(profile->getOnlineId())
            .addUInt8(profile->getLocalPlayerId())
            .encodeString(profile_name);

        std::shared_ptr<STKPeer> p = profile->getPeer();
        uint8_t boolean_combine = 0;

        // Show tux icon, otherwise show hourglass (lobby or ingame)
        if (p && p->isWaitingForGame())
            boolean_combine |= 1;

        // Show monitor display icon (spectating)
        if (!p->isAIPeer() && p && (p->isSpectator() ||
            ((m_state.load() == WAITING_FOR_START_GAME ||
            update_when_reset_server) && p->alwaysSpectate())))
            boolean_combine |= (1 << 1);

        // Show crown icon (owner)
        if (p && m_server_owner_id.load() == p->getHostId())
            boolean_combine |= (1 << 2);

        // Show checkmark icon (player is ready)
        if (ServerConfig::m_owner_less && !game_started &&
            m_peers_ready.find(p) != m_peers_ready.end() &&
            m_peers_ready.at(p))
            boolean_combine |= (1 << 3);

        // Show robot icon (player is controlled by AI)
        if ((p && p->isAIPeer()) || isAIProfile(profile))
            boolean_combine |= (1 << 4);

        pl->addUInt8(boolean_combine);
        pl->addUInt8(profile->getHandicap());
        if (ServerConfig::m_team_choosing &&
            RaceManager::get()->teamEnabled())
            pl->addUInt8(profile->getTeam());
        else
            pl->addUInt8(KART_TEAM_NONE);
        pl->encodeString(profile->getCountryCode());
    }

    // Don't send this message to in-game players
    STKHost::get()->sendPacketToAllPeersWith([game_started]
        (STKPeer* p)
        {
            if (!p->isValidated())
                return false;
            if (!p->isWaitingForGame() && game_started)
                return false;
            return true;
        }, pl);
    delete pl;
}   // updatePlayerList

//-----------------------------------------------------------------------------
void ServerLobby::updateServerOwner(std::shared_ptr<STKPeer> owner)
{
    if (m_state.load() < WAITING_FOR_START_GAME ||
        m_state.load() > RESULT_DISPLAY ||
        ServerConfig::m_owner_less)
        return;
    if (!owner && !m_server_owner.expired())
        return;
    auto peers = STKHost::get()->getPeers();
    if (peers.empty())
        return;
    std::sort(peers.begin(), peers.end(), [](const std::shared_ptr<STKPeer> a,
        const std::shared_ptr<STKPeer> b)->bool
        {
            return a->getHostId() < b->getHostId();
        });

    if (!owner)
    {
        for (auto peer: peers)
        {
            // Only matching host id can be server owner in case of
            // graphics-client-server
            if (peer->isValidated() && !peer->isAIPeer() &&
                (m_process_type == PT_MAIN ||
                peer->getHostId() == m_client_server_host_id.load()))
            {
                owner = peer;
                break;
            }
        }
    }
    if (owner)
    {
        NetworkString* ns = getNetworkString();
        ns->setSynchronous(true);
        ns->addUInt8(LE_SERVER_OWNERSHIP);
        owner->sendPacket(ns);
        delete ns;
        m_server_owner = owner;
        m_server_owner_id.store(owner->getHostId());
        updatePlayerList();
    }
}   // updateServerOwner

//-----------------------------------------------------------------------------
/*! \brief Called when a player asks to select karts.
 *  \param event : Event providing the information.
 */
void ServerLobby::kartSelectionRequested(Event* event)
{
    if (m_state != SELECTING || m_game_setup->isGrandPrixStarted())
    {
        Log::warn("ServerLobby", "Received kart selection while in state %d.",
                  m_state.load());
        return;
    }

    if (!checkDataSize(event, 1) ||
        event->getPeer()->getPlayerProfiles().empty())
        return;

    const NetworkString& data = event->data();
    STKPeer* peer = event->getPeer();
    setPlayerKarts(data, peer);
}   // kartSelectionRequested

//-----------------------------------------------------------------------------
/*! \brief Called when a player votes for track(s), it will auto correct client
 *         data if it sends some invalid data.
 *  \param event : Event providing the information.
 */
void ServerLobby::handlePlayerVote(Event* event)
{
    if (m_state != SELECTING || !ServerConfig::m_track_voting)
    {
        Log::warn("ServerLobby", "Received track vote while in state %d.",
                  m_state.load());
        return;
    }

    if (!m_set_field.empty())
        return;

    if (!checkDataSize(event, 4) ||
        event->getPeer()->getPlayerProfiles().empty() ||
        event->getPeer()->isWaitingForGame())
        return;

    if (isVotingOver())  return;

    // Check permissions, otherwise won't vote for anything
    if (event->getPeer()->hasRestriction(PRF_TRACK)
            || event->getPeer()->getPermissionLevel()
            < PERM_PLAYER)
        return;

    NetworkString& data = event->data();
    PeerVote vote(data);
    Log::verbose("ServerLobby",
        "Vote from client: host %d, track %s, laps %d, reverse %d.",
        event->getPeer()->getHostId(), vote.m_track_name.c_str(),
        vote.m_num_laps, vote.m_reverse);

    Track* t = track_manager->getTrack(vote.m_track_name);
    if (!t)
    {
        vote.m_track_name = *m_available_kts.second.begin();
        t = track_manager->getTrack(vote.m_track_name);
        assert(t);
    }

    // Remove / adjust any invalid settings
    if (RaceManager::get()->isInfiniteMode())
    {
        vote.m_num_laps = std::numeric_limits<uint8_t>::max();
    }
    else if (RaceManager::get()->modeHasLaps())
    {
        if (ServerConfig::m_auto_game_time_ratio > 0.0f)
        {
            vote.m_num_laps =
                (uint8_t)(fmaxf(1.0f, (float)t->getDefaultNumberOfLaps() *
                ServerConfig::m_auto_game_time_ratio));
        }
        else if (m_game_setup->isSoccerGoalTarget())
        {
            if (ServerConfig::m_auto_game_time_ratio > 0.0f)
            {
                vote.m_num_laps = (uint8_t)(ServerConfig::m_auto_game_time_ratio *
                                            UserConfigParams::m_num_goals);
            }
            else if (vote.m_num_laps > 10)
                vote.m_num_laps = (uint8_t)5;
        }
        else
        {
            if (ServerConfig::m_auto_game_time_ratio > 0.0f)
            {
                vote.m_num_laps = (uint8_t)(ServerConfig::m_auto_game_time_ratio *
                                            UserConfigParams::m_soccer_time_limit);
            }
            else if (vote.m_num_laps > 15)
                vote.m_num_laps = (uint8_t)7;
        }
    }
    else if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        vote.m_num_laps = 0;
    }
    else if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
    {
        vote.m_num_laps = 0;
        vote.m_reverse = false;
    }

    // Store vote:
    vote.m_player_name = event->getPeer()->getPlayerProfiles()[0]->getName();
    addVote(event->getPeer()->getHostId(), vote);

    // Now inform all clients about the vote
    NetworkString other = NetworkString(PROTOCOL_LOBBY_ROOM);
    other.setSynchronous(true);
    other.addUInt8(LE_VOTE);
    other.addUInt32(event->getPeer()->getHostId());
    vote.encode(&other);
    sendMessageToPeers(&other);

}   // handlePlayerVote

// ----------------------------------------------------------------------------
/** Select the track to be used based on all votes being received.
 * \param winner_vote The PeerVote that was picked.
 * \param winner_peer_id The host id of winner (unchanged if no vote).
 *  \return True if race can go on, otherwise wait.
 */
bool ServerLobby::handleAllVotes(PeerVote* winner_vote,
                                 uint32_t* winner_peer_id)
{
    // Determine majority agreement when 35% of voting time remains,
    // reserve some time for kart selection so it's not 50%
    if (getRemainingVotingTime() / getMaxVotingTime() > 0.35f)
    {
        return false;
    }

    // First remove all votes from disconnected hosts
    auto it = m_peers_votes.begin();
    while (it != m_peers_votes.end())
    {
        auto peer = STKHost::get()->findPeerByHostId(it->first);
        if (peer == nullptr)
        {
            it = m_peers_votes.erase(it);
        }
        else
            it++;
    }

    if (m_peers_votes.empty())
    {
        if (isVotingOver())
        {
            *winner_vote = *m_default_vote;
            return true;
        }
        return false;
    }

    // Count number of players 
    float cur_players = 0.0f;
    auto peers = STKHost::get()->getPeers();
    for (auto peer : peers)
    {
        if (peer->isAIPeer())
            continue;
        if (peer->hasPlayerProfiles() && !peer->isWaitingForGame())
            cur_players += 1.0f;
    }

    std::string top_track = m_default_vote->m_track_name;
    unsigned top_laps = m_default_vote->m_num_laps;
    bool top_reverse = m_default_vote->m_reverse;

    std::map<std::string, unsigned> tracks;
    std::map<unsigned, unsigned> laps;
    std::map<bool, unsigned> reverses;

    // Ratio to determine majority agreement
    float tracks_rate = 0.0f;
    float laps_rate = 0.0f;
    float reverses_rate = 0.0f;

    for (auto& p : m_peers_votes)
    {
        auto track_vote = tracks.find(p.second.m_track_name);
        if (track_vote == tracks.end())
            tracks[p.second.m_track_name] = 1;
        else
            track_vote->second++;
        auto lap_vote = laps.find(p.second.m_num_laps);
        if (lap_vote == laps.end())
            laps[p.second.m_num_laps] = 1;
        else
            lap_vote->second++;
        auto reverse_vote = reverses.find(p.second.m_reverse);
        if (reverse_vote == reverses.end())
            reverses[p.second.m_reverse] = 1;
        else
            reverse_vote->second++;
    }

    findMajorityValue<std::string>(tracks, cur_players, &top_track, &tracks_rate);
    findMajorityValue<unsigned>(laps, cur_players, &top_laps, &laps_rate);
    findMajorityValue<bool>(reverses, cur_players, &top_reverse, &reverses_rate);

    // End early if there is majority agreement which is all entries rate > 0.5
    it = m_peers_votes.begin();
    if (tracks_rate > 0.5f && laps_rate > 0.5f && reverses_rate > 0.5f)
    {
        while (it != m_peers_votes.end())
        {
            if (it->second.m_track_name == top_track &&
                it->second.m_num_laps == top_laps &&
                it->second.m_reverse == top_reverse)
                break;
            else
                it++;
        }
        if (it == m_peers_votes.end())
        {
            // Don't end if no vote matches all majority choices
            Log::warn("ServerLobby",
                "Missing track %s from majority.", top_track.c_str());
            it = m_peers_votes.begin();
            if (!isVotingOver())
                return false;
        }
        *winner_peer_id = it->first;
        *winner_vote = it->second;
        return true;
    }
    if (isVotingOver())
    {
        // Pick the best lap (or soccer goal / time) from only the top track
        // if no majority agreement from all
        int diff = std::numeric_limits<int>::max();
        auto closest_lap = m_peers_votes.begin();
        while (it != m_peers_votes.end())
        {
            if (it->second.m_track_name == top_track &&
                std::abs((int)it->second.m_num_laps - (int)top_laps) < diff)
            {
                closest_lap = it;
                diff = std::abs((int)it->second.m_num_laps - (int)top_laps);
            }
            else
                it++;
        }
        *winner_peer_id = closest_lap->first;
        *winner_vote = closest_lap->second;
        return true;
    }
    return false;
}   // handleAllVotes

// ----------------------------------------------------------------------------
template<typename T>
void ServerLobby::findMajorityValue(const std::map<T, unsigned>& choices, unsigned cur_players,
                       T* best_choice, float* rate)
{
    RandomGenerator rg;
    unsigned max_votes = 0;
    auto best_iter = choices.begin();
    unsigned best_iters_count = 1;
    // Among choices with max votes, we need to pick one uniformly,
    // thus we have to keep track of their number
    for (auto iter = choices.begin(); iter != choices.end(); iter++)
    {
        if (iter->second > max_votes)
        {
            max_votes = iter->second;
            best_iter = iter;
            best_iters_count = 1;
        }
        else if (iter->second == max_votes)
        {
            best_iters_count++;
            if (rg.get(best_iters_count) == 0)
            {
                max_votes = iter->second;
                best_iter = iter;
            }
        }
    }
    if (best_iter != choices.end())
    {
        *best_choice = best_iter->first;
        *rate = float(best_iter->second) / cur_players;
    }
}   // findMajorityValue

// ----------------------------------------------------------------------------
void ServerLobby::getHitCaptureLimit()
{
    int hit_capture_limit = std::numeric_limits<int>::max();
    float time_limit = 0.0f;
    if (RaceManager::get()->getMinorMode() ==
        RaceManager::MINOR_MODE_CAPTURE_THE_FLAG)
    {
        if (ServerConfig::m_capture_limit > 0)
            hit_capture_limit = ServerConfig::m_capture_limit;
        if (ServerConfig::m_time_limit_ctf > 0)
            time_limit = (float)ServerConfig::m_time_limit_ctf;
    }
    else
    {
        if (ServerConfig::m_hit_limit > 0)
            hit_capture_limit = ServerConfig::m_hit_limit;
        if (ServerConfig::m_time_limit_ffa > 0.0f)
            time_limit = (float)ServerConfig::m_time_limit_ffa;
    }
    m_battle_hit_capture_limit = hit_capture_limit;
    m_battle_time_limit = time_limit;
}   // getHitCaptureLimit

// ----------------------------------------------------------------------------
/** Called from the RaceManager of the server when the world is loaded. Marks
 *  the server to be ready to start the race.
 */
void ServerLobby::finishedLoadingWorld()
{
    for (auto p : m_peers_ready)
    {
        if (auto peer = p.first.lock())
            peer->updateLastActivity();
    }
    m_server_has_loaded_world.store(true);
}   // finishedLoadingWorld;

//-----------------------------------------------------------------------------
/** Called when a client notifies the server that it has loaded the world.
 *  When all clients and the server are ready, the race can be started.
 */
void ServerLobby::finishedLoadingWorldClient(Event *event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    peer->updateLastActivity();
    m_peers_ready.at(peer) = true;
    Log::info("ServerLobby", "Peer %d has finished loading world at %lf",
        peer->getHostId(), StkTime::getRealTime());
}   // finishedLoadingWorldClient

//-----------------------------------------------------------------------------
/** Called when a client clicks on 'ok' on the race result screen.
 *  If all players have clicked on 'ok', go back to the lobby.
 */
void ServerLobby::playerFinishedResult(Event *event)
{
    if (m_rs_state.load() == RS_ASYNC_RESET ||
        m_state.load() != RESULT_DISPLAY)
        return;
    std::shared_ptr<STKPeer> peer = event->getPeerSP();
    m_peers_ready.at(peer) = true;
}   // playerFinishedResult

//-----------------------------------------------------------------------------
bool ServerLobby::waitingForPlayers() const
{
    if (m_game_setup->isGrandPrix() && m_game_setup->isGrandPrixStarted())
        return false;
    return m_state.load() >= WAITING_FOR_START_GAME;
}   // waitingForPlayers

//-----------------------------------------------------------------------------
void ServerLobby::handlePendingConnection()
{
    std::lock_guard<std::mutex> lock(m_keys_mutex);

    for (auto it = m_pending_connection.begin();
         it != m_pending_connection.end();)
    {
        auto peer = it->first.lock();
        if (!peer)
        {
            it = m_pending_connection.erase(it);
        }
        else
        {
            const uint32_t online_id = it->second.first;
            auto key = m_keys.find(online_id);
            if (key != m_keys.end() && key->second.m_tried == false)
            {
                try
                {
                    if (decryptConnectionRequest(peer, it->second.second,
                        key->second.m_aes_key, key->second.m_aes_iv, online_id,
                        key->second.m_name, key->second.m_country_code))
                    {
                        it = m_pending_connection.erase(it);
                        m_keys.erase(online_id);
                        continue;
                    }
                    else
                        key->second.m_tried = true;
                }
                catch (std::exception& e)
                {
                    Log::error("ServerLobby",
                        "handlePendingConnection error: %s", e.what());
                    key->second.m_tried = true;
                }
            }
            it++;
        }
    }
}   // handlePendingConnection

//-----------------------------------------------------------------------------
bool ServerLobby::decryptConnectionRequest(std::shared_ptr<STKPeer> peer,
    BareNetworkString& data, const std::string& key, const std::string& iv,
    uint32_t online_id, const core::stringw& online_name,
    const std::string& country_code)
{
    auto crypto = std::unique_ptr<Crypto>(new Crypto(
        Crypto::decode64(key), Crypto::decode64(iv)));
    if (crypto->decryptConnectionRequest(data))
    {
        peer->setCrypto(std::move(crypto));
        Log::info("ServerLobby", "%s validated",
            StringUtils::wideToUtf8(online_name).c_str());
        handleUnencryptedConnection(peer, data, online_id,
            online_name, true/*is_pending_connection*/, country_code);
        return true;
    }
    return false;
}   // decryptConnectionRequest

//-----------------------------------------------------------------------------
void ServerLobby::getRankingForPlayer(std::shared_ptr<NetworkPlayerProfile> p)
{
    int priority = Online::RequestManager::HTTP_MAX_PRIORITY;
    auto request = std::make_shared<Online::XMLRequest>(priority);
    NetworkConfig::get()->setUserDetails(request, "get-ranking");

    const uint32_t id = p->getOnlineId();
    request->addParameter("id", id);
    request->executeNow();

    const XMLNode* result = request->getXMLData();
    std::string rec_success;

    bool success = false;
    if (result->get("success", &rec_success))
        if (rec_success == "yes")
            success = true;

    if (!success)
    {
        Log::error("ServerLobby", "No ranking info found for player %s.",
            StringUtils::wideToUtf8(p->getName()).c_str());
        // Kick the player to avoid his score being reset in case
        // connection to stk addons is broken
        auto peer = p->getPeer();
        if (peer)
        {
            peer->kick();
            return;
        }
    }
    m_ranking->fill(id, result, p);
}   // getRankingForPlayer

//-----------------------------------------------------------------------------
void ServerLobby::submitRankingsToAddons()
{
    // No ranking for battle mode
    if (!RaceManager::get()->modeHasLaps())
        return;

    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        const uint32_t id = RaceManager::get()->getKartInfo(i).getOnlineId();
        const RankingEntry& scores = m_ranking->getScores(id);
        auto request = std::make_shared<SubmitRankingRequest>
            (scores, RaceManager::get()->getKartInfo(i).getCountryCode());
        NetworkConfig::get()->setUserDetails(request, "submit-ranking");
        Log::info("ServerLobby", "Submitting ranking for %s (%d) : %lf, %lf %d",
            StringUtils::wideToUtf8(
            RaceManager::get()->getKartInfo(i).getPlayerName()).c_str(), id,
            scores.score, scores.max_score, scores.races);
        request->queue();
    }
}   // submitRankingsToAddons

//-----------------------------------------------------------------------------
/** This function serves a purpose to send the message to all peers that are
 *  in game and/or spectating.
 */
void ServerLobby::broadcastMessageInGame(const irr::core::stringw& message)
{

    NetworkString* chat = getNetworkString();
    chat->setSynchronous(true);
    chat->addUInt8(LE_CHAT).encodeString16(message);

    // what is wrong here?
    STKHost::get()->sendPacketToAllPeersWith(
        [](STKPeer* peer) {
		// is player
	    return peer->hasPlayerProfiles() &&
	        // is in game or spectating
		(!peer->isWaitingForGame() || peer->isSpectator());
    }, chat);
    delete chat;
}

//-----------------------------------------------------------------------------
/** This function is called when all clients have loaded the world and
 *  are therefore ready to start the race. It determine the start time in
 *  network timer for client and server based on pings and then switches state
 *  to WAIT_FOR_RACE_STARTED.
 */
void ServerLobby::configPeersStartTime()
{
    std::ofstream logFile;
    if (ServerConfig::m_is_world_record_race)
    {
  	  logFile.open("race_log.txt", std::ios::trunc);
  	  logFile.close();
    }
    uint32_t max_ping = 0;
    const unsigned max_ping_from_peers = ServerConfig::m_max_ping;
    bool peer_exceeded_max_ping = false;
    for (auto p : m_peers_ready)
    {
        auto peer = p.first.lock();
        // Spectators don't send input so we don't need to delay for them
        if (!peer || peer->alwaysSpectate())
            continue;
        if (peer->getAveragePing() > max_ping_from_peers)
        {
            Log::warn("ServerLobby",
                "Peer %s cannot catch up with max ping %d.",
                peer->getAddress().toString().c_str(), max_ping);
            peer_exceeded_max_ping = true;
            continue;
        }
        max_ping = std::max(peer->getAveragePing(), max_ping);
    }
    if ((ServerConfig::m_high_ping_workaround && peer_exceeded_max_ping) ||
        (ServerConfig::m_live_players && RaceManager::get()->supportsLiveJoining()))
    {
        Log::info("ServerLobby", "Max ping to ServerConfig::m_max_ping for "
            "live joining or high ping workaround.");
        max_ping = ServerConfig::m_max_ping;
    }
    // Start up time will be after 2500ms, so even if this packet is sent late
    // (due to packet loss), the start time will still ahead of current time
    uint64_t start_time = STKHost::get()->getNetworkTimer() + (uint64_t)2500;
    powerup_manager->setRandomSeed(start_time);
    NetworkString* ns = getNetworkString(10);
    ns->setSynchronous(true);
    ns->addUInt8(LE_START_RACE).addUInt64(start_time);
    const uint8_t cc = (uint8_t)Track::getCurrentTrack()->getCheckManager()->getCheckStructureCount();
    ns->addUInt8(cc);
    *ns += *m_items_complete_state;
    m_client_starting_time = start_time;
    sendMessageToPeers(ns, /*reliable*/true);

    const unsigned jitter_tolerance = ServerConfig::m_jitter_tolerance;
    Log::info("ServerLobby", "Max ping from peers: %d, jitter tolerance: %d",
        max_ping, jitter_tolerance);
    // Delay server for max ping / 2 from peers and jitter tolerance.
    m_server_delay = (uint64_t)(max_ping / 2) + (uint64_t)jitter_tolerance;
    start_time += m_server_delay;
    m_server_started_at = start_time;
    delete ns;
    m_state = WAIT_FOR_RACE_STARTED;

    World::getWorld()->setPhase(WorldStatus::SERVER_READY_PHASE);
    // Different stk process thread may have different stk host
    STKHost* stk_host = STKHost::get();
    if (ServerConfig::m_is_world_record_race)
    {
	    std::string log_msg;
	    log_msg = "Track: " + std::string(RaceManager::get()->getTrackName()) + ", "
		    + "Reverse: " + (RaceManager::get()->getReverseTrack() ? "Yes" : "No") + ", "
		    + "Laps: " + std::to_string(RaceManager::get()->getNumLaps());
	    logFile.open("race_log.txt", std::ios::app);
	    if (logFile.is_open())
	    {
		    logFile << log_msg << "\n";
		    logFile.close();
		    if (!log_msg.empty())
		    {
			    Log::info("ServerLobby", "%s", log_msg.c_str());
		    }
	    }
	    try
	    {
		    std::string python_output = ServerLobby::execPythonScript();
		    python_output.erase(std::remove(python_output.begin(), python_output.end(), '\n'), python_output.end());
		    Log::info("ServerLobby", "%s", python_output.c_str());
		    if (python_output.length() > 2)
		    {
			    broadcastMessageInGame(StringUtils::utf8ToWide(python_output));
		    }
	    }
	    catch (const std::exception& e)
	    {
		    Log::error("ServerLobby", "Python script execution failed: %s", e.what());
	    }
    }
    if (m_replay_requested && ServerConfig::m_is_world_record_race)
    {
        std::string replay_path = ServerConfig::m_replay_dir;
        std::string replay_name = replay_path + "race_" + getTimeStamp() + ".replay";
        ReplayRecorder::get()->setFilename(replay_name);
        Log::info("ServerLobby", "Starting replay recording with filename: %s", replay_name.c_str());
    }
    // Minimap
    if (ServerConfig::m_soccer_roulette)
    {
	    SoccerRoulette::get()->giveNitroToAll();
    }
           	    
    joinStartGameThread();
    m_start_game_thread = std::thread([start_time, stk_host, this]()
        {
            const uint64_t cur_time = stk_host->getNetworkTimer();
            assert(start_time > cur_time);
            int sleep_time = (int)(start_time - cur_time);
            StkTime::sleep(sleep_time);
            m_state.store(RACING);
	    const std::string game_start_message = ServerConfig::m_game_start_message;

	    // Have Fun
	    if (!game_start_message.empty() && !ServerConfig::m_soccer_roulette)
	    {
		broadcastMessageInGame(
		    StringUtils::utf8ToWide(game_start_message));
	    }
	    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER)
	    {
	    	GoalHistory::clearHistory();
	    }
        });
}   // configPeersStartTime

//-----------------------------------------------------------------------------
bool ServerLobby::allowJoinedPlayersWaiting() const
{
    return !m_game_setup->isGrandPrix();
}   // allowJoinedPlayersWaiting

//-----------------------------------------------------------------------------
void ServerLobby::addWaitingPlayersToGame()
{
    auto all_profiles = STKHost::get()->getAllPlayerProfiles();
    for (auto& profile : all_profiles)
    {
        auto peer = profile->getPeer();
        if (!peer || !peer->isValidated())
            continue;

        peer->resetAlwaysSpectateFull();
        peer->setWaitingForGame(false);
        peer->setSpectator(false);
        if (m_peers_ready.find(peer) == m_peers_ready.end())
        {
            m_peers_ready[peer] = false;
            if (!ServerConfig::m_sql_management)
            {
                Log::info("ServerLobby",
                    "New player %s with online id %u from %s with %s.",
                    StringUtils::wideToUtf8(profile->getName()).c_str(),
                    profile->getOnlineId(),
                    peer->getAddress().toString().c_str(),
                    peer->getUserVersion().c_str());
            }
        }
        uint32_t online_id = profile->getOnlineId();
        if (ServerConfig::m_ranked && !m_ranking->has(online_id))
        {
            getRankingForPlayer(peer->getPlayerProfiles()[0]);
        }
    }
    // Re-activiate the ai
    if (auto ai = m_ai_peer.lock())
        ai->setValidated(true);
}   // addWaitingPlayersToGame

//-----------------------------------------------------------------------------
void ServerLobby::resetServer()
{
    addWaitingPlayersToGame();
    resetPeersReady();
    updatePlayerList(true/*update_when_reset_server*/);
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);

    sendMessageToPeersInServer(server_info);
    delete server_info;

    if (ServerConfig::m_enable_ril)
    {
        NetworkString* ril_pkt = getNetworkString();
        ril_pkt->setSynchronous(true);
        addRandomInstalladdonMessage(ril_pkt);

        sendMessageToPeersInServer(ril_pkt);
        delete ril_pkt;
    }
    setup();
    m_state = NetworkConfig::get()->isLAN() ?
        WAITING_FOR_START_GAME : REGISTER_SELF_ADDRESS;
    updatePlayerList();
    if (m_random_karts_enabled)
    {
	    assignRandomKarts();
    }
    if (ServerConfig::m_soccer_roulette)
    {
	    setPoleEnabled(true);
    }
}   // resetServer

//-----------------------------------------------------------------------------
void ServerLobby::testBannedForIP(STKPeer* peer) const
{
#ifdef ENABLE_SQLITE3
    if (!m_db->hasDatabase() || !m_db->hasIpBanTable())
        return;

    // Test for IPv4
    if (peer->getAddress().isIPv6())
        return;

    bool is_banned = false;
    uint32_t ip_start = 0;
    uint32_t ip_end = 0;

    std::vector<AbstractDatabase::IpBanTableData> ip_ban_list =
            m_db->getIpBanTableData(peer->getAddress().getIP());
    if (!ip_ban_list.empty())
    {
        is_banned = true;
        ip_start = ip_ban_list[0].ip_start;
        ip_end = ip_ban_list[0].ip_end;
        int row_id = ip_ban_list[0].row_id;
        std::string reason = ip_ban_list[0].reason;
        std::string description = ip_ban_list[0].description;
        Log::info("ServerLobby", "%s banned by IP: %s "
                "(rowid: %d, description: %s).",
                peer->getAddress().toString().c_str(), reason.c_str(), row_id, description.c_str());
        kickPlayerWithReason(peer, reason.c_str());
    }
    if (is_banned)
        m_db->increaseIpBanTriggerCount(ip_start, ip_end);
#endif
}   // testBannedForIP

//-----------------------------------------------------------------------------
void ServerLobby::testBannedForIPv6(STKPeer* peer) const
{
#ifdef ENABLE_SQLITE3
    if (!m_db->hasDatabase() || !m_db->hasIpv6BanTable())
        return;

    // Test for IPv6
    if (!peer->getAddress().isIPv6())
        return;

    bool is_banned = false;
    std::string ipv6_cidr = "";

    std::vector<AbstractDatabase::Ipv6BanTableData> ipv6_ban_list =
            m_db->getIpv6BanTableData(peer->getAddress().toString(false));

    if (!ipv6_ban_list.empty())
    {
        is_banned = true;
        ipv6_cidr = ipv6_ban_list[0].ipv6_cidr;
        int row_id = ipv6_ban_list[0].row_id;
        std::string reason = ipv6_ban_list[0].reason;
        std::string description = ipv6_ban_list[0].description;
        Log::info("ServerLobby", "%s banned by IPv6: %s "
                "(rowid: %d, description: %s).",
                peer->getAddress().toString(false).c_str(), reason.c_str(), row_id, description.c_str());
        kickPlayerWithReason(peer, reason.c_str());
    }
    if (is_banned)
        m_db->increaseIpv6BanTriggerCount(ipv6_cidr);
#endif
}   // testBannedForIPv6

//-----------------------------------------------------------------------------
void ServerLobby::testBannedForOnlineId(STKPeer* peer,
                                        uint32_t online_id) const
{
#ifdef ENABLE_SQLITE3
    if (!m_db->hasDatabase() || !m_db->hasOnlineIdBanTable())
        return;

    bool is_banned = false;
    std::vector<AbstractDatabase::OnlineIdBanTableData> online_id_ban_list =
            m_db->getOnlineIdBanTableData(online_id);

    if (!online_id_ban_list.empty())
    {
        is_banned = true;
        int row_id = online_id_ban_list[0].row_id;
        std::string reason = online_id_ban_list[0].reason;
        std::string description = online_id_ban_list[0].description;
        Log::info("ServerLobby", "%s banned by online id: %s "
                "(online id: %u, rowid: %d, description: %s).",
                peer->getAddress().toString().c_str(), reason.c_str(), online_id, row_id, description.c_str());
        kickPlayerWithReason(peer, reason.c_str());
    }
    if (is_banned)
        m_db->increaseOnlineIdBanTriggerCount(online_id);
#endif
}   // testBannedForOnlineId

//-----------------------------------------------------------------------------
void ServerLobby::listBanTable(std::stringstream& out)
{
#ifdef ENABLE_SQLITE3
    m_db->listBanTable(out);
#endif
}   // listBanTable

//-----------------------------------------------------------------------------
float ServerLobby::getStartupBoostOrPenaltyForKart(uint32_t ping,
                                                   unsigned kart_id)
{
    AbstractKart* k = World::getWorld()->getKart(kart_id);
    if (k->getStartupBoost() != 0.0f)
        return k->getStartupBoost();
    uint64_t now = STKHost::get()->getNetworkTimer();
    uint64_t client_time = now - ping / 2;
    uint64_t server_time = client_time + m_server_delay;
    int ticks = stk_config->time2Ticks(
        (float)(server_time - m_server_started_at) / 1000.0f);
    if (ticks < stk_config->time2Ticks(1.0f))
    {
        PlayerController* pc =
            dynamic_cast<PlayerController*>(k->getController());
        pc->displayPenaltyWarning();
        return -1.0f;
    }
    float f = k->getStartupBoostFromStartTicks(ticks);
    k->setStartupBoost(f);
    return f;
}   // getStartupBoostOrPenaltyForKart

//-----------------------------------------------------------------------------
/*! \brief Called when the server owner request to change game mode or
 *         difficulty.
 *  \param event : Event providing the information.
 *
 *  Format of the data :
 *  Byte 0            1            2
 *       -----------------------------------------------
 *  Size |     1      |     1     |         1          |
 *  Data | difficulty | game mode | soccer goal target |
 *       -----------------------------------------------
 */
void ServerLobby::handleServerConfiguration(Event* event)
{
    if (m_state != WAITING_FOR_START_GAME)
    {
        Log::warn("ServerLobby",
            "Received handleServerConfiguration while being in state %d.",
            m_state.load());
        return;
    }
    if (!ServerConfig::m_server_configurable)
    {
        Log::warn("ServerLobby", "server-configurable is not enabled.");
        return;
    }
    if (event->getPeerSP() != m_server_owner.lock())
    {
        Log::warn("ServerLobby",
            "Client %d is not authorised to config server.",
            event->getPeer()->getHostId());
        return;
    }
    NetworkString& data = event->data();
    int new_difficulty = data.getUInt8();
    int new_game_mode = data.getUInt8();
    bool new_soccer_goal_target = data.getUInt8() == 1;
    auto modes = ServerConfig::getLocalGameMode(new_game_mode);
    if (modes.second == RaceManager::MAJOR_MODE_GRAND_PRIX)
    {
        Log::warn("ServerLobby", "Grand prix is used for new mode.");
        return;
    }
    updateServerConfiguration(new_difficulty, new_game_mode,
            new_soccer_goal_target ? 1 : 0);

}   // handleServerConfiguration
//-----------------------------------------------------------------------------
void ServerLobby::updateServerConfiguration(int new_difficulty,
        int new_game_mode,
        std::int8_t new_soccer_goal_target)
{
    if (new_difficulty == -1)
        new_difficulty = m_difficulty.load();
    if (new_game_mode == -1)
        new_game_mode = m_game_mode.load();
    if (new_soccer_goal_target == -1)
        new_soccer_goal_target = ServerConfig::m_soccer_goal_target ? 1 : 0;

    auto modes = ServerConfig::getLocalGameMode(new_game_mode);
    RaceManager::get()->setMinorMode(modes.first);
    RaceManager::get()->setMajorMode(modes.second);
    RaceManager::get()->setDifficulty(RaceManager::Difficulty(new_difficulty));
    m_game_setup->resetExtraServerInfo();
    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER)
        m_game_setup->setSoccerGoalTarget(new_soccer_goal_target > 0 ? true : false);

    if (NetworkConfig::get()->isWAN() &&
        (m_difficulty.load() != new_difficulty ||
        m_game_mode.load() != new_game_mode))
    {
        Log::info("ServerLobby", "Updating server info with new "
            "difficulty: %d, game mode: %d to stk-addons.", new_difficulty,
            new_game_mode);
        int priority = Online::RequestManager::HTTP_MAX_PRIORITY;
        auto request = std::make_shared<Online::XMLRequest>(priority);
        NetworkConfig::get()->setServerDetails(request, "update-config");
        const SocketAddress& addr = STKHost::get()->getPublicAddress();
        request->addParameter("address", addr.getIP());
        request->addParameter("port", addr.getPort());
        request->addParameter("new-difficulty", new_difficulty);
        request->addParameter("new-game-mode", new_game_mode);
        request->queue();
    }
    m_difficulty.store(new_difficulty);
    m_game_mode.store(new_game_mode);
    updateTracksForMode();

    auto peers = STKHost::get()->getPeers();
    for (auto& peer : peers)
    {
        auto assets = peer->getClientAssets();
        if (!peer->isValidated() || assets.second.empty())
            continue;
        std::set<std::string> tracks_erase;
        for (const std::string& server_track : m_available_kts.second)
        {
            if (assets.second.find(server_track) == assets.second.end())
            {
                tracks_erase.insert(server_track);
            }
        }
        if (tracks_erase.size() == m_available_kts.second.size())
        {
            NetworkString *message = getNetworkString(2);
            message->setSynchronous(true);
            message->addUInt8(LE_CONNECTION_REFUSED)
                .addUInt8(RR_INCOMPATIBLE_DATA);
            peer->cleanPlayerProfiles();
            peer->sendPacket(message, true/*reliable*/);
            peer->reset();
            delete message;
            Log::verbose("ServerLobby",
                "Player has incompatible tracks for new game mode.");
        }
    }
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    sendMessageToPeers(server_info);
    delete server_info;
    if (ServerConfig::m_enable_ril)
    {
        NetworkString* ril_pkt = getNetworkString();
        ril_pkt->setSynchronous(true);
        addRandomInstalladdonMessage(ril_pkt);

        sendMessageToPeers(ril_pkt);
        delete ril_pkt;
    }
    updatePlayerList();

}   // updateServerConfiguration

//-----------------------------------------------------------------------------
/*! \brief Called when a player want to change his handicap
 *  \param event : Event providing the information.
 *
 *  Format of the data :
 *  Byte 0                 1
 *       ----------------------------------
 *  Size |       1         |       1      |
 *  Data | local player id | new handicap |
 *       ----------------------------------
 */
void ServerLobby::changeHandicap(Event* event)
{
    NetworkString& data = event->data();
    if (m_state.load() != WAITING_FOR_START_GAME &&
        !event->getPeer()->isWaitingForGame())
    {
        Log::warn("ServerLobby", "Set handicap at wrong time.");
        return;
    }
    uint8_t local_id = data.getUInt8();
    auto& player = event->getPeer()->getPlayerProfiles().at(local_id);

    // Check for restrictions
    if (event->getPeer()->hasRestriction(PRF_HANDICAP))
    {
        Log::info("ServerLobby",
                "Player %s tried to change the handicap without permission.",
                StringUtils::wideToUtf8(player->getName()).c_str());

        NetworkString* const response = getNetworkString();
        response->setSynchronous(true);
        response->addUInt8(LE_CHAT).encodeString16(
                L"You are not allowed to change the handicap.");
        event->getPeer()->sendPacket(response, true/*reliable*/);
        delete response;
        return;
    }

    uint8_t handicap_id = data.getUInt8();
    if (handicap_id >= HANDICAP_COUNT)
    {
        Log::warn("ServerLobby", "Wrong handicap %d.", handicap_id);
        return;
    }
    HandicapLevel h = (HandicapLevel)handicap_id;
    player->setHandicap(h);
    updatePlayerList();
}   // changeHandicap
//-----------------------------------------------------------------------------
void ServerLobby::forceChangeHandicap(NetworkPlayerProfile* const player,
        const HandicapLevel status)
{
    player->setHandicap(status);
    updatePlayerList();
}   // forceChangeHandicap

//-----------------------------------------------------------------------------
/** Update and see if any player disconnects, if so eliminate the kart in
 *  world, so this function must be called in main thread.
 */
void ServerLobby::handlePlayerDisconnection() const
{
    if (!World::getWorld() ||
        World::getWorld()->getPhase() < WorldStatus::MUSIC_PHASE)
    {
        return;
    }

    int red_count = 0;
    int blue_count = 0;
    unsigned total = 0;
    for (unsigned i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        RemoteKartInfo& rki = RaceManager::get()->getKartInfo(i);
        if (rki.isReserved())
            continue;
        bool disconnected = rki.disconnected();
        if (RaceManager::get()->getKartInfo(i).getKartTeam() == KART_TEAM_RED &&
            !disconnected)
            red_count++;
        else if (RaceManager::get()->getKartInfo(i).getKartTeam() ==
            KART_TEAM_BLUE && !disconnected)
            blue_count++;

        if (!disconnected)
        {
            total++;
            continue;
        }
        else
            rki.makeReserved();

        AbstractKart* k = World::getWorld()->getKart(i);
        if (!k->isEliminated() && !k->hasFinishedRace())
        {
            CaptureTheFlag* ctf = dynamic_cast<CaptureTheFlag*>
                (World::getWorld());
            if (ctf)
                ctf->loseFlagForKart(k->getWorldKartId());

            World::getWorld()->eliminateKart(i,
                false/*notify_of_elimination*/);
            if (ServerConfig::m_ranked)
            {
                // Handle disconnection earlier to prevent cheating by joining
                // another ranked server
                // Real score will be submitted later in computeNewRankings
                const uint32_t id =
                    RaceManager::get()->getKartInfo(i).getOnlineId();
                RankingEntry penalized = m_ranking->getTemporaryPenalizedScores(id);
                auto request = std::make_shared<SubmitRankingRequest>
                    (penalized,
                    RaceManager::get()->getKartInfo(i).getCountryCode());
                NetworkConfig::get()->setUserDetails(request,
                    "submit-ranking");
                request->queue();
            }
            k->setPosition(
                World::getWorld()->getCurrentNumKarts() + 1);
            k->finishedRace(World::getWorld()->getTime(), true/*from_server*/);
        }
    }

    // If live players is enabled, don't end the game if unfair team
    if (!ServerConfig::m_live_players &&
        total != 1 && World::getWorld()->hasTeam() &&
        (red_count == 0 || blue_count == 0))
        World::getWorld()->setUnfairTeam(true);

}   // handlePlayerDisconnection

//-----------------------------------------------------------------------------
/** Add reserved players for live join later if required.
 */
void ServerLobby::addLiveJoinPlaceholder(
    std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
    unsigned int push_front_blue,
    unsigned int push_front_red) const
{
    assert(push_front_blue <= 7);
    assert(push_front_red <= 7);
    if (!ServerConfig::m_live_players || !RaceManager::get()->supportsLiveJoining())
        return;
    if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_FREE_FOR_ALL)
    {
        Track* t = track_manager->getTrack(m_game_setup->getCurrentTrack());
        assert(t);
        int max_players = std::min((int)ServerConfig::m_server_max_players,
            (int)t->getMaxArenaPlayers());
        int add_size = max_players - (int)players.size();
        assert(add_size >= 0);
        for (int i = 0; i < add_size; i++)
        {
            players.push_back(
                NetworkPlayerProfile::getReservedProfile(KART_TEAM_NONE));
        }
    }
    else
    {
        // CTF or soccer, reserve at most 7 players on each team
        int red_count = (int)push_front_red;
        int blue_count = (int)push_front_blue;
        for (unsigned i = 0; i < players.size(); i++)
        {
            if (players[i]->getTeam() == KART_TEAM_RED)
                red_count++;
            else
                blue_count++;
        }
        red_count = red_count >= 7 ? 0 : 7 - red_count;
        blue_count = blue_count >= 7 ? 0 : 7 - blue_count;
        for (unsigned int i = 0; i < push_front_red; i++)
        {
            players.insert(players.begin(),
                NetworkPlayerProfile::getReservedProfile(KART_TEAM_RED));
        }
        for (unsigned int i = 0; i < push_front_blue; i++)
        {
            players.insert(players.begin(),
                NetworkPlayerProfile::getReservedProfile(KART_TEAM_BLUE));
        }
        for (int i = 0; i < red_count; i++)
        {
            players.push_back(
                NetworkPlayerProfile::getReservedProfile(KART_TEAM_RED));
        }
        for (int i = 0; i < blue_count; i++)
        {
            players.push_back(
                NetworkPlayerProfile::getReservedProfile(KART_TEAM_BLUE));
        }
    }
}   // addLiveJoinPlaceholder

//-----------------------------------------------------------------------------
void ServerLobby::setPlayerKarts(const NetworkString& ns, STKPeer* peer) const
{
    Log::verbose("ServerLobby", "ServerLobby::setPlayerKarts()");
    unsigned player_count = ns.getUInt8();
    for (unsigned i = 0; i < player_count; i++)
    {
        std::string kart;
        std::shared_ptr<NetworkPlayerProfile> player =
            peer->getPlayerProfiles()[i];
        bool forcedRandom = false;

        ns.decodeString(&kart);
        const bool isStandardKart = kart.find("addon_") == std::string::npos;

        if (!player->getForcedKart().empty()
                && kart != player->getForcedKart())
        {
            Log::verbose("ServerLobby",
                         "Player %s chose the kart %s that doesn't comply with the requirement.",
                    StringUtils::wideToUtf8(player->getName()).c_str(),
                    kart.c_str());
            auto chat = getNetworkString();
            chat->setSynchronous(true);
            chat->addUInt8(LE_CHAT);
            chat->encodeString16(irr::core::stringw(L"You may not use this kart for this game. Please choose a different one."));
            peer->sendPacket(chat, true/*reliable*/);
            delete chat;
            forcedRandom = true;
        }
        if (m_kart_restriction && !isStandardKart)
        {
            auto kt = m_addon_kts.first.find(kart);
            if (kt == m_addon_kts.first.cend())
            {
                Log::verbose("ServerLobby",
                             "Player %s chose the addon kart %s that is not installed on the server.",
                        StringUtils::wideToUtf8(player->getName()).c_str(),
                        kart.c_str());
                auto chat = getNetworkString();
                chat->setSynchronous(true);
                chat->addUInt8(LE_CHAT);
                chat->encodeString16(irr::core::stringw(L"You may not use this kart for this game. Please choose a different one."));
                peer->sendPacket(chat, true/*reliable*/);
                delete chat;
                forcedRandom = true;
            }
            else {
                const std::string ktr(kartRestrictedTypeName(m_kart_restriction));
                const std::string ktc = kart_properties_manager->getKart(*kt)->getKartType();

                if (kt != m_addon_kts.first.cend() && 
                        ktc != ktr)
                {
                    Log::verbose("ServerLobby",
                                 "Player %s chose the addon kart %s that is of the type %s, not %s.",
                            StringUtils::wideToUtf8(player->getName()).c_str(),
                            kart.c_str(), ktc.c_str(), ktr.c_str());
                    auto chat = getNetworkString();
                    chat->setSynchronous(true);
                    chat->addUInt8(LE_CHAT);
                    chat->encodeString16(irr::core::stringw(L"You may not use this kart for this game. Please choose a different one."));
                    peer->sendPacket(chat, true/*reliable*/);
                    delete chat;
                    forcedRandom = true;
                }
            }   
        }

        // decide if the kart is chosen incorrectly or randomly
        //
        if (kart.find("randomkart") != std::string::npos || forcedRandom ||
                    (isStandardKart && (
                    m_available_kts.first.find(kart) == m_available_kts.first.end())))
        {
                RandomGenerator rg;
                std::set<std::string>::iterator it =
                    m_available_kts.first.begin();
                std::advance(it,
                    rg.get((int)m_available_kts.first.size()));
                player->setKartName(*it);
        }
        else
        {
            player->setKartName(kart);
        }
    }
    if (peer->getClientCapabilities().find("real_addon_karts") ==
        peer->getClientCapabilities().end() || ns.size() == 0)
        return;
    for (unsigned i = 0; i < player_count; i++)
    {
        KartData kart_data(ns);
        std::string type = kart_data.m_kart_type;
        auto& player = peer->getPlayerProfiles()[i];
        if (!player->getForcedKart().empty())
            player->setKartName(player->getForcedKart());

        const std::string& kart_id = player->getKartName();
        if (NetworkConfig::get()->useTuxHitboxAddon() &&
            StringUtils::startsWith(kart_id, "addon_") &&
            kart_properties_manager->hasKartTypeCharacteristic(type))
        {
            const KartProperties* real_addon =
                kart_properties_manager->getKart(kart_id);
            if (ServerConfig::m_real_addon_karts && real_addon)
            {
                kart_data = KartData(real_addon);
            }
            else
            {
                const KartProperties* tux_kp =
                    kart_properties_manager->getKart("tux");
                kart_data = KartData(tux_kp);
                kart_data.m_kart_type = type;
            }
            player->setKartData(kart_data);
        }
    }
}   // setPlayerKarts

//-----------------------------------------------------------------------------
/** Tell the client \ref RemoteKartInfo of a player when some player joining
 *  live.
 */
void ServerLobby::handleKartInfo(Event* event)
{
    World* w = World::getWorld();
    if (!w)
        return;

    STKPeer* peer = event->getPeer();
    const NetworkString& data = event->data();
    uint8_t kart_id = data.getUInt8();
    if (kart_id > RaceManager::get()->getNumPlayers())
        return;

    AbstractKart* k = w->getKart(kart_id);
    int live_join_util_ticks = k->getLiveJoinUntilTicks();

    const RemoteKartInfo& rki = RaceManager::get()->getKartInfo(kart_id);

    NetworkString* ns = getNetworkString(1);
    ns->setSynchronous(true);
    ns->addUInt8(LE_KART_INFO).addUInt32(live_join_util_ticks)
        .addUInt8(kart_id) .encodeString(rki.getPlayerName())
        .addUInt32(rki.getHostId()).addFloat(rki.getDefaultKartColor())
        .addUInt32(rki.getOnlineId()).addUInt8(rki.getHandicap())
        .addUInt8((uint8_t)rki.getLocalPlayerId())
        .encodeString(rki.getKartName()).encodeString(rki.getCountryCode());
    if (peer->getClientCapabilities().find("real_addon_karts") !=
        peer->getClientCapabilities().end())
        rki.getKartData().encode(ns);
    peer->sendPacket(ns, true/*reliable*/);
    delete ns;
}   // handleKartInfo

//-----------------------------------------------------------------------------
/** Client if currently in-game (including spectator) wants to go back to
 *  lobby.
 */
void ServerLobby::clientInGameWantsToBackLobby(Event* event)
{
    World* w = World::getWorld();
    std::shared_ptr<STKPeer> peer = event->getPeerSP();

    if (!w || !worldIsActive() || peer->isWaitingForGame())
    {
        Log::warn("ServerLobby", "%s try to leave the game at wrong time.",
            peer->getAddress().toString().c_str());
        return;
    }

    if (m_process_type == PT_CHILD &&
        event->getPeer()->getHostId() == m_client_server_host_id.load())
    {
        // For child server the remaining client cannot go on player when the
        // server owner quited the game (because the world will be deleted), so
        // we reset all players
        auto pm = ProtocolManager::lock();
        if (RaceEventManager::get())
        {
            RaceEventManager::get()->stop();
            pm->findAndTerminate(PROTOCOL_GAME_EVENTS);
        }
        auto gp = GameProtocol::lock();
        if (gp)
        {
            auto lock = gp->acquireWorldDeletingMutex();
            pm->findAndTerminate(PROTOCOL_CONTROLLER_EVENTS);
            exitGameState();
        }
        else
            exitGameState();
        NetworkString* back_to_lobby = getNetworkString(2);
        back_to_lobby->setSynchronous(true);
        back_to_lobby->addUInt8(LE_BACK_LOBBY)
            .addUInt8(BLR_SERVER_ONWER_QUITED_THE_GAME);
        sendMessageToPeersInServer(back_to_lobby, /*reliable*/true);
        delete back_to_lobby;
        m_rs_state.store(RS_ASYNC_RESET);
        return;
    }

    for (const int id : peer->getAvailableKartIDs())
    {
        RemoteKartInfo& rki = RaceManager::get()->getKartInfo(id);
        if (rki.getHostId() == peer->getHostId())
        {
            Log::info("ServerLobby", "%s left the game with kart id %d.",
                peer->getAddress().toString().c_str(), id);
            rki.setNetworkPlayerProfile(
                std::shared_ptr<NetworkPlayerProfile>());
        }
        else
        {
            Log::error("ServerLobby", "%s doesn't exist anymore in server.",
                peer->getAddress().toString().c_str());
        }
    }
    NetworkItemManager* nim = dynamic_cast<NetworkItemManager*>
        (Track::getCurrentTrack()->getItemManager());
    assert(nim);
    nim->erasePeerInGame(peer);
    m_peers_ready.erase(peer);
    peer->setWaitingForGame(true);
    peer->setSpectator(false);

    NetworkString* reset = getNetworkString(2);
    reset->setSynchronous(true);
    reset->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
    peer->sendPacket(reset, /*reliable*/true);
    delete reset;
    updatePlayerList();
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    peer->sendPacket(server_info, /*reliable*/true);
    delete server_info;
    sendRandomInstalladdonLine(peer);
    sendCurrentModifiers(peer);
}   // clientInGameWantsToBackLobby

//-----------------------------------------------------------------------------
/** Client if currently select assets wants to go back to lobby.
 */
void ServerLobby::clientSelectingAssetsWantsToBackLobby(Event* event)
{
    std::shared_ptr<STKPeer> peer = event->getPeerSP();

    if (m_state.load() != SELECTING || peer->isWaitingForGame())
    {
        Log::warn("ServerLobby",
            "%s try to leave selecting assets at wrong time.",
            peer->getAddress().toString().c_str());
        return;
    }

    if (m_process_type == PT_CHILD &&
        event->getPeer()->getHostId() == m_client_server_host_id.load())
    {
        NetworkString* back_to_lobby = getNetworkString(2);
        back_to_lobby->setSynchronous(true);
        back_to_lobby->addUInt8(LE_BACK_LOBBY)
            .addUInt8(BLR_SERVER_ONWER_QUITED_THE_GAME);
        sendMessageToPeersInServer(back_to_lobby, /*reliable*/true);
        delete back_to_lobby;
        resetVotingTime();
        resetServer();
        m_rs_state.store(RS_NONE);
        return;
    }

    m_peers_ready.erase(peer);
    peer->setWaitingForGame(true);
    peer->setSpectator(false);

    NetworkString* reset = getNetworkString(2);
    reset->setSynchronous(true);
    reset->addUInt8(LE_BACK_LOBBY).addUInt8(BLR_NONE);
    peer->sendPacket(reset, /*reliable*/true);
    delete reset;
    updatePlayerList();
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    m_game_setup->addServerInfo(server_info);
    peer->sendPacket(server_info, /*reliable*/true);
    delete server_info;
    sendRandomInstalladdonLine(peer);
    sendCurrentModifiers(peer);
}   // clientSelectingAssetsWantsToBackLobby

//-----------------------------------------------------------------------------
void ServerLobby::saveInitialItems(std::shared_ptr<NetworkItemManager> nim)
{
    m_items_complete_state->getBuffer().clear();
    m_items_complete_state->reset();
    nim->saveCompleteState(m_items_complete_state);
}   // saveInitialItems

//-----------------------------------------------------------------------------
bool ServerLobby::supportsAI()
{
    return getGameMode() == 3 || getGameMode() == 4;
}   // supportsAI

//-----------------------------------------------------------------------------
bool ServerLobby::checkPeersCanPlay(bool ignore_ai_peer) const
{
// frustrating indentation, in this vim setting
    const bool is_team_game = (
        RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER ||
        RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_CAPTURE_THE_FLAG
    );
    for (auto p : m_peers_ready)
    {
        auto peer = p.first.lock();
	KartTeam team;
        if (!peer)
            continue;
	if (!peer->hasPlayerProfiles())
	    continue;
	team = peer->getPlayerProfiles()[0]->getTeam();
        if (ignore_ai_peer && peer->isAIPeer())
            continue;
	// player won't play the game without teams
	if (is_team_game && (team == KART_TEAM_NONE))
	    continue;
	if (!peer->alwaysSpectate())
	    return true;
    }
    return false;
}   // checkPeersCanPlay

//-----------------------------------------------------------------------------
bool ServerLobby::checkPeersReady(bool ignore_ai_peer) const
{
    bool all_ready = true;
    for (auto p : m_peers_ready)
    {
        auto peer = p.first.lock();
        if (!peer)
            continue;
        if (ignore_ai_peer && peer->isAIPeer())
            continue;
	if (peer->alwaysSpectate())
            continue;
        all_ready = all_ready && p.second;
        if (!all_ready)
            return false;
    }
    return true;
}   // checkPeersReady

// Optimization: you can go through the loop once, without the second time
//-----------------------------------------------------------------------------
char ServerLobby::checkPeersCanPlayAndReady(bool ignore_ai_peer) const
{
    const bool is_team_game = (
        RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER ||
        RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_CAPTURE_THE_FLAG
    );
    // 0x1: all the players are ready, 0x2: there is at least one player that can play,
    // by default all players are ready
    char all_ready_play = !m_peers_ready.empty();
    for (auto p : m_peers_ready)
    {
        auto peer = p.first.lock();
        KartTeam team;
        if (!peer)
            continue;
        if (!peer->hasPlayerProfiles())
            continue;
        team = peer->getPlayerProfiles()[0]->getTeam();
        if (ignore_ai_peer && peer->isAIPeer())
            continue;
        if (!peer->isEligibleForGame())
            continue;
        else if(!is_team_game || is_team_game == (team != KART_TEAM_NONE))
            all_ready_play |= 2;

        if (all_ready_play&1 && !p.second)
            all_ready_play &= ~1;
    }
    return all_ready_play;
}   // checkPeersCanPlayAndReady


//-----------------------------------------------------------------------------
void ServerLobby::handleServerCommand(Event* event,
                                      std::shared_ptr<STKPeer> peer)
{
    NetworkString& data = event->data();
    std::string language;
    data.decodeString(&language);
    std::string cmd;
    data.decodeString(&cmd);
    auto argv = StringUtils::split(cmd, ' ');
    if (argv.size() == 0)
        return;

    ServerLobbyCommands::get()->handleServerCommand(this, peer, cmd);
    return;
}   // handleServerCommand

//-----------------------------------------------------------------------------
bool ServerLobby::serverAndPeerHaveTrack(std::shared_ptr<STKPeer>& peer, std::string track_id) const
{
    return serverAndPeerHaveTrack(peer.get(), track_id);
} // serverAndPeerHaveTrack
//-----------------------------------------------------------------------------
bool ServerLobby::serverAndPeerHaveTrack(STKPeer* peer, std::string track_id) const
{
    std::pair<std::set<std::string>, std::set<std::string>> kt = peer->getClientAssets();
    bool peerHasTrack = kt.second.find(track_id) != kt.second.end();
    bool serverHasTrack = (m_official_kts.second.find(track_id) != m_official_kts.second.end()) ||
        (m_addon_kts.second.find(track_id) != m_addon_kts.second.end()) ||
        (m_addon_soccers.find(track_id) != m_addon_soccers.end()) ||
        (m_addon_arenas.find(track_id) != m_addon_arenas.end());
    return peerHasTrack && serverHasTrack;
} // serverAndPeerHaveTrack
//-----------------------------------------------------------------------------
bool ServerLobby::peerHasForcedTrack(std::shared_ptr<STKPeer>& peer) const
{
    return peerHasForcedTrack(peer.get());
}   // peerHasForcedTrack
//-----------------------------------------------------------------------------
bool ServerLobby::peerHasForcedTrack(STKPeer* const peer) const
{
    if (!peer) return false;
    const auto& kt = peer->getClientAssets();

    // Players who do not have the addon defined via /setfield are not allowed to play.
    if (!m_set_field.empty())
        return kt.second.find(m_set_field) != kt.second.cend();

    // nothing is forced otherwise
    return true;
}   // peerHasForcedTrack

//-----------------------------------------------------------------------------
void ServerLobby::sendStringToPeer(const std::string& s, std::shared_ptr<STKPeer>& peer) const
{
    if (peer==NULL)
    {
        Log::info("ServerLobby",s.c_str());
        return;
    }
    NetworkString* chat = getNetworkString();
    chat->addUInt8(LE_CHAT);
    chat->setSynchronous(true);
    chat->encodeString16(StringUtils::utf8ToWide(s));
    peer->sendPacket(chat, true/*reliable*/);
    delete chat;
}   // sendStringToPeer
//-----------------------------------------------------------------------------
void ServerLobby::sendStringToPeer(const irr::core::stringw& s, std::shared_ptr<STKPeer>& peer) const
{
    if (peer==NULL)
    {
        Log::info("ServerLobby",StringUtils::wideToUtf8(s).c_str());
        return;
    }
    NetworkString* chat = getNetworkString();
    chat->addUInt8(LE_CHAT);
    chat->setSynchronous(true);
    chat->encodeString16(s);
    peer->sendPacket(chat, true/*reliable*/);
    delete chat;
}   // sendStringToPeer
//-----------------------------------------------------------------------------
void ServerLobby::sendStringToPeer(const std::string& s, STKPeer* const peer) const
{
    if (peer==NULL)
    {
        Log::info("ServerLobby",s.c_str());
        return;
    }
    NetworkString* chat = getNetworkString();
    chat->addUInt8(LE_CHAT);
    chat->setSynchronous(true);
    chat->encodeString16(StringUtils::utf8ToWide(s));
    peer->sendPacket(chat, true/*reliable*/);
    delete chat;
}   // sendStringToPeer
//-----------------------------------------------------------------------------
void ServerLobby::sendStringToPeer(const irr::core::stringw& s, STKPeer* const peer) const
{
    if (peer==NULL)
    {
        Log::info("ServerLobby",StringUtils::wideToUtf8(s).c_str());
        return;
    }
    NetworkString* chat = getNetworkString();
    chat->addUInt8(LE_CHAT);
    chat->setSynchronous(true);
    chat->encodeString16(s);
    peer->sendPacket(chat, true/*reliable*/);
    delete chat;
}   // sendStringToPeer

//-----------------------------------------------------------------------------
void ServerLobby::sendStringToAllPeers(const std::string s)
{
    Log::info("ServerLobby",s.c_str());
    NetworkString* chat = getNetworkString();
    chat->addUInt8(LE_CHAT);
    chat->setSynchronous(true);
    chat->encodeString16(StringUtils::utf8ToWide(s));
    sendMessageToPeers(chat, true/*reliable*/);
    delete chat;
}   // sendStringToAllPeers
//-----------------------------------------------------------------------------
// Specify default argument to determine current mode from server config
const std::string ServerLobby::getRandomAddon(RaceManager::MinorRaceModeType m) const
{
    const std::set<std::string>* addon_list;

    m = m != RaceManager::MINOR_MODE_NONE ? m : RaceManager::get()->getMinorMode();
    switch (m)
    {
        case RaceManager::MINOR_MODE_NORMAL_RACE:
        case RaceManager::MINOR_MODE_TIME_TRIAL:
        case RaceManager::MINOR_MODE_FOLLOW_LEADER:
	    addon_list = &m_addon_kts.second;
            break;
        case RaceManager::MINOR_MODE_FREE_FOR_ALL:
        case RaceManager::MINOR_MODE_CAPTURE_THE_FLAG:
	    addon_list = &m_addon_arenas;
            break;
        case RaceManager::MINOR_MODE_SOCCER:
	    addon_list = &m_addon_soccers;
            break;
        default:
            assert(false);
	    return "";
            break;
    }

    if (addon_list->empty())
    {
        return "(no-addons-installed)";
    }
    
    RandomGenerator rg;
    std::set<std::string>::const_iterator it = addon_list->cbegin();
    std::advance(it, rg.get((int)addon_list->size()));
    std::string result = *it; 
    // remove "addon_" prefix
    result.erase(0, 6);

    return result;
} // getRandomSoccerAddon

//------------------------------------------------------------------------------------------
core::stringw ServerLobby::formatTeammateList(
    const std::vector<std::shared_ptr<NetworkPlayerProfile>> &team) const
{
    core::stringw res;
    for (unsigned int i = 0; i < team.size(); ++i)
    {
        res += L"/";
        res += core::stringw(i + 1);
        res += L" to vote for ";
        res += team[i]->getName();
        res += L"\n";
    }
    return res;
} // formatTeammateList

//------------------------------------------------------------------------------------------
void ServerLobby::setPoleEnabled(bool mode)
{
    m_blue_pole_votes.clear();
    m_red_pole_votes.clear();

    if (m_pole_enabled == mode)
        return;
    m_pole_enabled = mode;

    STKHost* const host = STKHost::get();
    RaceManager* const race_manager = RaceManager::get();
    host->setForcedFirstPlayer(nullptr);
    host->setForcedSecondPlayer(nullptr);
    race_manager->clearPoles();

    if (mode)
    {
        resetPeersReady();
	updatePlayerList();

        std::vector<std::shared_ptr<NetworkPlayerProfile>>
            team_blue, team_red;

        host->getTeamLists(team_blue, team_red);

        // send message to team members
        const core::stringw header =
            L"Pole vote has been opened. If you want to remove your vote, use /999. Please vote for the teammate that will be at the "
            L"most front position towards the ball or puck:\n";

        core::stringw msg_red = header, msg_blue = header;

        msg_blue += formatTeammateList(team_blue);
        msg_red += formatTeammateList(team_red);

        NetworkString* const pkt_blue = getNetworkString();
        NetworkString* const pkt_red = getNetworkString();

        pkt_blue->setSynchronous(true);
        pkt_blue->addUInt8(LE_CHAT);
        pkt_red->setSynchronous(true);
        pkt_red->addUInt8(LE_CHAT);
        pkt_blue->encodeString16(msg_blue);
        pkt_red->encodeString16(msg_red);

        host->sendPacketToAllPeersWith([host](STKPeer* p)
                {
                    if (p->isAIPeer() || !p->isConnected() || !p->isValidated() || p->alwaysSpectate()) return false;
                    return host->isPeerInTeam(p, KART_TEAM_BLUE);
                }, pkt_blue);
        host->sendPacketToAllPeersWith([host](STKPeer* p)
                {
                    if (p->isAIPeer() || !p->isConnected() || !p->isValidated() || p->alwaysSpectate()) return false;
                    return host->isPeerInTeam(p, KART_TEAM_RED);
                }, pkt_red);

        delete pkt_blue;
        delete pkt_red;
    }
    else 
    {
        // delete command votes for "pole on"
        ServerLobbyCommands::get()->resetCommandVotesFor("pole");
        std::string resp("Pole has been disabled.");
	if (!ServerConfig::m_soccer_roulette)
	{
		sendStringToAllPeers(resp);
	}
    }
} // setPoleEnabled

//------------------------------------------------------------------------------------------
void ServerLobby::submitPoleVote(std::shared_ptr<STKPeer>& voter, const unsigned int vote)
{
    static bool isVoteCommandActive = true;	
    STKPeer* const voter_p = voter.get();
    std::set<STKPeer*> removedVoteOnce;

    if (!voter->hasPlayerProfiles())
        return;

    if (m_state.load() != WAITING_FOR_START_GAME)
    {
        sendStringToPeer(L"You can only vote for the poles before the game started.", voter);
        return;
    }

    if (!isVoteCommandActive)
    {
        sendStringToPeer(L"Voting is currently disabled. Please wait for the pole command to be activated.", voter);
        return;
    }
    std::shared_ptr<NetworkPlayerProfile> voter_profile = voter->getPlayerProfiles()[0];
    const KartTeam team = voter_profile->getTeam();
    std::map<STKPeer*, std::weak_ptr<NetworkPlayerProfile>>*
        mapping = &m_blue_pole_votes;
    if (team == KART_TEAM_NONE)
    {
        sendStringToPeer(L"You need to be in the team in order to vote for the pole.", voter);
        return;
    }

    if (voter->alwaysSpectate())
    {
        sendStringToPeer(L"You need to disable spectator mode in order to vote for the pole.", voter);
    	return;
    }

    if (team == KART_TEAM_RED)
        mapping = &m_red_pole_votes;

    auto teammates = STKHost::get()->getPlayerProfilesOfTeam(team);

    if (vote == 999) 
    {
        if (mapping->count(voter_p)) 
	{
            mapping->erase(voter_p);
            removedVoteOnce.insert(voter_p);
            sendStringToPeer(L"Your vote has been removed.", voter);
        }
       	else 
	{
            sendStringToPeer(L"You haven't voted yet, so there's nothing to remove.", voter);
        }
        return;
    }
    if (vote == 0 || teammates.size() == 0 || vote > teammates.size())
    {
        sendStringToPeer(L"Out of range. Please select one of the listed teammates.", voter);
        return;
    }
    if (mapping->count(voter_p)) {
        sendStringToPeer(L"You have already voted. You can only vote once. Use /999 to delete your vote.", voter);
        return;
    }
    
    std::weak_ptr<NetworkPlayerProfile> teammate = teammates[vote - 1];
    auto teammate_p = teammate.lock();
    const core::stringw& tmName = teammate_p->getName();
    (*mapping)[voter_p] = teammate;
    core::stringw msg = voter_profile->getName();
    msg += L" voted for ";
    msg += tmName;
    msg += L" to be the pole.";

    NetworkString* const packet = getNetworkString();
    packet->setSynchronous(true);
    packet->addUInt8(LE_CHAT);
    packet->encodeString16(msg);

    STKHost::get()->sendPacketToAllPeersWith(
            [team](STKPeer* p)
            {
                if (p->isAIPeer()) return false;
                return STKHost::get()->isPeerInTeam(p, team);
            }, packet);
    delete packet;

} // submitPoleVote

//-----------------------------------------------------------------------------------------

std::shared_ptr<NetworkPlayerProfile> ServerLobby::decidePoleFor(
        const PoleVoterMap& mapping, const KartTeam team) const
{
    std::shared_ptr<NetworkPlayerProfile> npp, max_npp;
    unsigned max_npp_c = 0;
    std::map<std::shared_ptr<NetworkPlayerProfile>, unsigned> res;

    for (auto entry : mapping)
    {
        if (entry.second.expired())
            continue;

        npp = entry.second.lock();
        if (npp->getTeam() != team)
            continue;

        auto rentry = res.find(npp);
        if (rentry == res.cend())
        {
            res[npp] = 1;
            if (max_npp_c < 1)
            {
                max_npp_c = 1;
                max_npp = npp;
            }
        }
        else
        {
            res[npp] += 1;
            if (max_npp_c < res[npp])
            {
                max_npp_c = res[npp];
                max_npp = npp;
            }
        }
    }

    if (res.size() == 0)
        return nullptr;

    /*return std::max_element(
            res.cbegin(), res.cend(),
            [](const PoleVoterResultEntry& a,
               const PoleVoterResultEntry& b)
            {
                return a.second < b.second;
            })->first;*/
    return max_npp;
} // countPoleVotes
//-----------------------------------------------------------------------------------------
std::pair<
    std::shared_ptr<NetworkPlayerProfile>,
    std::shared_ptr<NetworkPlayerProfile>>
ServerLobby::decidePoles()
{
    std::shared_ptr<NetworkPlayerProfile> blue, red;

    blue = decidePoleFor(m_blue_pole_votes, KART_TEAM_BLUE);
    red = decidePoleFor(m_red_pole_votes, KART_TEAM_RED);

    return std::make_pair(blue, red);
} // decidePoles

void ServerLobby::announcePoleFor(std::shared_ptr<NetworkPlayerProfile>& pole, const KartTeam team) const
{
    if (team == KART_TEAM_NONE || pole == nullptr)
        return;

    core::stringw msgS = L"Current pole player is ";
    msgS += pole->getName();
    NetworkString* const msg = getNetworkString();
    msg->setSynchronous(true);
    msg->addUInt8(LE_CHAT);
    msg->encodeString16(msgS);

    STKHost::get()->sendPacketToAllPeersWith(
            [team](STKPeer* p)
            {
                if (!p->isConnected() || !p->hasPlayerProfiles() || p->isAIPeer() || !p->isValidated() ||
                        p->isWaitingForGame())
                    return false;

                return STKHost::get()->isPeerInTeam(p, team);
            }, msg);
    
    delete msg;
}  // announcePoleFor

void ServerLobby::setRandomKartsEnabled(const bool state, const bool announce)
{
    m_random_karts_enabled = state;
    std::string msg = "Random karts have been ";

    if (state)
    {
        assignRandomKarts();
        if (announce)
        {
            msg += "activated.";
            sendStringToAllPeers(msg);
        }
    }
    else
    {
        resetKartSelections();
        if (announce)
        {
            msg += "deactivated.";
            sendStringToAllPeers(msg);
        }
    }
}

NetworkString* ServerLobby::addRandomInstalladdonMessage(NetworkString* const ril_pkt) const
{
    if (!ServerConfig::m_enable_ril)
        return ril_pkt;
    std::string ril_prefix = ServerConfig::m_ril_prefix;
    std::string msg(ril_prefix + " ");
    // choose an addon
    msg += getRandomAddon();

    ril_pkt->addUInt8(LE_CHAT);
    ril_pkt->encodeString16(StringUtils::utf8ToWide(msg));
    return ril_pkt;
}
//-----------------------------------------------------------------------------
void ServerLobby::sendRandomInstalladdonLine(STKPeer* const peer) const
{
    if (ServerConfig::m_enable_ril)
    {
        NetworkString* ril_pkt = getNetworkString();
        ril_pkt->setSynchronous(true);
        addRandomInstalladdonMessage(ril_pkt);

        peer->sendPacket(ril_pkt, true/*reliable*/);
        delete ril_pkt;
    }
} // sendRandomInstalladdonLine
//-----------------------------------------------------------------------------
void ServerLobby::sendRandomInstalladdonLine(std::shared_ptr<STKPeer> const peer) const
{
    if (ServerConfig::m_enable_ril)
    {
	NetworkString* ril_pkt = getNetworkString();
	ril_pkt->setSynchronous(true);
	addRandomInstalladdonMessage(ril_pkt);

        peer->sendPacket(ril_pkt, true/*reliable*/);
	delete ril_pkt;
    }
} // sendRandomInstalladdonLine
void ServerLobby::sendCurrentModifiers(STKPeer* const peer) const
{
    NetworkString* pkt = getNetworkString();
    pkt->setSynchronous(true);
    pkt->addUInt8(LE_CHAT);
    std::string msg;

    // add stuff here
    addKartRestrictionMessage(msg);
    addPowerupSMMessage(msg);

    if (!msg.empty())
    {
        msg.insert(0, "\n---===---");
        msg        += "\n---===---";
        pkt->encodeString16(StringUtils::utf8ToWide(msg));
        peer->sendPacket(pkt, true/*reliable*/);
    }
    delete pkt;
}
void ServerLobby::sendCurrentModifiers(std::shared_ptr<STKPeer>& peer) const
{
    NetworkString* pkt = getNetworkString();
    pkt->setSynchronous(true);
    pkt->addUInt8(LE_CHAT);
    std::string msg;

    // add stuff here
    addKartRestrictionMessage(msg);
    addPowerupSMMessage(msg);

    if (!msg.empty())
    {
        msg.insert(0, "\n---===---\n");
        msg        += "---===---\n";
        pkt->encodeString16(StringUtils::utf8ToWide(msg));
        peer->sendPacket(pkt, true/*reliable*/);
    }
    delete pkt;
}
void ServerLobby::addKartRestrictionMessage(std::string& msg) const
{
    if (m_kart_restriction == NONE)
        return;

    switch (m_kart_restriction)
    {
        case HEAVY:
            msg += "HEAVY PARTY is ACTIVE! Only heavy karts can be chosen\n";
            break;
        case MEDIUM:
            msg += "MEDIUM PARTY is ACTIVE! Only medium karts can be chosen\n";
            break;
        case LIGHT:
            msg += "LIGHT INSURANCE is ACTIVE! Only light karts can be chosen, to ensure better experience.\n";
            break;
        case NONE:
            break;
    }
}
void ServerLobby::addPowerupSMMessage(std::string& msg) const
{
    if (RaceManager::get()->getPowerupSpecialModifier() == Powerup::TSM_NONE)
        return;

    switch (RaceManager::get()->getPowerupSpecialModifier())
    {
        case Powerup::TSM_BOWLPARTY:
            msg += "BOWL PARTY is ACTIVE! All boxes give 3 bowling balls.\n";
            break;
        case Powerup::TSM_CAKEPARTY:
	    msg += "CAKE PARTY IS ACTIVE! All boxes are full of cakes.\n";
        default:
            break;
    }
}
int ServerLobby::loadPermissionLevelForOID(const uint32_t online_id)
{
#ifdef ENABLE_SQLITE3
    if (!m_db || !m_db->hasPermissionsTable())
        return 0;

    if (ServerConfig::m_server_owner != -1 
            && online_id == ServerConfig::m_server_owner)
        return std::numeric_limits<int>::max();

    return m_db->loadPermissionLevelForOID(online_id);
#else
    return 0;
#endif
}
void ServerLobby::writePermissionLevelForOID(const uint32_t online_id, const int lvl)
{
#ifdef ENABLE_SQLITE3
    m_db->writePermissionLevelForOID(online_id, lvl);
#endif
}
void ServerLobby::writePermissionLevelForUsername(const core::stringw& name, const int lvl)
{
#ifdef ENABLE_SQLITE3
    m_db->writePermissionLevelForUsername(name, lvl);
#endif
}
std::tuple<uint32_t, std::string> ServerLobby::loadRestrictionsForOID(const uint32_t online_id)
{
#ifdef ENABLE_SQLITE3
    return m_db->loadRestrictionsForOID(online_id);
#else
    return 0;
#endif
}
std::tuple<uint32_t, std::string> ServerLobby::loadRestrictionsForUsername(const core::stringw& name)
{
#ifdef ENABLE_SQLITE3
    return m_db->loadRestrictionsForUsername(name);
#else
    return 0;
#endif
}
void ServerLobby::writeRestrictionsForOID(const uint32_t online_id, const uint32_t flags)
{
#ifdef ENABLE_SQLITE3
    m_db->writeRestrictionsForOID(online_id, flags);
#endif
}
void ServerLobby::writeRestrictionsForOID(const uint32_t online_id, const uint32_t flags,
        const std::string& kart_id)
{
#ifdef ENABLE_SQLITE3
    m_db->writeRestrictionsForOID(online_id, flags, kart_id);
#endif
}
void ServerLobby::writeRestrictionsForOID(const uint32_t online_id, const std::string& kart_id)
{
#ifdef ENABLE_SQLITE3
    m_db->writeRestrictionsForOID(online_id, kart_id);
#endif
}
void ServerLobby::writeRestrictionsForUsername(const core::stringw& name, const uint32_t flags)
{
#ifdef ENABLE_SQLITE3
    m_db->writeRestrictionsForUsername(name, flags);
#endif
}
void ServerLobby::writeRestrictionsForUsername(const core::stringw& name, const uint32_t flags,
        const std::string& kart_id)
{
#ifdef ENABLE_SQLITE3
    m_db->writeRestrictionsForUsername(name, flags, kart_id);
#endif
}
void ServerLobby::writeRestrictionsForUsername(const core::stringw& name,
        const std::string& kart_id)
{
#ifdef ENABLE_SQLITE3
    m_db->writeRestrictionsForUsername(name, kart_id);
#endif
}
void ServerLobby::sendNoPermissionToPeer(STKPeer* p, const std::vector<std::string>& argv)
{
    NetworkString* const msg = getNetworkString();
    msg->setSynchronous(true);
    msg->addUInt8(LE_CHAT);
    if (ServerConfig::m_permission_message.toString().empty() && argv.size() >= 1)
    {
        core::stringw msg_ = L"Unknown command: ";
        msg_ += StringUtils::utf8ToWide(argv[0]);
        msg->encodeString16(msg_);
    }
    else
        msg->encodeString16(
                StringUtils::utf8ToWide(ServerConfig::m_permission_message));
    p->sendPacket(msg, true/*reliable*/);
    delete msg;
}
uint32_t ServerLobby::lookupOID(const std::string& name)
{
#ifdef ENABLE_SQLITE3
    return m_db->lookupOID(name);
#else
    return 0;
#endif
}
uint32_t ServerLobby::lookupOID(const core::stringw& name)
{
#ifdef ENABLE_SQLITE3
    return m_db->lookupOID(name);
#else
    return 0;
#endif
}
int ServerLobby::banPlayer(const std::string& name, const std::string& reason, const int days)
{
#ifdef ENABLE_SQLITE3
    return m_db->banPlayer(name, reason, days);
#else
    return -2;
#endif
}
int ServerLobby::unbanPlayer(const std::string& name)
{
#ifdef ENABLE_SQLITE3
    return m_db->unbanPlayer(name);
#else
    return -2;
#endif
}
const std::string ServerLobby::formatBanList(unsigned int page,
        const unsigned int psize)
{
#ifdef ENABLE_SQLITE3
    return m_db->formatBanList(page, psize);
#else
    return "";
#endif
}
const std::string ServerLobby::formatBanInfo(const std::string& name)
{
    return m_db->formatBanInfo(name);
}
int ServerLobby::loadPermissionLevelForUsername(const core::stringw& name)
{
#if ENABLE_SQLITE3
    return m_db->loadPermissionLevelForUsername(name);
#else
    return PERM_PLAYER;
#endif
}

std::string ServerLobby::get_elo_change_string()
{
    std::string fileName = "elo_changes.txt";
    std::ifstream in_file2(fileName);
    std::string result = "";
    std::string player;
    std::string elo_change;
    std::vector<std::string> split;
    if (in_file2.is_open())
    {
        std::string line;
        while (std::getline(in_file2, line))
        {
            split = StringUtils::split(line, ' ');
            if (split.size() < 2) continue;
            player = split[0];
            elo_change = split[1];
            result += player + " " + elo_change + "\n";
        }
    }
    return result;
}

std::pair<std::vector<std::string>, std::vector<std::string>> ServerLobby::createBalancedTeams(std::vector<std::pair<std::string, int>>& elo_players)
{
    int num_players = elo_players.size();
    int min_elo_diff = INT_MAX;
    int optimal_teams = -1;

    for (int teams = 0; teams < pow(2, num_players - 1); teams++)
    {
        int elo_red = 0, elo_blue = 0;
        for (int player_idx = 0; player_idx < num_players; player_idx++)
        {
            if (teams & 1 << player_idx)
                elo_red += elo_players[player_idx].second;
            else
                elo_blue += elo_players[player_idx].second;
        }
        int elo_diff = std::abs(elo_red - elo_blue);
        if (elo_diff < min_elo_diff)
        {
            min_elo_diff = elo_diff;
            optimal_teams = teams;
        }
        if (elo_diff == 0) break;
    }

    std::vector<std::string> red_team, blue_team;

    for (int player_idx = 0; player_idx < num_players; player_idx++)
    {
        if (optimal_teams & 1 << player_idx)
            red_team.push_back(elo_players[player_idx].first);
        else
            blue_team.push_back(elo_players[player_idx].first);
    }
    return std::pair<std::vector<std::string>, std::vector<std::string>>(red_team, blue_team);
}

void ServerLobby::soccerRankedMakeTeams(std::pair<std::vector<std::string>, std::vector<std::string>> teams, int min, std::vector <std::pair<std::string, int>> player_vec)
{
    auto peers2 = STKHost::get()->getPeers();
    int random = rand() % 2;
    std::string msg = "";
    std::string blue = "blue";
    std::string red = "red";

    for (auto peer2 : peers2)
    {
        for (auto player : peer2->getPlayerProfiles())
        {
            std::string username = std::string(StringUtils::wideToUtf8(player->getName()));
            if (player_vec.size() % 2 == 1)
            {
                int min_idx = std::min(min, (int)player_vec.size() - 1);
                if (username == player_vec[min_idx].first)
                {
                    if (random == 1)
                    {
                        player->setTeam(KART_TEAM_RED);
                        msg = "Player " + player_vec[min_idx].first + " has been put in the red team. Random=" + std::to_string(random);
                        Log::info("ServerLobby", msg.c_str());
                    }
                    else
                    {
                        player->setTeam(KART_TEAM_BLUE);
                        msg = "Player " + player_vec[min_idx].first + " has been put in the blue team. Random=" + std::to_string(random);
                        Log::info("ServerLobby", msg.c_str());
                    }
                }

            }
            if (std::find(teams.first.begin(), teams.first.end(), username) != teams.first.end())
            {
                player->setTeam(KART_TEAM_RED);
            }
            if (std::find(teams.second.begin(), teams.second.end(), username) != teams.second.end())
            {
                player->setTeam(KART_TEAM_BLUE);
            }
        }
    }
    return;
}

int64_t ServerLobby::getTimeout()
{
    return m_timeout.load();
}
void ServerLobby::changeTimeout(long timeout, bool infinite, bool absolute)
{
    std::string msg;

    if (infinite)
        m_timeout.store(std::numeric_limits<std::int64_t>::max());
    else if (absolute)
        m_timeout.store(timeout * 1000);
    else
        m_timeout.store(m_timeout.load() + timeout * 1000);

    // new configuration
    NetworkString* server_info = getNetworkString();
    server_info->setSynchronous(true);
    server_info->addUInt8(LE_SERVER_INFO);
    float auto_start_timer = 0.0f;
    if (m_timeout.load() == std::numeric_limits<int64_t>::max())
        auto_start_timer = std::numeric_limits<float>::max();
    else
    {
        auto_start_timer =
            (m_timeout.load() - (int64_t)StkTime::getMonoTimeMs()) / 1000.0f;
    }
    m_game_setup->addModifiedServerInfo(
            server_info, -1, -1, 0, -1, -1, -1,
            auto_start_timer, "", false, false, false, false, false);
    if (absolute)
    {
        msg = StringUtils::insertValues(
                "Set %d seconds for the start timeout.", timeout);
    }
    else
    {
        msg = StringUtils::insertValues(
                "Added %d seconds to the start timeout.", timeout);
    }
    STKHost::get()->sendPacketToAllPeers(server_info);

    // and also send the changing seconds notification
    if (!ServerConfig::m_soccer_roulette)
    {
	    sendStringToAllPeers(msg);
    }

    delete server_info;
}
//-----------------------------------------------------------------------------
bool ServerLobby::setForcedTrack(std::string track_id,
        int laps,
        bool specvalue, const bool is_soccer, const bool announce)
{
        bool found = false;
        PeerVote fv;

        auto peers = STKHost::get()->getPeers();
        for (auto& peer2 : peers)
        {
            found = serverAndPeerHaveTrack(peer2, track_id) || track_id == "all";
            if (!found)
            {
                found = serverAndPeerHaveTrack(peer2, "addon_" + track_id);
                if (found)
                {
                    track_id = "addon_" + track_id;
                    break;
                }
            }
            else break;
        }
        if (track_id == "all") found = true;

        if (found)
        {
            if (track_id == "all")
            {
                m_set_field = "";
                m_set_laps = 0;
                m_fixed_laps = -1;
                m_set_specvalue = false;
                std::string msg = is_soccer ? "All soccer fields can be played again" : "All tracks can be played again";
                sendStringToAllPeers(msg);
                Log::info("ServerLobby", "setfield all");
            }
            else
            {
                Track* t = track_manager->getTrack(track_id);
                if (!t)
                    return false;
                if (laps < 1)
                {
                    laps = t->getDefaultNumberOfLaps();
                }
                m_set_field = track_id;
                m_set_laps = laps;
                m_fixed_laps = laps;
                m_set_specvalue = specvalue;
                std::string msg = is_soccer ? "Next played soccer field will be " + track_id + "." :
                    "Next played track will be " + track_id + ".";
                if (!ServerConfig::m_soccer_roulette)
                {
                    // Send message to the lobby
                    sendStringToAllPeers(msg);
                    std::string msg2 = "setfield " + track_id;
                    Log::info("ServerLobby", msg2.c_str());
                }
            }
            return true;
        }
        else
        {
            /*std::string msg = is_soccer ? "Soccer field \'" + track_id + "\' does not exist or is not installed." :
                "Track \'" + track_id + "\' does not exist or is not installed.";
            sendStringToPeer(msg, peer);*/
            return false;
        }
}
// =======================================================================
// Assigns random karts to all players in the lobby
// Uses the available kart list to randomly select karts

void ServerLobby::assignRandomKarts()
{
    if (m_peers_ready.empty())
    {
        Log::error("ServerLobby", "No players in the lobby to assign random karts.");
        return;
    }
    if (m_available_kts.first.empty())
    {
        Log::error("ServerLobby", "No karts available.");
        return;
    }
    RandomGenerator random_gen;
    for (auto& peer : m_peers_ready)
    {
        auto locked_peer = peer.first.lock();
        if (!locked_peer)
            continue;
        auto player = locked_peer->getPlayerProfiles();
        if (!player.empty())
        {
            for (unsigned i = 0; i < player.size(); i++)
            {
                auto& player_profile = player[i];
                std::set<std::string>::iterator it = m_available_kts.first.begin();
                std::advance(it, random_gen.get((int)m_available_kts.first.size()));
                std::string selected_kart = *it;
                player_profile->forceKart(selected_kart);
            }
        }
    }
    std::string msg = "Random karts have been forcibly assigned to all players, to disable this state: /randomkarts off";
    sendStringToAllPeers(msg);
}
// ===============================================================================
// Resets all kart selections for players in the lobby
// Removes any forced kart assignments

void ServerLobby::resetKartSelections()
{
        for (auto& peer : m_peers_ready)
        {
                auto locked_peer = peer.first.lock();
                if (!locked_peer)
                        continue;
                auto player = locked_peer->getPlayerProfiles();
                if (!player.empty())
                {
                        for (unsigned i = 0; i < player.size(); i++)
                        {
                                auto& player_profile = player[i];
                                player_profile->unforceKart();
                        }
                }
        }
}
// ========================================================================
// Executes the Python script for track records and returns its output

std::string ServerLobby::execPythonScript()
{
    std::array<char, 1024> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen("python3 track_records.py", "r"), pclose);

    if (!pipe)
    {
        throw std::runtime_error("popen() failed!");
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    {
        result += buffer.data();
    }

    return result;
}
// =========================================================================
// Gets a player's soccer ranking and rating from the ranking file
// Returns a pair containing:
// - first: player's rank (or max unsigned int if not found)
// - second: player's rating (or 0 if not found)
// Reads through ranking file line by line to find matching username

std::pair<unsigned int, int> ServerLobby::getSoccerRanking(std::string username) const
{
    std::ifstream file(ServerConfig::m_soccer_ranking_path.c_str());
    std::string line, name;
    int rank = 1;
    int rating;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        iss >> name;
        std::string word;
        while (iss >> word) {
            try {
                rating = std::stoi(word);
            } catch (const std::exception& e) {
                continue;
            }
        }
        if (name == username)
            return std::make_pair(rank, rating);
        rank++;
    }
    return std::make_pair(std::numeric_limits<unsigned int>::max(), 0);
    return std::make_pair(std::numeric_limits<unsigned int>::max(), 0);
}
//=========================================================================
// Checks if a player has all required standard tracks installed
// (except for allowed exceptions)
bool ServerLobby::checkAllStandardContentInstalled(STKPeer* peer) const
{
    const auto& client_assets = peer->getClientAssets();
    std::vector<std::string> missing_tracks;
    // TODO: separate ServerConfig entry
    const std::set<std::string> allowed_missing_tracks = {"hole_drop", "oasis"};
    const std::vector<std::string>& all_track_ids = track_manager->getAllTrackIdentifiers();   
    for (const std::string& track_id : all_track_ids)
    {
        if (allowed_missing_tracks.find(track_id) != allowed_missing_tracks.end())
            continue;      
        Track* track = track_manager->getTrack(track_id);
        if (track && !track->isAddon() && 
            client_assets.second.find(track_id) == client_assets.second.end())
        {
            missing_tracks.push_back(track_id);
        }
    }
    if (!missing_tracks.empty())
    {
        std::string msg = "You are missing standard tracks";   
        msg += "\n\nMissing tracks:\n";
        for (const auto& track : missing_tracks)
            msg += "- " + track + "\n";
        sendStringToPeer(msg, peer);
        return false;
    }   
    return true;
}
// Soccer Roulette
void ServerLobby::checkSoccerRoulette()
{
    if (ServerConfig::m_soccer_roulette)
    {
        std::string next_field = SoccerRoulette::get()->getNextField();
        Log::info("ServerLobby", "Soccer Roulette: Setting next field to %s", next_field.c_str());
        setForcedTrack(next_field, 10, false, true, true);
    }
}
// check if this person has won the most recent soccer roulette event
bool ServerLobby::checkXmlEmoji(const std::string& username) const
{
    std::string filename = "checkevent.xml";
    XMLNode* root = file_manager->createXMLTree(filename);
    if (!root) return false;
    for (unsigned i = 0; i < root->getNumNodes(); i++)
    {
        const XMLNode* node = root->getNode(i);
        if (node->getName() == "player")
        {
            std::string name;
            node->get("name", &name);
            if (name == username)
            {
                delete root;
                return true;
            }
        }
    }    
    delete root;
    return false;
}
// -- 
void ServerLobby::startTeamSelectionVote()
{
    m_team_selection_votes_a = 0;
    m_team_selection_votes_b = 0;
    m_team_selection_voted_peers.clear();
    m_team_selection_vote_active = true;
    // timer
    m_team_selection_vote_timer = StkTime::getMonoTimeMs() + 60000;
}
void ServerLobby::handleTeamSelectionVote(STKPeer* const peer, const bool select_option_a)
{
    if (!m_team_selection_vote_active)
    {
        std::string msg = "No team selection vote is currently active.";
        sendStringToPeer(msg, peer);
        return;
    }
    if (m_team_selection_voted_peers.find(peer->getHostId()) != m_team_selection_voted_peers.end())
    {
        std::string msg = "You have already voted for team selection.";
        sendStringToPeer(msg, peer);
        return;
    }
    if (select_option_a)
    {
        m_team_selection_votes_a++;
        m_team_selection_voted_peers.insert(peer->getHostId());
        std::string msg = "You voted for Team Option A.";
        sendStringToPeer(msg, peer);
    }
    else
    {
        m_team_selection_votes_b++;
        m_team_selection_voted_peers.insert(peer->getHostId());
        std::string msg = "You voted for Team Option B.";
        sendStringToPeer(msg, peer);
    }
    int total_players = STKHost::get()->getPeers().size();
    int required_votes = std::max(2, total_players / 2 + 1);
    
    if (m_team_selection_votes_a >= required_votes)
    {
        applyTeamSelection(true);
        m_team_selection_vote_active = false;
        std::string msg = "Vote complete! Applying Team Option A.";
        sendStringToAllPeers(msg);
    }
    else if (m_team_selection_votes_b >= required_votes)
    {
        applyTeamSelection(false);
        m_team_selection_vote_active = false;
        std::string msg = "Vote complete! Applying Team Option B.";
        sendStringToAllPeers(msg);
    }
    else
    {
        std::string msg = "Team selection vote: " + std::to_string(m_team_selection_votes_a) +
		" for Option A, " + std::to_string(m_team_selection_votes_b) +
		" for Option B. ";
	sendStringToAllPeers(msg);
    }
}
void ServerLobby::applyTeamSelection(bool select_option_a)
{
    if (select_option_a)
    {
        soccerRankedMakeTeams(m_team_option_a, m_min_player_idx, m_player_vec);
    }
    else
    {
        soccerRankedMakeTeams(m_team_option_b, m_min_player_idx, m_player_vec);
    }
    updatePlayerList();
}
void ServerLobby::checkTeamSelectionVoteTimeout()
{
    if (m_team_selection_vote_active && StkTime::getMonoTimeMs() > m_team_selection_vote_timer)
    {
        m_team_selection_vote_active = false;
        if (m_team_selection_votes_a > m_team_selection_votes_b)
        {
            applyTeamSelection(true);
            std::string msg = "Vote time expired! Applying Team Option A based on majority vote.";
            sendStringToAllPeers(msg);
        }
        else if (m_team_selection_votes_b > m_team_selection_votes_a)
        {
            applyTeamSelection(false);
            std::string msg = "Vote time expired! Applying Team Option B based on majority vote.";
            sendStringToAllPeers(msg);
        }
        else
        {
            // random
            bool select_a = (rand() % 2 == 0);
            applyTeamSelection(select_a);
            std::string msg = "Vote time expired with a tie! Randomly selected " + 
                              std::string(select_a ? "Team Option A" : "Team Option B") + ".";
            sendStringToAllPeers(msg);
        }
    }
}
std::pair<std::vector<std::string>, std::vector<std::string>> ServerLobby::createAlternativeTeams(
    const std::vector<std::pair<std::string, int>>& players)
{
    std::vector<std::string> red_team, blue_team;
    std::vector<std::pair<std::string, int>> sorted_players = players;
    std::sort(sorted_players.begin(), sorted_players.end(), 
        [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) 
        { return a.second > b.second; });
    for (size_t i = 0; i < sorted_players.size(); i++)
    {
        if (i % 4 == 0 || i % 4 == 3) 
        {
            red_team.push_back(sorted_players[i].first);
        }
        else 
        {
            blue_team.push_back(sorted_players[i].first);
        }
    }
    return std::make_pair(red_team, blue_team);
}
void ServerLobby::checkRPSTimeouts()
{
    uint64_t current_time = StkTime::getMonoTimeMs();
    for (auto it = m_rps_challenges.begin(); it != m_rps_challenges.end(); )
    {
        if (current_time > it->timeout)
        {
            std::shared_ptr<STKPeer> challenger_peer = NULL;
            std::shared_ptr<STKPeer> challenged_peer = NULL;
            for (auto& p : STKHost::get()->getPeers())
            {
                if (p->getHostId() == it->challenger_id)
                    challenger_peer = p;
                else if (p->getHostId() == it->challenged_id)
                    challenged_peer = p;   
                if (challenger_peer && challenged_peer)
                    break;
            }
            if (!it->accepted)
            {
                if (challenger_peer)
                {
                    std::string msg = it->challenged_name + " didn't accept your Rock Paper Scissors challenge.";
                    sendStringToPeer(msg, challenger_peer);
                }   
                it = m_rps_challenges.erase(it);
            }
            else if (it->challenger_choice == RPS_NONE && it->challenged_choice == RPS_NONE)
            {
                std::string msg = "Rock Paper Scissors game timed out. Neither player made a choice.";
                if (challenger_peer)
                    sendStringToPeer(msg, challenger_peer);
                if (challenged_peer)
                    sendStringToPeer(msg, challenged_peer);   
                it = m_rps_challenges.erase(it);
            }
            else if (it->challenger_choice == RPS_NONE)
            {
                if (challenged_peer)
                {
                    std::string msg = "You win the Rock Paper Scissors game by default! " + it->challenger_name + " didn't make a choice.";
                    sendStringToPeer(msg, challenged_peer);
                }
                if (challenger_peer)
                {
                    std::string msg = "You lost the Rock Paper Scissors game by default! You didn't make a choice in time.";
                    sendStringToPeer(msg, challenger_peer);
                }   
                it = m_rps_challenges.erase(it);
            }
            else if (it->challenged_choice == RPS_NONE)
            {
                if (challenger_peer)
                {
                    std::string msg = "You win the Rock Paper Scissors game by default! " + it->challenged_name + " didn't make a choice.";
                    sendStringToPeer(msg, challenger_peer);
                }
                if (challenged_peer)
                {
                    std::string msg = "You lost the Rock Paper Scissors game by default! You didn't make a choice in time.";
                    sendStringToPeer(msg, challenged_peer);
                }   
                it = m_rps_challenges.erase(it);
            }
            else
            {
                it = m_rps_challenges.erase(it);
            }
        }
        else
        {
            ++it;
        }
    }
}
void ServerLobby::determineRPSWinner(RPSChallenge& challenge)
{
    std::shared_ptr<STKPeer> challenger_peer = NULL;
    std::shared_ptr<STKPeer> challenged_peer = NULL;
    for (auto& p : STKHost::get()->getPeers())
    {
        if (p->getHostId() == challenge.challenger_id)
            challenger_peer = p;
        else if (p->getHostId() == challenge.challenged_id)
            challenged_peer = p;
        
        if (challenger_peer && challenged_peer)
            break;
    }
    STKPeer* winner_peer;
    STKPeer* loser_peer;
    RPSChoice winner_choice;
    RPSChoice loser_choice;
    std::string winner_name;
    std::string loser_name;

    if (challenge.challenger_choice == challenge.challenged_choice)
    {
        winner_peer = nullptr;
        loser_peer = nullptr;
        std::string result = "It's a tie! Both players chose " +
            RockPaperScissors::rpsToString(challenge.challenger_choice) + ".";
        
        if (challenger_peer)
            sendStringToPeer(result, challenger_peer);
        
        if (challenged_peer)
            sendStringToPeer(result, challenged_peer);
    }
    else if (RockPaperScissors::wins(
                challenge.challenger_choice, challenge.challenged_choice))
    {
        winner_choice = challenge.challenged_choice;
        loser_choice = challenge.challenger_choice;
        winner_name = challenge.challenged_name;
        loser_name = challenge.challenger_name;
        winner_peer = challenged_peer.get();
        loser_peer = challenger_peer.get();
    }
    else
    {
        winner_choice = challenge.challenger_choice;
        loser_choice = challenge.challenged_choice;
        winner_name = challenge.challenger_name;
        loser_name = challenge.challenged_name;
        winner_peer = challenger_peer.get();
        loser_peer = challenged_peer.get();
    }
    if (winner_peer)
    {
        std::string result = "You won! You chose " +
            RockPaperScissors::rpsToString(winner_choice) + " and " + 
            loser_name + " chose " +
            RockPaperScissors::rpsToString(loser_choice) + ".";
        sendStringToPeer(result, winner_peer);
    }
    if (loser_peer)
    {
        std::string result =
            "You lost! You chose " +
            RockPaperScissors::rpsToString(loser_choice) + " and " + 
            winner_name + " chose " +
            RockPaperScissors::rpsToString(winner_choice) + ".";
        sendStringToPeer(result, loser_peer);
    }
    for (auto it = m_rps_challenges.begin(); it != m_rps_challenges.end();)
    {
        if (it->challenger_id == challenge.challenger_id && it->challenged_id == challenge.challenged_id)
        {
            it = m_rps_challenges.erase(it);
            break;
        }
        else
            it++;
    }
}
// -- 
void ServerLobby::loadJumbleWordList()
{
    const std::string filename = "wordlist.txt";
    std::ifstream file(filename);
    
    m_jumble_word_list.clear();
    
    if (file.is_open())
    {
        std::string word;
        while (std::getline(file, word))
        {
            if (word.empty() || word.length() < 3)
                continue;
                
            size_t start = word.find_first_not_of(" \t\n\r");
            if (start == std::string::npos)
                continue;
                
            size_t end = word.find_last_not_of(" \t\n\r");
            word = word.substr(start, end - start + 1);
            
            for (char& c : word)
                c = std::tolower(c);
            
            bool valid = true;
            for (char c : word)
            {
                if (c < 'a' || c > 'z')
                {
                    valid = false;
                    break;
                }
            }
            
            if (valid)
                m_jumble_word_list.push_back(word);
        }
        
        file.close();
        
        Log::info("ServerLobby", "Successfully loaded %d words from %s.",
                 (int)m_jumble_word_list.size(), filename.c_str());
    }
    else
    {
        Log::warn("ServerLobby", "Could not open word list file: %s", filename.c_str());
    }
}

std::string ServerLobby::jumbleWord(const std::string& word)
{
    std::string jumbled = word;
    if (word.length() <= 1) return word;
    int attempts = 0;
    const int max_attempts = 5;
    do 
    {
        std::shuffle(jumbled.begin(), jumbled.end(), m_jumble_rng);
        attempts++;
    } 
    while (jumbled == word && attempts < max_attempts);
    if (jumbled == word) 
    {
        std::shuffle(jumbled.begin(), jumbled.end(), m_jumble_rng);
    }
    return jumbled;
}
void ServerLobby::startJumbleForPlayer(uint32_t player_id)
{
    if (m_jumble_word_list.empty()) return;
    std::uniform_int_distribution<size_t> dist(0, m_jumble_word_list.size() - 1);
    std::string correct_word = m_jumble_word_list[dist(m_jumble_rng)];
    std::string jumbled_word = jumbleWord(correct_word);
    if (correct_word == jumbled_word && correct_word.length() > 1)
    {
        jumbled_word = jumbleWord(correct_word);
        if (correct_word == jumbled_word)
        {
            return;
        }
    }
    m_jumble_player_words[player_id] = correct_word;
    m_jumble_player_jumbled[player_id] = jumbled_word;
    m_jumble_player_start_time[player_id] = StkTime::getMonoTimeMs();
    std::string announcement = "Unscramble this word: " + jumbled_word;
    for (auto& p : STKHost::get()->getPeers())
    {
        if (p->getHostId() == player_id)
        {
            sendStringToPeer(announcement, p);
            break;
        }
    }
}

void ServerLobby::endJumbleForPlayer(uint32_t player_id, bool won)
{
    auto it_word = m_jumble_player_words.find(player_id);
    if (it_word == m_jumble_player_words.end()) return;
    std::string correct_word = it_word->second;
    std::shared_ptr<STKPeer> peer = nullptr;
    for (auto& p : STKHost::get()->getPeers())
    {
        if (p->getHostId() == player_id)
        {
            peer = p;
            break;
        }
    }
    if (!peer) return;
    if (won)
    {
        std::string msg = "Well done! You correctly unscrambled the word: " + correct_word;
        sendStringToPeer(msg, peer);
    }
    else
    {
        std::string msg = "Time's up! The word was: " + correct_word;
        sendStringToPeer(msg, peer);
    }
    m_jumble_player_words.erase(player_id);
    m_jumble_player_jumbled.erase(player_id);
    m_jumble_player_start_time.erase(player_id);
}
