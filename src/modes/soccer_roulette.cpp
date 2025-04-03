#include "modes/soccer_roulette.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"
#include "utils/time.hpp"
#include <algorithm>
#include "items/powerup_manager.hpp"
#include "items/powerup.hpp"
#include "modes/world.hpp"
#include "modes/soccer_world.hpp"
#include "race/race_manager.hpp"
#include "karts/abstract_kart.hpp"
#include "network/network_string.hpp"
#include "network/stk_host.hpp"
#include "network/protocols/lobby_protocol.hpp"
#include "io/file_manager.hpp"
#include "io/xml_node.hpp"
#include "network/network_player_profile.hpp"
#include "network/stk_peer.hpp"
#include "network/server_config.hpp"
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "karts/controller/controller.hpp"
#include "network/protocols/server_lobby.hpp"

SoccerRoulette* SoccerRoulette::m_soccer_roulette = NULL;

// -----------------------------------------------------------------------------
void SoccerRoulette::create()
{
    if (m_soccer_roulette == NULL)
    {
        m_soccer_roulette = new SoccerRoulette();
    }
}
// -----------------------------------------------------------------------------
void SoccerRoulette::destroy()
{
    if (m_soccer_roulette != NULL)
    {
        delete m_soccer_roulette;
        m_soccer_roulette = NULL;
    }
}
// -----------------------------------------------------------------------------
SoccerRoulette* SoccerRoulette::get()
{
    if (m_soccer_roulette == NULL)
    {
        create();
    }
    return m_soccer_roulette;
}
// -----------------------------------------------------------------------------
SoccerRoulette::SoccerRoulette()
{
    m_current_field_index = 0;
    m_minimap_running = false;
    m_minimap_socket = -1;
    loadFieldsFromConfig();
    loadTeamsFromXML();
}
// -----------------------------------------------------------------------------
SoccerRoulette::~SoccerRoulette()
{
    stopMinimapExport();
}
// -----------------------------------------------------------------------------
bool SoccerRoulette::isEnabled() const
{
    return ServerConfig::m_soccer_roulette;
}
// -----------------------------------------------------------------------------
void SoccerRoulette::loadFieldsFromConfig()
{
    m_fields.clear();   
    std::string fields_str = ServerConfig::m_soccer_roulette_fields;
    std::vector<std::string> fields = StringUtils::split(fields_str, ',');
    for (const std::string& field : fields)
    {
        std::string trimmed = StringUtils::removeWhitespaces(field);
        if (!trimmed.empty())
        {
            m_fields.push_back(trimmed);
        }
    }
    // if there are no fields specified, use those default fields
    if (m_fields.empty())
    {
        m_fields = {"icy_soccer_field", "soccer_field", "addon_green-field", "addon_huge"};
    }
    
    Log::info("SoccerRoulette", "Loaded %d fields from configuration", m_fields.size());
}

// -----------------------------------------------------------------------------
void SoccerRoulette::loadTeamsFromXML()
{
    m_player_teams.clear();
    
    if (ServerConfig::m_teams_xml_path.c_str()[0] == '\0')
    {
        Log::info("SoccerRoulette", "No XML path specified");
        return;
    }
    
    try
    {
        // check if the file exists
        if (!file_manager->fileExists(ServerConfig::m_teams_xml_path))
        {
            Log::warn("SoccerRoulette", "Teams XML file %s does not exist.",
                      ServerConfig::m_teams_xml_path.c_str());
            return;
        }
        
        // load the file
        XMLNode* root = file_manager->createXMLTree(ServerConfig::m_teams_xml_path);
        if (!root || root->getName() != "teams")
        {
            Log::error("SoccerRoulette", "Invalid teams XML file format.");
            if (root) delete root;
            return;
        }
        
        // process teams
        for (unsigned int i = 0; i < root->getNumNodes(); i++)
        {
            const XMLNode* team_node = root->getNode(i);
            if (team_node->getName() != "team")
                continue;
            std::string team_name;
            team_node->get("name", &team_name);
            for (unsigned int j = 0; j < team_node->getNumNodes(); j++)
            {
                const XMLNode* player_node = team_node->getNode(j);
                if (player_node->getName() != "player")
                    continue;
                std::string player;
                player_node->get("name", &player);
                
                // store teams
                m_player_teams[player] = team_name;
                // log the teams to the console
                Log::info("SoccerRoulette", "Team assignment: %s -> %s",
                          player.c_str(), team_name.c_str());
            }
        }
        
        delete root;
        Log::info("SoccerRoulette", "Teams loaded from %s",
                  ServerConfig::m_teams_xml_path.c_str());
    }
    catch (const std::exception& e)
    {
        Log::error("SoccerRoulette", "Error loading teams: %s", e.what());
    }
}

