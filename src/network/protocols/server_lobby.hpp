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

#ifndef SERVER_LOBBY_HPP
#define SERVER_LOBBY_HPP

#include "network/protocols/lobby_protocol.hpp"
#include "network/remote_kart_info.hpp"
#include "race/race_manager.hpp"
#include "race/kart_restriction.hpp"
#include "utils/cpp2011.hpp"
#include "utils/time.hpp"
#include "network/servers_manager.hpp"
#include "lobby/rps_challenge.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"

#include "irrString.h"

#include <algorithm>
//#include <array>
#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <fstream>
#include <locale>
#include <random>


class BareNetworkString;
class AbstractDatabase;
class NetworkItemManager;
class NetworkString;
class NetworkPlayerProfile;
class STKPeer;
class SocketAddress;
class Ranking;

namespace Online
{
    class Request;
}


class ServerLobby : public LobbyProtocol
{
    friend class ServerLobbyCommands;
    friend class STKCommandContext;
    // friend commands
    friend class SpectateCommand;
    friend class TeamChatCommand;
    friend class PublicCommand;
    friend class MuteCommand;
    friend class UnmuteCommand;
    friend class ListmuteCommand;
    friend class ListServerAddonCommand;
    friend class ServerHasAddonCommand;
    friend class ScanServersCommand; // asynchronous chat command
    friend class KartsCommand;
    friend class TracksCommand;
    friend class ReplayCommand;
    // To be separated into their own units
    friend class AutoteamsCommand;
    friend class AutoteamsVariantVoteCommand;
    friend class RPSCommand;
    friend class JumblewordCommand;
public:	
    typedef std::map<STKPeer*,
                      std::weak_ptr<NetworkPlayerProfile>>
        PoleVoterMap;
    typedef std::map<STKPeer* const,
                      std::weak_ptr<NetworkPlayerProfile>>
        PoleVoterConstMap;
    typedef std::pair<STKPeer* const,
                      std::weak_ptr<NetworkPlayerProfile>>
        PoleVoterConstEntry;
    typedef std::pair<std::weak_ptr<NetworkPlayerProfile>,
                      unsigned int>
        PoleVoterResultEntry;
    typedef std::pair<const std::weak_ptr<NetworkPlayerProfile>,
                      unsigned int>
        PoleVoterConstResultEntry;


    /* The state for a small finite state machine. */
    enum ServerState : unsigned int
    {
        SET_PUBLIC_ADDRESS,       // Waiting to receive its public ip address
        REGISTER_SELF_ADDRESS,    // Register with STK online server
        WAITING_FOR_START_GAME,   // In lobby, waiting for (auto) start game
        SELECTING,                // kart, track, ... selection started
        LOAD_WORLD,               // Server starts loading world
        WAIT_FOR_WORLD_LOADED,    // Wait for clients and server to load world
        WAIT_FOR_RACE_STARTED,    // Wait for all clients to have started the race
        RACING,                   // racing
        WAIT_FOR_RACE_STOPPED,    // Wait server for stopping all race protocols
        RESULT_DISPLAY,           // Show result screen
        ERROR_LEAVE,              // shutting down server
        EXITING
    };

private:
    struct KeyData
    {
        std::string m_aes_key;
        std::string m_aes_iv;
        irr::core::stringw m_name;
        std::string m_country_code;
        bool m_tried = false;
    };

    std::vector<std::string> m_jumble_word_list;
    std::map<uint32_t, std::string> m_jumble_player_words;
    std::map<uint32_t, std::string> m_jumble_player_jumbled;
    std::map<uint32_t, uint64_t> m_jumble_player_start_time;
    uint64_t m_jumble_last_event_time;
    std::mt19937 m_jumble_rng;
    std::mutex m_jumble_mutex;
    std::vector<RPSChallenge> m_rps_challenges;
    std::pair<
        std::vector<std::string>,
        std::vector<std::string>> m_team_option_a;
    std::pair<
        std::vector<std::string>,
        std::vector<std::string>> m_team_option_b;
    int m_min_player_idx;
    std::vector<std::pair<std::string, int>> m_player_vec;
    int m_team_selection_votes_a;
    int m_team_selection_votes_b;
    std::set<uint32_t> m_team_selection_voted_peers;
    bool m_team_selection_vote_active;
    uint64_t m_team_selection_vote_timer;
    bool m_random_karts_enabled;
    std::string m_replay_dir;
    bool m_replay_requested = false;    
#ifdef ENABLE_SQLITE3
    AbstractDatabase* m_db;

