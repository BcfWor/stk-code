#include "network/live_soccer.hpp"
#include "modes/world.hpp"
#include "modes/soccer_world.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/controller.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"
#include "utils/time.hpp"
#include "network/server_config.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <chrono>

// ------------------------------------------------------------------------------
LiveSoccer* LiveSoccer::m_instance = nullptr;

// ------------------------------------------------------------------------------
LiveSoccer::LiveSoccer()
{
    m_server_ip = ServerConfig::m_server_livesoccer_ip;
    m_server_port = std::stoi(ServerConfig::m_server_livesoccer_port.c_str());
    m_update_interval_ms = 2000;
    m_socket = -1;
    m_export_running = false;
    m_last_red_score = 0;
    m_last_blue_score = 0;
    m_is_active = false;
}

// ------------------------------------------------------------------------------
LiveSoccer::~LiveSoccer()
{
    stopExport();
}

// ------------------------------------------------------------------------------
LiveSoccer* LiveSoccer::getInstance()
{
    if (!m_instance)
    {
        m_instance = new LiveSoccer();
    }
    return m_instance;
}

// ------------------------------------------------------------------------------
void LiveSoccer::destroy()
{
    if (m_instance)
    {
        delete m_instance;
        m_instance = nullptr;
    }
}

// ------------------------------------------------------------------------------
void LiveSoccer::startExport(const std::string& server_ip, int server_port, int update_interval_ms)
{
    stopExport();
    
    m_server_ip = server_ip;
    m_server_port = server_port;
    m_update_interval_ms = update_interval_ms;
    
    m_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socket < 0)
    {
        Log::error("LiveSoccer", "Failed to create socket");
        return;
    }
    
    m_export_running = true;
    m_export_thread = std::thread(&LiveSoccer::liveExportThread, this);
    m_is_active = true;
    
    Log::info("LiveSoccer", "Live export started to %s:%d", m_server_ip.c_str(), m_server_port);
}

// ------------------------------------------------------------------------------
void LiveSoccer::stopExport()
{
    if (m_export_running)
    {
        m_export_running = false;
        if (m_export_thread.joinable())
            m_export_thread.join();
        
        if (m_socket >= 0)
        {
            close(m_socket);
            m_socket = -1;
        }
        
        m_is_active = false;
        Log::info("LiveSoccer", "Live export stopped");
    }
}

// ------------------------------------------------------------------------------
void LiveSoccer::resetGame()
{
    m_last_red_score = 0;
    m_last_blue_score = 0;
    
    Log::info("LiveSoccer", "Game reset - scores back to 0-0");
}

// ------------------------------------------------------------------------------
void LiveSoccer::updateGoal(const std::string& scorer_name, int team, int red_score, int blue_score, float game_time)
{
    if (!m_is_active || m_socket < 0)
        return;
    
    try
    {
        std::stringstream json;
        json << "{";
        json << "\"timestamp\":" << StkTime::getTimeSinceEpoch() << ",";
        json << "\"type\":\"goal\",";
        std::string escaped_name = "";
        for (char c : scorer_name) {
            if (c == '\\' || c == '"') {
                escaped_name += '\\';
            }
            escaped_name += c;
        }
        json << "\"scorer\":\"" << escaped_name << "\",";
        json << "\"team\":" << team << ",";
        json << "\"score\":{\"red\":" << red_score << ",\"blue\":" << blue_score << "},";
        json << "\"time\":" << game_time;
        json << "}";
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(m_server_port);
        inet_pton(AF_INET, m_server_ip.c_str(), &server_addr.sin_addr);
        
        std::string json_str = json.str();
        sendto(m_socket, json_str.c_str(), json_str.length(), 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        // Update internal scores
        m_last_red_score = red_score;
        m_last_blue_score = blue_score;
        
        Log::info("LiveSoccer", "Goal event sent: %s (Team %d) - Score: %d-%d", 
                 scorer_name.c_str(), team, red_score, blue_score);
    }
    catch (const std::exception& e)
    {
        Log::error("LiveSoccer", "Error sending goal event: %s", e.what());
    }
}

// ------------------------------------------------------------------------------
void LiveSoccer::updateGameState(int red_score, int blue_score, float game_time)
{
    m_last_red_score = red_score;
    m_last_blue_score = blue_score;
}

// ------------------------------------------------------------------------------
void LiveSoccer::liveExportThread()
{
    while (m_export_running)
    {
        exportLiveData();
        std::this_thread::sleep_for(std::chrono::milliseconds(m_update_interval_ms));
    }
}

// ------------------------------------------------------------------------------
void LiveSoccer::exportLiveData()
{
    if (!World::getWorld() || !m_is_active)
        return;

    SoccerWorld* soccer_world = dynamic_cast<SoccerWorld*>(World::getWorld());
    try
    {
        std::stringstream json;
        json << "{";
        json << "\"timestamp\":" << StkTime::getTimeSinceEpoch() << ",";
        json << "\"type\":\"update\",";
        int red_score = soccer_world->getScore(KART_TEAM_RED);
        int blue_score = soccer_world->getScore(KART_TEAM_BLUE);
        json << "\"score\":{\"red\":" << red_score << ",\"blue\":" << blue_score << "},";
        json << "\"time\":" << soccer_world->getTime() << ",";
        json << "\"players\":[";
        bool first_player = true;
        for (unsigned int i = 0; i < World::getWorld()->getNumKarts(); i++)
        {
            AbstractKart* kart = World::getWorld()->getKart(i);
            if (!kart || kart->isEliminated())
                continue;
            
            if (!first_player)
                json << ",";
            first_player = false;
            int team = (int)soccer_world->getKartTeam(kart->getWorldKartId());
            std::string player_name = StringUtils::wideToUtf8(kart->getController()->getName());
            std::string escaped_name = "";
            for (char c : player_name) 
	    {
                if (c == '\\' || c == '"') 
		{
                    escaped_name += '\\';
                }
                escaped_name += c;
            }   
            json << "{\"name\":\"" << escaped_name << "\",\"team\":" << team << "}";
        }
        json << "]}";
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(m_server_port);
        inet_pton(AF_INET, m_server_ip.c_str(), &server_addr.sin_addr);
        
        std::string json_str = json.str();
        sendto(m_socket, json_str.c_str(), json_str.length(), 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr));
    }
    catch (const std::exception& e)
    {
        Log::error("LiveSoccer", "Error exporting live data: %s", e.what());
    }
}
