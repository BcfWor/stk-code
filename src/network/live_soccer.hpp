#ifndef HEADER_LIVE_SOCCER_HPP
#define HEADER_LIVE_SOCCER_HPP

#include <string>
#include <thread>
#include <atomic>

class AbstractKart;

class LiveSoccer
{
private:
    std::string m_server_ip;
    int m_server_port;
    int m_update_interval_ms;
    int m_socket;

    std::atomic<bool> m_export_running;
    std::thread m_export_thread;
    int m_last_red_score;
    int m_last_blue_score;
    bool m_is_active;
    void liveExportThread();
    void exportLiveData();
    static LiveSoccer* m_instance;
    
public:
    LiveSoccer();
    ~LiveSoccer();
    
    static LiveSoccer* getInstance();
    static void destroy();
    void startExport(const std::string& server_ip = "127.0.0.1", int server_port = 9877, int update_interval_ms = 2000);
    void stopExport();
    void resetGame();
    void updateGoal(const std::string& scorer_name, int team, int red_score, int blue_score, float game_time);
    void updateGameState(int red_score, int blue_score, float game_time);
    bool isActive() const { return m_is_active; }
};

#endif