    void pollDatabase();
#endif

    std::atomic<ServerState> m_state;

    /* The state used in multiple threads when reseting server. */
    enum ResetState : unsigned int
    {
        RS_NONE, // Default state
        RS_WAITING, // Waiting for reseting finished
        RS_ASYNC_RESET // Finished reseting server in main thread, now async
                       // thread
    };

    std::atomic<ResetState> m_rs_state;

    /** Hold the next connected peer for server owner if current one expired
     * (disconnected). */
    std::weak_ptr<STKPeer> m_server_owner;

    /** AI peer which holds the list of reserved AI for dedicated server. */
    std::weak_ptr<STKPeer> m_ai_peer;

    /** AI profiles for all-in-one graphical client server, this will be a
     *  fixed count thorough the live time of server, which its value is
     *  configured in NetworkConfig. */
    std::vector<std::shared_ptr<NetworkPlayerProfile> > m_ai_profiles;

    std::atomic<uint32_t> m_server_owner_id;

    /** Official karts and tracks available in server. */
    std::pair<std::set<std::string>, std::set<std::string> > m_official_kts;

    /** Addon karts and tracks available in server. */
    std::pair<std::set<std::string>, std::set<std::string> > m_addon_kts;

    /** Addon arenas available in server. */
    std::set<std::string> m_addon_arenas;

    /** Addon soccers available in server. */
    std::set<std::string> m_addon_soccers;

    /** Available karts and tracks for all clients, this will be initialized
     *  with data in server first. */
    std::pair<std::set<std::string>, std::set<std::string> > m_available_kts;

    /** Keeps track of the server state. */
    std::atomic_bool m_server_has_loaded_world;

    bool m_registered_for_once_only;

    bool m_save_server_config;

    /** Counts how many peers have finished loading the world. */
    std::map<std::weak_ptr<STKPeer>, bool,
        std::owner_less<std::weak_ptr<STKPeer> > > m_peers_ready;

    std::map<std::weak_ptr<STKPeer>, std::set<irr::core::stringw>,
        std::owner_less<std::weak_ptr<STKPeer> > > m_peers_muted_players;

    std::weak_ptr<Online::Request> m_server_registering;

    /** Timeout counter for various state. */
    std::atomic<int64_t> m_timeout;

    std::mutex m_keys_mutex;

    std::map<uint32_t, KeyData> m_keys;

    std::map<std::weak_ptr<STKPeer>,
        std::pair<uint32_t, BareNetworkString>,
        std::owner_less<std::weak_ptr<STKPeer> > > m_pending_connection;

    std::map<std::string, uint64_t> m_pending_peer_connection;

    std::shared_ptr<Ranking> m_ranking;

    /* Saved the last game result */
    NetworkString* m_result_ns;

    /* Used to make sure clients are having same item list at start */
    BareNetworkString* m_items_complete_state;

    std::atomic<uint32_t> m_server_id_online;

    std::atomic<uint32_t> m_client_server_host_id;

    std::atomic<int> m_difficulty;

    std::atomic<int> m_game_mode;

    std::atomic<int> m_lobby_players;

    std::atomic<int> m_current_ai_count;

    std::atomic<uint64_t> m_last_success_poll_time;

    uint64_t m_last_unsuccess_poll_time, m_server_started_at, m_server_delay;

    // Default game settings if no one has ever vote, and save inside here for
    // final vote (for live join)
    PeerVote* m_default_vote;

    int m_battle_hit_capture_limit;

    float m_battle_time_limit;

    unsigned m_item_seed;

    uint32_t m_winner_peer_id;

    uint64_t m_client_starting_time;

    // Calculated before each game started
    unsigned m_ai_count;

