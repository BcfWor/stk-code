#ifndef HEADER_SOCCER_ROULETTE_HPP
#define HEADER_SOCCER_ROULETTE_HPP

#include "lobby/stk_command_context.hpp"
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <memory>

class STKPeer;
class NetworkPlayerProfile;

/**
 * \brief Class that manages soccer roulette mode.
 * \ingroup modes
 */
class SoccerRoulette
{
private:
    static SoccerRoulette* m_soccer_roulette;
    std::vector<std::string> m_fields;
    std::map<std::string, std::string> m_player_teams;
    int m_current_field_index;
    int m_minimap_socket;
    std::atomic<bool> m_minimap_running;
    std::thread m_minimap_thread;
    int m_minimap_update_interval_ms;
    std::string m_minimap_server_ip;
    int m_minimap_server_port;
    void loadFieldsFromConfig();
    void minimapExportThread();
    void exportMinimapData();
    SoccerRoulette();
    ~SoccerRoulette();

public:
    static void create();
    static void destroy();
    static SoccerRoulette* get();
    bool isEnabled() const;
    void reload();
    void resetIndex();
    std::string getNextField();
    std::string getCurrentField() const;
    void addField(const std::string& field);
    void removeField(const std::string& field);
    std::string getTeamForPlayer(const std::string& player_name) const;
    void assignTeamToPlayer(const std::string& player_name, const std::string& team);
    void assignTeamToPlayer(NetworkPlayerProfile* profile);
    std::string getTeamsInfo() const;
    bool checkRequiredAddons(STKPeer* peer, std::string& error_msg);
    void giveNitroToAll();
    const std::vector<std::string>& getFields() const { return m_fields; }
    void startMinimapExport(const std::string& server_ip, int server_port, int update_interval_ms = 33);
    void stopMinimapExport();
    static std::string m_default_minimap_server;
    static int m_default_minimap_port;
    static bool m_enable_minimap_export;
    void resetFieldIndex();
    void calculateGameResult();
    std::string getLastGameResults();
    void kickPlayer(const std::string& player_name, STKCommandContext* kicker);
    void reassignTeams(STKCommandContext* commander);
    void loadTeamsFromXML();
    void setRouletteTimeout(STKCommandContext* commander);
};

#endif