// -----------------------------------------------------------------------------
// this is gonna be used if someone with at least REFEREE
// rights uses /soccerroulette|sr reload
void SoccerRoulette::reload()
{
    m_current_field_index = 0;
    loadFieldsFromConfig();
    loadTeamsFromXML();
}

// -----------------------------------------------------------------------------
void SoccerRoulette::resetIndex()
{
    m_current_field_index = 0;
    Log::info("SoccerRoulette", "Field index reset to 0");
}

// -----------------------------------------------------------------------------
std::string SoccerRoulette::getNextField()
{
    if (m_fields.empty())
    {
        return ""; // if no fields specified
    }
    
    std::string current_field = m_fields[m_current_field_index];
    m_current_field_index = (m_current_field_index + 1) % m_fields.size();
    
    Log::info("SoccerRoulette", "Next field: %s (index %d/%d)",
               current_field.c_str(), m_current_field_index, m_fields.size());
    
    return current_field;
}

// -----------------------------------------------------------------------------
std::string SoccerRoulette::getCurrentField() const
{
    if (m_fields.empty())
    {
        return "icy_soccer_field"; // icy_soccer_field is default track
    }
    
    return m_fields[m_current_field_index];
}

// -----------------------------------------------------------------------------
void SoccerRoulette::addField(const std::string& field)
{
    if (std::find(m_fields.begin(), m_fields.end(), field) == m_fields.end())
    {
        m_fields.push_back(field);
        // update config
        std::string fields_str;
        for (size_t i = 0; i < m_fields.size(); i++)
        {
            fields_str += m_fields[i];
            if (i < m_fields.size() - 1)
                fields_str += ",";
        }
        ServerConfig::m_soccer_roulette_fields = fields_str;
        Log::info("SoccerRoulette", "Added field %s to roulette", field.c_str());
    }
}

// -----------------------------------------------------------------------------
// remove field
void SoccerRoulette::removeField(const std::string& field)
{
    auto it = std::find(m_fields.begin(), m_fields.end(), field);
    if (it != m_fields.end())
    {
        m_fields.erase(it);
        // update config
        std::string fields_str;
        for (size_t i = 0; i < m_fields.size(); i++)
        {
            fields_str += m_fields[i];
            if (i < m_fields.size() - 1)
                fields_str += ",";
        }
        ServerConfig::m_soccer_roulette_fields = fields_str;
        // reset index
        if (m_current_field_index >= (int)m_fields.size())
        {
            m_current_field_index = 0;
        }
        
        Log::info("SoccerRoulette", "Removed field %s from roulette", field.c_str());
    }
}

// -----------------------------------------------------------------------------
// get team for player
std::string SoccerRoulette::getTeamForPlayer(const std::string& player_name) const
{
    auto it = m_player_teams.find(player_name);
    if (it != m_player_teams.end())
    {
        return it->second;
    }
    return "";
}