    // TierS additional members
    std::shared_ptr<ServerList> m_last_wanrefresh_res;
    std::weak_ptr<STKPeer> m_last_wanrefresh_requester;
    std::atomic<bool> m_last_wanrefresh_is_peer;
    std::mutex m_wanrefresh_lock;

    // Pole
    bool m_pole_enabled = false;
    // For which player each peer submits a vote
    std::map<STKPeer*, std::weak_ptr<NetworkPlayerProfile>>
        m_blue_pole_votes;
    std::map<STKPeer*, std::weak_ptr<NetworkPlayerProfile>>
        m_red_pole_votes;
    /* forced track to be playing, or field */
    std::string m_set_field;
    /* forced laps, or forced minutes to play in case of the soccer game. */
    int         m_set_laps;
    /* for race it's reverse on/off, for battle/soccer it's random items */
    bool        m_set_specvalue;
    //std::map<std::string, std::vector<std::string>> m_command_voters;
    std::set<STKPeer*> m_team_speakers;
    int m_max_players;
    bool m_powerupper_active = false;
    // TODO:
    enum KartRestrictionMode m_kart_restriction = NONE;
    bool m_allow_powerupper = false;
    bool m_show_elo = false;
    bool m_show_rank = false;

    // connection management
    void clientDisconnected(Event* event);
    void connectionRequested(Event* event);
    // kart selection
    void kartSelectionRequested(Event* event);
    // Track(s) votes
    void handlePlayerVote(Event *event);
    void playerFinishedResult(Event *event);
    void registerServer(bool first_time);
    void finishedLoadingWorldClient(Event *event);
    void finishedLoadingLiveJoinClient(Event *event);
    void kickHost(Event* event);
    void changeTeam(Event* event);
    void handleChat(Event* event);
    void unregisterServer(bool now,
        std::weak_ptr<ServerLobby> sl = std::weak_ptr<ServerLobby>());
    void addPeerConnection(const std::string& addr_str)
    {
        m_pending_peer_connection[addr_str] = StkTime::getMonoTimeMs();
    }
    void removeExpiredPeerConnection()
    {
        // Remove connect to peer protocol running more than a 45 seconds
        // (from stk addons poll server request),
        for (auto it = m_pending_peer_connection.begin();
             it != m_pending_peer_connection.end();)
        {
            if (StkTime::getMonoTimeMs() - it->second > 45000)
                it = m_pending_peer_connection.erase(it);
            else
                it++;
        }
    }
    void replaceKeys(std::map<uint32_t, KeyData>& new_keys)
    {
        std::lock_guard<std::mutex> lock(m_keys_mutex);
        std::swap(m_keys, new_keys);
    }
    void handlePendingConnection();
    void handleUnencryptedConnection(std::shared_ptr<STKPeer> peer,
                                     BareNetworkString& data,
                                     uint32_t online_id,
                                     const irr::core::stringw& online_name,
                                     bool is_pending_connection,
                                     std::string country_code = "");
    bool decryptConnectionRequest(std::shared_ptr<STKPeer> peer,
                                  BareNetworkString& data,
                                  const std::string& key,
                                  const std::string& iv,
                                  uint32_t online_id,
                                  const irr::core::stringw& online_name,
                                  const std::string& country_code);
    bool handleAllVotes(PeerVote* winner, uint32_t* winner_peer_id);
    template<typename T>
    void findMajorityValue(const std::map<T, unsigned>& choices, unsigned cur_players,
                           T* best_choice, float* rate);
    void getRankingForPlayer(std::shared_ptr<NetworkPlayerProfile> p);
    void submitRankingsToAddons();
    void computeNewRankings();
    void checkRaceFinished();
    void getHitCaptureLimit();
    void configPeersStartTime();
    void resetServer();
    void addWaitingPlayersToGame();
    void changeHandicap(Event* event);
    void handlePlayerDisconnection() const;
    void addLiveJoinPlaceholder(
        std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
        unsigned int push_front_blue = 0,
        unsigned int push_front_red = 0) const;
    NetworkString* getLoadWorldMessage(
        std::vector<std::shared_ptr<NetworkPlayerProfile> >& players,
        bool live_join) const;
    void encodePlayers(BareNetworkString* bns,
        std::vector<std::shared_ptr<NetworkPlayerProfile> >& players) const;
    std::vector<std::shared_ptr<NetworkPlayerProfile> > getLivePlayers() const;
    void setPlayerKarts(const NetworkString& ns, STKPeer* peer) const;
    bool handleAssets(const NetworkString& ns, std::shared_ptr<STKPeer> peer);
    /* TODO: to be integrated with nnwcli */
    void handleServerCommand(Event* event, std::shared_ptr<STKPeer> peer);
    void liveJoinRequest(Event* event);
    void rejectLiveJoin(STKPeer* peer, BackLobbyReason blr);
    bool canLiveJoinNow() const;
    bool worldIsActive() const;
    int getReservedId(std::shared_ptr<NetworkPlayerProfile>& p,
                      unsigned local_id) const;
    void handleKartInfo(Event* event);
    void clientInGameWantsToBackLobby(Event* event);
    void clientSelectingAssetsWantsToBackLobby(Event* event);
    void kickPlayerWithReason(STKPeer* peer, const char* reason) const;
    void testBannedForIP(STKPeer* peer) const;
    void testBannedForIPv6(STKPeer* peer) const;
    void testBannedForOnlineId(STKPeer* peer, uint32_t online_id) const;
    void writePlayerReport(Event* event);
    bool supportsAI();
    void updateAddons();
public:
             ServerLobby();
    virtual ~ServerLobby();

    virtual bool notifyEventAsynchronous(Event* event) OVERRIDE;
    virtual bool notifyEvent(Event* event) OVERRIDE;
    virtual void setup() OVERRIDE;
    virtual void update(int ticks) OVERRIDE;
    virtual void asynchronousUpdate() OVERRIDE;

    void updatePlayerList(bool update_when_reset_server = false);
    std::shared_ptr<STKPeer> getServerOwner() const { return m_server_owner.lock(); }
    void updateServerOwner(std::shared_ptr<STKPeer> owner = nullptr);
    void updateTracksForMode();
    /* To be adjusted and unified with the legacy signature: find usage and adjust name */
    bool checkPeersReady(bool ignore_ai_peer) const;
    bool checkPeersCanPlay(bool ignore_ai_peer) const;
    char checkPeersCanPlayAndReady(bool ignore_ai_peer) const;
    void handleServerConfiguration(Event* event);
    void updateServerConfiguration(int new_difficulty, int new_game_mode,
            std::int8_t new_soccer_goal_target);
    // TODO: move to the source file
    void resetPeersReady()
    {
        for (auto it = m_peers_ready.begin(); it != m_peers_ready.end();)
        {
            if (it->first.expired())
            {
                it = m_peers_ready.erase(it);
            }
            else
            {
                it->second = false;
                it++;
            }
        }
    }