// -----------------------------------------------------------------------------
// assigns a team to a player
void SoccerRoulette::assignTeamToPlayer(const std::string& player_name, const std::string& team)
{
    m_player_teams[player_name] = team;
    Log::info("SoccerRoulette", "Assigned player %s to team %s", 
              player_name.c_str(), team.c_str());
}

// -----------------------------------------------------------------------------
// assigns a team to a player profile
void SoccerRoulette::assignTeamToPlayer(NetworkPlayerProfile* profile)
{
    if (!profile)
        return;
    
    std::string player_name = StringUtils::wideToUtf8(profile->getName());
    std::string team = getTeamForPlayer(player_name);
    
    if (team.empty())
    {
        // No team found, assign to KART_TEAM_NONE (spectator)
        profile->setTeam(KART_TEAM_NONE);
        Log::info("SoccerRoulette", "Player %s not found in teams XML, assigned as spectator",
                  player_name.c_str());
        return;
    }
    
    // assign a team when someone joins
    // team blue
    if (team == "blue")
    {
        profile->setTeam(KART_TEAM_BLUE);
        Log::info("SoccerRoulette", "Assigned %s to blue team", player_name.c_str());
    }
    // team red
    else if (team == "red")
    {
        profile->setTeam(KART_TEAM_RED);
        Log::info("SoccerRoulette", "Assigned %s to red team", player_name.c_str());
    }
    // if forced as a spectator
    else if (team == "spectator")
    {
        // Set the player as spectator
        profile->setTeam(KART_TEAM_NONE);
        Log::info("SoccerRoulette", "Assigned %s as spectator", player_name.c_str());
    }
    // Or else, KART_TEAM_NONE
    else
    {
        // Unknown team, assign as spectator
        profile->setTeam(KART_TEAM_NONE);
        Log::warn("SoccerRoulette", "Unknown team %s for player %s, assigned as spectator",
                  team.c_str(), player_name.c_str());
    }
}

// -----------------------------------------------------------------------------
// get players and teams from the xml file
std::string SoccerRoulette::getTeamsInfo() const
{
    std::string result = "Soccer Roulette Teams:\n";
    // group players
    std::map<std::string, std::vector<std::string>> teams_players;
    for (const auto& pair : m_player_teams)
    {
        teams_players[pair.second].push_back(pair.first);
    }
    for (const auto& team_pair : teams_players)
    {
        result += "Team " + team_pair.first + ": ";
        for (size_t i = 0; i < team_pair.second.size(); i++)
        {
            result += team_pair.second[i];
            if (i < team_pair.second.size() - 1)
                result += ", ";
        }
        result += "\n";
    }
    if (teams_players.empty())
    {
        result += "No teams defined in XML file.\n";
    }
    return result;
}

// -----------------------------------------------------------------------------
// This will give 1 nitro bottle.
// This is used at the start of each game.
void SoccerRoulette::giveNitroToAll()
{
    if (!World::getWorld())
        return;
    Log::info("SoccerRoulette", "Giving nitro to all players at game start");
   
    for (unsigned int i = 0; i < RaceManager::get()->getNumPlayers(); i++)
    {
        AbstractKart* kart = World::getWorld()->getKart(i);
        if (!kart || kart->isEliminated())
            continue;
        
        // give all the players a boost bottle 
        kart->setEnergy(3.0f);
    }
}

// -----------------------------------------------------------------------------
bool SoccerRoulette::checkRequiredAddons(STKPeer* peer, std::string& error_msg)
{
    if (!peer)
    {
        error_msg = "Invalid peer";
        return false;
    }
    // get client assets
    const auto& client_assets = peer->getClientAssets().second;
    std::vector<std::string> missing_required;
    for (const std::string& field : m_fields)
    {
        if (client_assets.find(field) == client_assets.end())
        {
            missing_required.push_back(field);
        }
    }
    if (!missing_required.empty())
    {
        std::string missing_fields_str;
        for (size_t i = 0; i < missing_required.size(); i++)
        {
            missing_fields_str += missing_required[i];
            if (i < missing_required.size() - 1)
                missing_fields_str += ", ";
        }
       
        error_msg = StringUtils::insertValues(
            "You need to install the following required soccer fields: %s",
            missing_fields_str.c_str());
        return false;
    }
    
    return true;
}