    void insertKartsIntoNotType(std::set<std::string>& set, const char* type) const;
    std::set<std::string> getOtherKartsThan(const std::string& name) const;
    const char* kartRestrictedTypeName(const enum KartRestrictionMode mode) const;
    enum KartRestrictionMode getKartRestrictionMode() const { return m_kart_restriction; }
    void setKartRestrictionMode(enum KartRestrictionMode mode);
    void startSelection(const Event *event=NULL);
    void checkIncomingConnectionRequests();
    void finishedLoadingWorld() OVERRIDE;
    ServerState getCurrentState() const { return m_state.load(); }
    void updateBanList();
    bool waitingForPlayers() const;
    virtual bool allPlayersReady() const OVERRIDE
                            { return m_state.load() >= WAIT_FOR_RACE_STARTED; }
    virtual bool isRacing() const OVERRIDE { return m_state.load() == RACING; }
    bool allowJoinedPlayersWaiting() const;
    void broadcastMessageInGame(const irr::core::stringw& message);
    void setSaveServerConfig(bool val)          { m_save_server_config = val; }
    float getStartupBoostOrPenaltyForKart(uint32_t ping, unsigned kart_id);
    int getDifficulty() const                   { return m_difficulty.load(); }
    int getGameMode() const                      { return m_game_mode.load(); }
    int getLobbyPlayers() const              { return m_lobby_players.load(); }
    void saveInitialItems(std::shared_ptr<NetworkItemManager> nim);
    void saveIPBanTable(const SocketAddress& addr);
    void removeIPBanTable(const SocketAddress& addr);
    void listBanTable(std::stringstream& out);
    void initServerStatsTable();
    bool isAIProfile(const std::shared_ptr<NetworkPlayerProfile>& npp) const
    {
        return std::find(m_ai_profiles.begin(), m_ai_profiles.end(), npp) !=
            m_ai_profiles.end();
    }
    uint32_t getServerIdOnline() const           { return m_server_id_online; }
    void setClientServerHostId(uint32_t id)   { m_client_server_host_id = id; }
    std::set<std::string> m_red_team;
    std::set<std::string> m_blue_team;
    std::vector<std::vector<std::string>>
                          m_tournament_fields_per_game;
    bool serverAndPeerHaveTrack(std::shared_ptr<STKPeer>& peer, std::string track_id) const;
    bool serverAndPeerHaveTrack(STKPeer* peer, std::string track_id) const;
    // TODO: refactor to rename the function, and perhaps merge it
    // ...outside of the lobby? or with another public member method?
    bool peerHasForcedTrack(std::shared_ptr<STKPeer>& peer) const;
    bool peerHasForcedTrack(STKPeer* peer) const;
    bool checkAllStandardContentInstalled(STKPeer* peer) const;
    static int m_fixed_laps;
    void sendStringToPeer(const std::string& s, std::shared_ptr<STKPeer>& peer) const;
    void sendStringToPeer(const irr::core::stringw& s, std::shared_ptr<STKPeer>& peer) const;
    void sendStringToPeer(const std::string& s, STKPeer* peer) const;
    void sendStringToPeer(const irr::core::stringw& s, STKPeer* peer) const;
    void sendStringToAllPeers(const std::string s);
    void sendRandomInstalladdonLine(STKPeer* peer) const;
    void sendRandomInstalladdonLine(std::shared_ptr<STKPeer> peer) const;
    void sendCurrentModifiers(STKPeer* peer) const;
    void sendCurrentModifiers(std::shared_ptr<STKPeer>& peer) const;
    void sendWANListToPeer(std::shared_ptr<STKPeer> peer);
    bool voteForCommand(std::shared_ptr<STKPeer>& peer, std::string command);
    NetworkString* addRandomInstalladdonMessage(NetworkString* ns) const;
    void addKartRestrictionMessage(std::string& msg) const;
    void addPowerupSMMessage(std::string& msg) const;
    const std::string getRandomAddon(RaceManager::MinorRaceModeType m=RaceManager::MINOR_MODE_NONE) const;
    bool isPoleEnabled() const { return m_pole_enabled; }
    core::stringw formatTeammateList(
            const std::vector<std::shared_ptr<NetworkPlayerProfile>> &team) const;
    void setPoleEnabled(bool mode);
    void sendPoleMessage(STKPeer* peer);
    void submitPoleVote(std::shared_ptr<STKPeer>& voter, unsigned int vote);

    std::shared_ptr<NetworkPlayerProfile> decidePoleFor(const PoleVoterMap& mapping, KartTeam team) const;

    std::pair<
        std::shared_ptr<NetworkPlayerProfile>,
        std::shared_ptr<NetworkPlayerProfile>> decidePoles();
    void announcePoleFor(std::shared_ptr<NetworkPlayerProfile>& p, KartTeam team) const;

    bool getRandomKartsEnabled() const { return m_random_karts_enabled; };
    void setRandomKartsEnabled(bool state, bool announce = true);

    // When the database is made into a singleton, deprecate this method.
    AbstractDatabase* getDatabase() { return m_db; };
    /* Moderation toolkit */
    bool moderationToolkitAvailable() { return true; }
    /* Deprecated functions, use AbstractDatabase instance instead */
    int loadPermissionLevelForOID(uint32_t online_id);
    int loadPermissionLevelForUsername(const core::stringw& name);
    void writePermissionLevelForOID(uint32_t online_id, int lvl);
    void writePermissionLevelForUsername(const core::stringw& name, int lvl);
    std::tuple<uint32_t, std::string> loadRestrictionsForOID(uint32_t online_id);
    std::tuple<uint32_t, std::string> loadRestrictionsForUsername(const core::stringw& name);
    void writeRestrictionsForOID(uint32_t online_id, uint32_t flags);
    void writeRestrictionsForOID(uint32_t online_id, uint32_t flags, const std::string& set_kart);
    void writeRestrictionsForOID(uint32_t online_id, const std::string& set_kart);
    void writeRestrictionsForUsername(const core::stringw& name, uint32_t flags);
    void writeRestrictionsForUsername(const core::stringw& name, uint32_t flags, const std::string& set_kart);
    void writeRestrictionsForUsername(const core::stringw& name, const std::string& set_kart);
    /**/
    void sendNoPermissionToPeer(STKPeer* p, const std::vector<std::string>& argv);
    void forceChangeTeam(NetworkPlayerProfile* player, KartTeam team);
    void forceChangeHandicap(NetworkPlayerProfile* player, HandicapLevel lvl);
    bool setForcedTrack(std::string track_id, int laps, bool specvalue = false,
            bool is_soccer = false, bool announce = true);
    const std::string& getForcedTrack() const { return m_set_field; };
    /* Todo: send to the underlying classes instead of keeping it here */
    /* Deprecated functions, use AbstractDatabase instance instead */
    uint32_t lookupOID(const std::string& name);
    uint32_t lookupOID(const core::stringw& name);
    int banPlayer(const std::string& name, const std::string& reason, int days = -1);
    int unbanPlayer(const std::string& name);
    const std::string formatBanList(unsigned int page = 0, unsigned int psize = 8);
    const std::string formatBanInfo(const std::string& name);
    /**/
    int64_t getTimeout();
    void changeTimeout(long timeout, bool infinite = false, bool absolute = false);

    int getMaxPlayers() const                                           { return m_max_players; }
    // unimplemented
    std::string getPlayerAlt(std::string username) const;
    std::pair<std::vector<std::string>, std::vector<std::string>> createBalancedTeams(std::vector<std::pair<std::string, int>>& elo_players);
    // TODO: move to soccer_elo_ranking
    std::pair<unsigned int, int> getSoccerRanking(std::string username) const;
    std::pair<unsigned int, int> getPlayerRanking(std::string username) const;
    std::string get_elo_change_string();
    void soccerRankedMakeTeams(std::pair<std::vector<std::string>, std::vector<std::string>> teams, int min, std::vector <std::pair<std::string, int>> player_vec);
    // TODO: move to soccer_autoteams
    void startTeamSelectionVote();
    void handleTeamSelectionVote(STKPeer* peer, bool select_option_a);
    void applyTeamSelection(bool select_option_a);
    void checkTeamSelectionVoteTimeout();
    std::pair<std::vector<std::string>, std::vector<std::string>> createAlternativeTeams(
        const std::vector<std::pair<std::string, int>>& players); 
    bool isReplayRequested() const                                      { return m_replay_requested; }
    void setReplayRequested(const bool value)                           { m_replay_requested = value; }
    // Soccer Roulette
    void checkSoccerRoulette();

    void loadJumbleWordList();
    std::string jumbleWord(const std::string& word);
    void startJumbleForPlayer(uint32_t player_id);
    void endJumbleForPlayer(uint32_t player_id, bool won);
    void updateJumbleTimer();
    void checkRPSTimeouts();
    void determineRPSWinner(RPSChallenge& challenge);
    bool checkXmlEmoji(const std::string& username) const;
    void assignRandomKarts();
    void resetKartSelections();
    std::string getTimeStamp();    
    std::string execPythonScript();    
    // Add more macro conditions when needed. SQLite3 is the only implementation for now.
};   // class ServerLobby

#endif // SERVER_LOBBY_HPP