// -----------------------------------------------------------------------------
// Minimap functions
std::string SoccerRoulette::m_default_minimap_server = "127.0.0.1";
int SoccerRoulette::m_default_minimap_port = 9876;
bool SoccerRoulette::m_enable_minimap_export = true;

void SoccerRoulette::startMinimapExport(const std::string& server_ip, int server_port, int update_interval_ms)
{
    stopMinimapExport();
    m_minimap_server_ip = server_ip;
    m_minimap_server_port = server_port;
    m_minimap_update_interval_ms = update_interval_ms;
    m_minimap_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_minimap_socket < 0)
    {
        Log::error("SoccerRoulette", "Failed to create minimap socket");
        return;
    }
    m_minimap_running = true;
    m_minimap_thread = std::thread(&SoccerRoulette::minimapExportThread, this);
    Log::info("SoccerRoulette", "Started minimap export to %s:%d (interval: %dms)",
             m_minimap_server_ip.c_str(), m_minimap_server_port, m_minimap_update_interval_ms);
}

// -----------------------------------------------------------------------------
void SoccerRoulette::stopMinimapExport()
{
    if (m_minimap_running)
    {
        m_minimap_running = false;
        if (m_minimap_thread.joinable())
            m_minimap_thread.join();
        
        if (m_minimap_socket >= 0)
        {
            close(m_minimap_socket);
            m_minimap_socket = -1;
        }
        
        Log::info("SoccerRoulette", "Stopped minimap export");
    }
}

// -----------------------------------------------------------------------------
void SoccerRoulette::minimapExportThread()
{
    while (m_minimap_running)
    {
        exportMinimapData();
        std::this_thread::sleep_for(std::chrono::milliseconds(m_minimap_update_interval_ms));
    }
}
// ----------------------------------------------------------------------------
// This function exports data to the udp server
void SoccerRoulette::exportMinimapData()
{
    if (!World::getWorld())
        return;
    try
    {
        std::stringstream json;
        json << "{";
        json << "\"timestamp\":" << StkTime::getTimeSinceEpoch() << ",";
        SoccerWorld* soccer_world = dynamic_cast<SoccerWorld*>(World::getWorld());
        if (soccer_world)
        {
            const Vec3& ball_pos = soccer_world->getBallPosition();
            json << "\"ball\":{\"x\":" << ball_pos.getX()
                  << ",\"y\":" << ball_pos.getY()
                  << ",\"z\":" << ball_pos.getZ() << "},";
            json << "\"score\":{\"red\":" << soccer_world->getScore(KART_TEAM_RED)
                 << ",\"blue\":" << soccer_world->getScore(KART_TEAM_BLUE) << "},";
        }
        json << "\"karts\":[";
        bool first_kart = true;
        for (unsigned int i = 0; i < World::getWorld()->getNumKarts(); i++)
        {
            AbstractKart* kart = World::getWorld()->getKart(i);
            if (!kart)
            {
                continue;
            }
            if (kart->isEliminated())
            {
                continue;
            }
            if (!first_kart)
                json << ",";
            first_kart = false;
            const Vec3& pos = kart->getXYZ();
            int team = 0;
            if (soccer_world)
            {
                team = (int)soccer_world->getKartTeam(kart->getWorldKartId());
            }
            std::string player_name = StringUtils::wideToUtf8(kart->getController()->getName());

            std::string escaped_name = "";
            for (char c : player_name) {
                if (c == '\\' || c == '"') {
                    escaped_name += '\\';
                }
                escaped_name += c;
            }
            json << "{\"id\":\"" << escaped_name
                 << "\",\"name\":\"" << escaped_name
                 << "\",\"team\":" << team
                 << ",\"x\":" << pos.getX()
                 << ",\"y\":" << pos.getY()
                 << ",\"z\":" << pos.getZ() << "}";
        }
        json << "]}";
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(m_minimap_server_port);
        inet_pton(AF_INET, m_minimap_server_ip.c_str(), &server_addr.sin_addr);
        std::string json_str = json.str();
        sendto(m_minimap_socket, json_str.c_str(), json_str.length(), 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr));
    }
    catch (const std::exception& e)
    {
        Log::error("SoccerRoulette", "Error exporting minimap data: %s", e.what());
    }
}

// ---------------------------------------------------------------------------------
void SoccerRoulette::resetFieldIndex()
{
	m_current_field_index = 0;
	Log::info("SoccerRoulette", "Field index resetted to 0");
}
// --------------------------------------------------------------------------------
void SoccerRoulette::calculateGameResult()
{
    try
    {
        std::string input_file_path = ServerConfig::m_gh_path_soccer_roulette.c_str();
        std::string output_file_path = ServerConfig::m_gh_path_soccer_roulette.c_str() + std::string(".json");
        std::ifstream input_file(input_file_path);
        if (!input_file.is_open())
        {
            Log::error("SoccerRoulette", "Failed to open goal history file for reading");
            return;
        }
        std::ofstream output_file(output_file_path);
        if (!output_file.is_open())
        {
            Log::error("SoccerRoulette", "Failed to open results file for writing");
            input_file.close();
            return;
        }
        std::string line;
        std::string game_timestamp;
        std::vector<std::string> game_lines;
        bool in_game = false;
        while (std::getline(input_file, line))
        {
            if (line.find("=== Soccer Game ") != std::string::npos)
            {
                if (in_game)
                {
                    game_lines.clear();
                }
                in_game = true;
                game_timestamp = line.substr(16, 19);
                game_lines.push_back(line);
            }
            else if (in_game)
            {
                game_lines.push_back(line);
            }
        }
        if (in_game && !game_lines.empty())
        {
            int red_score = 0;
            int blue_score = 0;
            std::string score_line;
            for (const auto& line : game_lines)
            {
                if (line.find("Final Score:") != std::string::npos)
                {
                    score_line = line;
                    size_t red_pos = line.find("Red ") + 4;
                    size_t dash_pos = line.find(" - ");
                    size_t blue_pos = dash_pos + 3;
                    size_t blue_end = line.find(" Blue");
                    if (red_pos != std::string::npos && dash_pos != std::string::npos && blue_pos != std::string::npos && blue_end != std::string::npos)
                    {
                        red_score = std::stoi(line.substr(red_pos, dash_pos - red_pos));
                        blue_score = std::stoi(line.substr(blue_pos, blue_end - blue_pos));
                    }
                    break;
                }
            }
            int red_points = 0;
            int blue_points = 0;
            if (red_score > blue_score)
            {
                red_points += 10;
            }
            else if (blue_score > red_score)
            {
                blue_points += 10;
            }
            std::map<std::string, int> red_players_points;
            std::map<std::string, int> blue_players_points;
            float fastest_speed = 0;
            std::string fastest_player;
            std::string fastest_team;
            bool in_goal_details = false;
            for (const auto& line : game_lines)
            {
                if (line.find("Goal Details:") != std::string::npos)
                {
                    in_goal_details = true;
                    continue;
                }
                if (in_goal_details && !line.empty() && line[0] != '=')
                {
                    size_t team_pos = line.find(" - ") + 3;
                    size_t team_end = line.find(" goal by ");
                    size_t player_pos = team_end + 9;
                    size_t player_end = line.find(" (");
                    size_t speed_pos = player_end + 2;
                    size_t speed_end = line.find(" km/h)");
                    if (team_pos != std::string::npos && team_end != std::string::npos && player_pos != std::string::npos && player_end != std::string::npos && speed_pos != std::string::npos && speed_end != std::string::npos)
                    {
                        std::string team = line.substr(team_pos, team_end - team_pos);
                        std::string player = line.substr(player_pos, player_end - player_pos);
                        float speed = std::stof(line.substr(speed_pos, speed_end - speed_pos));
                        if (speed > fastest_speed)
                        {
                            fastest_speed = speed;
                            fastest_player = player;
                            fastest_team = team;
                        }
                        if (team == "Red")
                        {
                            red_players_points[player] += 1;
                            red_points += 1;
                        }
                        else if (team == "Blue")
                        {
                            blue_players_points[player] += 1;
                            blue_points += 1;
                        }
                    }
                }
            }
            if (!fastest_team.empty())
            {
                if (fastest_team == "Red")
                {
                    red_players_points[fastest_player] += 3;
                    red_points += 3;
                }
                else if (fastest_team == "Blue")
                {
                    blue_players_points[fastest_player] += 3;
                    blue_points += 3;
                }
            }
            
            // Write JSON output
            output_file << "{" << std::endl;
            output_file << "  \"timestamp\": \"" << game_timestamp << "\"," << std::endl;
            output_file << "  \"score\": {" << std::endl;
            output_file << "    \"red\": " << red_score << "," << std::endl;
            output_file << "    \"blue\": " << blue_score << std::endl;
            output_file << "  }," << std::endl;
            output_file << "  \"points\": {" << std::endl;
            output_file << "    \"red\": " << red_points << "," << std::endl;
            output_file << "    \"blue\": " << blue_points << std::endl;
            output_file << "  }";
            
            if (fastest_speed > 0)
            {
                output_file << "," << std::endl;
                output_file << "  \"fastest_goal\": {" << std::endl;
                output_file << "    \"player\": \"" << fastest_player << "\"," << std::endl;
                output_file << "    \"team\": \"" << fastest_team << "\"," << std::endl;
                output_file << "    \"speed\": " << fastest_speed << std::endl;
                output_file << "  }" << std::endl;
            }
            else
            {
                output_file << std::endl;
            }
            
            output_file << "}" << std::endl;
        }
        
        input_file.close();
        output_file.close();
        Log::info("SoccerRoulette", "Game results calculated and saved to JSON file");
    }
    catch (const std::exception& e)
    {
        Log::error("SoccerRoulette", "Exception while calculating game results: %s", e.what());
    }
}
void SoccerRoulette::kickPlayer(const std::string& player_name, const std::shared_ptr<STKPeer>& kicker_peer)
{
    auto sl = LobbyProtocol::get<ServerLobby>();
    if (!sl)
    {
        Log::error("SoccerRoulette", "ServerLobby not available");
        return;
    }
    std::shared_ptr<STKPeer> player_peer = STKHost::get()->findPeerByName(
        StringUtils::utf8ToWide(player_name), true/*ignoreCase*/, true/*prefixOnly*/);

    if (player_name.empty() || !player_peer || player_peer->isAIPeer())
    {
        std::string error_msg = "Player '" + player_name + "' not found. Usage: /sr kick <player name>";
        std::shared_ptr<STKPeer> kicker_peer_copy = kicker_peer;
        sl->sendStringToPeer(error_msg, kicker_peer_copy);
        return;
    }
    std::string kicker_name = StringUtils::wideToUtf8(kicker_peer->getPlayerProfiles()[0]->getName());
    Log::info("SoccerRoulette", "Player %s kicked %s.",
              kicker_name.c_str(), player_name.c_str());
    std::string kick_msg = kicker_name + " kicked " + player_name;
    sl->sendStringToAllPeers(kick_msg);
    player_peer->kick();
    std::string confirm_msg = "You kicked player '" + player_name + "'";
    std::shared_ptr<STKPeer> kicker_peer_copy = kicker_peer;
    sl->sendStringToPeer(confirm_msg, kicker_peer_copy);
}

