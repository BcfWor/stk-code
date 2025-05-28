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

// File moved from database_connector.cpp

#include "network/moderation_toolkit/server_permission_level.hpp"
#ifdef ENABLE_SQLITE3

#include "network/database/sqlite_database.hpp"

#include "network/network_player_profile.hpp"
#include "network/server_config.hpp"
#include "network/socket_address.hpp"
#include "network/stk_host.hpp"
#include "network/stk_ipv6.hpp"
#include "network/stk_peer.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"

#include <iostream>

//-----------------------------------------------------------------------------
/** Prints "?" to the output stream and saves the Binder object to the
 *   corresponding BinderCollection so that it can produce bind function later
 *   When we invoke StringUtils::insertValues with a Binder argument, the
 *   implementation of insertValues ensures that this function is invoked for
 *   all Binder arguments from left to right.
 */
std::ostream& operator << (std::ostream& os, const Binder& binder)
{
    os << "?";
    binder.m_collection.lock()->m_binders.emplace_back(std::make_shared<Binder>(binder));
    return os;
}   // operator << (Binder)

//-----------------------------------------------------------------------------
/** Returns a bind function that should be used inside an easySQLQuery. As the
 *   Binder objects are already ordered in a correct way, the indices just go
 *   from 1 upwards. Depending on a particular Binder, we can also bind NULL
 *   instead of a string.
 */
std::function<void(sqlite3_stmt* stmt)> BinderCollection::getBindFunction() const
{
    auto binders = m_binders;
    return [binders](sqlite3_stmt* stmt)
    {
        int idx = 1;
        for (std::shared_ptr<Binder> binder: binders)
        {
            if (binder)
            {
                // SQLITE_TRANSIENT to copy string
                if (binder->m_use_null_if_empty && binder->m_value.empty())
                {
                    if (sqlite3_bind_null(stmt, idx) != SQLITE_OK)
                    {
                        Log::error("easySQLQuery", "Failed to bind NULL for %s.",
                            binder->m_name.c_str());
                    }
                }
                else
                {
                    if (sqlite3_bind_text(stmt, idx, binder->m_value.c_str(),
                        -1, SQLITE_TRANSIENT) != SQLITE_OK)
                    {
                        Log::error("easySQLQuery", "Failed to bind %s as %s.",
                            binder->m_value.c_str(), binder->m_name.c_str());
                    }
                }
            }
            ++idx;
        }
    };
}   // BinderCollection::getBindFunction

SQLiteDatabase::SQLiteDatabase()
{
    m_db = NULL;
    m_ip_ban_table_exists = false;
    m_ipv6_ban_table_exists = false;
    m_online_id_ban_table_exists = false;
    m_ip_geolocation_table_exists = false;
    m_ipv6_geolocation_table_exists = false;
    m_player_reports_table_exists = false;
    init();
}
//-----------------------------------------------------------------------------
/** Opens the database, sets its busy handler and variables related to it. */
void SQLiteDatabase::init()
{
    m_last_poll_db_time = StkTime::getMonoTimeMs();
    if (!ServerConfig::m_sql_management)
        return;
    const std::string& path = ServerConfig::getConfigDirectory() + "/" +
        ServerConfig::m_database_file.c_str();
    int ret = sqlite3_open_v2(path.c_str(), &m_db,
        SQLITE_OPEN_SHAREDCACHE | SQLITE_OPEN_FULLMUTEX |
        SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK)
    {
        Log::error("DatabaseConnector", "Cannot open database: %s.",
            sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = NULL;
        return;
    }
    sqlite3_busy_handler(m_db, [](void* data, int retry)
        {
            int retry_count = ServerConfig::m_database_timeout / 100;
            if (retry < retry_count)
            {
                sqlite3_sleep(100);
                // Return non-zero to let caller retry again
                return 1;
            }
            // Return zero to let caller return SQLITE_BUSY immediately
            return 0;
        }, NULL);
    sqlite3_create_function(m_db, "insideIPv6CIDR", 2, SQLITE_UTF8, NULL,
        &insideIPv6CIDRSQL, NULL, NULL);
    sqlite3_create_function(m_db, "upperIPv6", 1, SQLITE_UTF8, NULL,
        &upperIPv6SQL, NULL, NULL);
    checkTableExists(ServerConfig::m_ip_ban_table, m_ip_ban_table_exists);
    checkTableExists(ServerConfig::m_ipv6_ban_table, m_ipv6_ban_table_exists);
    checkTableExists(ServerConfig::m_online_id_ban_table,
        m_online_id_ban_table_exists);
    checkTableExists(ServerConfig::m_player_reports_table,
        m_player_reports_table_exists);
    checkTableExists(ServerConfig::m_ip_geolocation_table,
        m_ip_geolocation_table_exists);
    checkTableExists(ServerConfig::m_ipv6_geolocation_table,
        m_ipv6_geolocation_table_exists);
}   // initDatabase

//-----------------------------------------------------------------------------
/** Closes the database. */
void SQLiteDatabase::finalize()
{
    auto peers = STKHost::get()->getPeers();
    for (auto& peer : peers)
        writeDisconnectInfoTable(peer.get());
    if (m_db != NULL)
        sqlite3_close(m_db);
    m_db = NULL;
}   // destroyDatabase
SQLiteDatabase::~SQLiteDatabase()
{
    if (m_db != NULL)
        finalize();
}

//-----------------------------------------------------------------------------
/** Runs simple query with optional bind function. If output vector pointer is
 *   not (default) nullptr, then the output is written there.
 *  \param query The SQL query with '?'-placeholders for values to bind.
 *  \param output The 2D vector for output rows. If nullptr, the query output
 *                is ignored.
 *  \param bind_function The function for binding missing values.
 *  \return True if no error occurs.
 */
bool SQLiteDatabase::easySQLQuery(
       const std::string& query, std::vector<std::vector<std::string>>* output,
                         std::function<void(sqlite3_stmt* stmt)> bind_function,
                                                  std::string null_value) const
{
    if (!m_db)
        return false;
    sqlite3_stmt* stmt = NULL;
    int ret = sqlite3_prepare_v2(m_db, query.c_str(), -1, &stmt, 0);
    if (ret == SQLITE_OK)
    {
        if (bind_function)
            bind_function(stmt);
        ret = sqlite3_step(stmt);
        if (output)
        {
            output->clear();
            while (ret == SQLITE_ROW)
            {
                output->emplace_back();
                int columns = sqlite3_column_count(stmt);
                for (int i = 0; i < columns; ++i)
                {
                    const char* value = (char*)sqlite3_column_text(stmt, i);
                    if (value == nullptr)
                        output->back().push_back(null_value);
                    else
                        output->back().push_back(std::string(value));
                }
                ret = sqlite3_step(stmt);
            }
        }
        ret = sqlite3_finalize(stmt);
        if (ret != SQLITE_OK)
        {
            Log::error("DatabaseConnector",
                "Error finalize database for easy query %s: %s",
                query.c_str(), sqlite3_errmsg(m_db));
            return false;
        }
    }
    else
    {
        Log::error("DatabaseConnector",
            "Error preparing database for easy query %s: %s",
            query.c_str(), sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}   // easySQLQuery

//-----------------------------------------------------------------------------
/** Performs a query to determine if a certain table exists.
 *  \param table The searched name.
 *  \param result The output value.
 */
void SQLiteDatabase::checkTableExists(const std::string& table, bool& result)
{
    if (!m_db)
        return;
    result = false;
    if (!table.empty())
    {
        std::string query = StringUtils::insertValues(
            "SELECT count(type) FROM sqlite_master "
            "WHERE type='table' AND name='%s';", table.c_str());

        std::vector<std::vector<std::string>> output;
        if (easySQLQuery(query, &output) && !output.empty())
        {
            int number;
            if (StringUtils::fromString(output[0][0], number) && number == 1)
            {
                Log::info("DatabaseConnector", "Table named %s will be used.",
                    table.c_str());
                result = true;
            }
        }
    }
    if (!result && !table.empty())
    {
        Log::warn("DatabaseConnector", "Table named %s not found in database.",
            table.c_str());
    }
}   // checkTableExists

//-----------------------------------------------------------------------------
/** Queries the database's IP mapping to determine the country code for an
 *   address.
 *  \param addr Queried address.
 *  \return A country code string if the address is found in the mapping,
 *          and an empty string otherwise.
 */
std::string SQLiteDatabase::ip2Country(const SocketAddress& addr) const
{
    if (!m_db || !m_ip_geolocation_table_exists || addr.isLAN())
        return "";

    std::string cc_code;
    std::string query = StringUtils::insertValues(
        "SELECT country_code FROM %s "
        "WHERE `ip_start` <= %d AND `ip_end` >= %d "
        "ORDER BY `ip_start` DESC LIMIT 1;",
        ServerConfig::m_ip_geolocation_table.c_str(), addr.getIP(),
        addr.getIP());

    std::vector<std::vector<std::string>> output;
    if (easySQLQuery(query, &output) && !output.empty())
    {
        cc_code = output[0][0];
    }
    return cc_code;
}   // ip2Country

//-----------------------------------------------------------------------------
/** Queries the database's IPv6 mapping to determine the country code for an
 *   address.
 *  \param addr Queried address.
 *  \return A country code string if the address is found in the mapping,
 *          and an empty string otherwise.
 */
std::string SQLiteDatabase::ipv62Country(const SocketAddress& addr) const
{
    if (!m_db || !m_ipv6_geolocation_table_exists)
        return "";

    std::string cc_code;
    const std::string& ipv6 = addr.toString(false/*show_port*/);
    std::string query = StringUtils::insertValues(
        "SELECT country_code FROM %s "
        "WHERE `ip_start` <= upperIPv6(\"%s\") AND `ip_end` >= upperIPv6(\"%s\") "
        "ORDER BY `ip_start` DESC LIMIT 1;",
        ServerConfig::m_ipv6_geolocation_table.c_str(), ipv6.c_str(),
        ipv6.c_str());

    std::vector<std::vector<std::string>> output;
    if (easySQLQuery(query, &output) && !output.empty())
    {
        cc_code = output[0][0];
    }
    return cc_code;
}   // ipv62Country

// ----------------------------------------------------------------------------
/** A function invoked within SQLite */
void SQLiteDatabase::upperIPv6SQL(sqlite3_context* context, int argc,
                         sqlite3_value** argv)
{
    if (argc != 1)
    {
        sqlite3_result_int64(context, 0);
        return;
    }

    char* ipv6 = (char*)sqlite3_value_text(argv[0]);
    if (ipv6 == NULL)
    {
        sqlite3_result_int64(context, 0);
        return;
    }
    sqlite3_result_int64(context, upperIPv6(ipv6));
}

// ----------------------------------------------------------------------------
/** A function that checks within SQLite whether an IPv6 address (argv[1])
 *   is located within a specified block (argv[0]) of IPv6 addresses.
 */
void SQLiteDatabase::insideIPv6CIDRSQL(sqlite3_context* context, int argc,
                       sqlite3_value** argv)
{
    if (argc != 2)
    {
        sqlite3_result_int(context, 0);
        return;
    }

    char* ipv6_cidr = (char*)sqlite3_value_text(argv[0]);
    char* ipv6_in = (char*)sqlite3_value_text(argv[1]);
    if (ipv6_cidr == NULL || ipv6_in == NULL)
    {
        sqlite3_result_int(context, 0);
        return;
    }
    sqlite3_result_int(context, insideIPv6CIDR(ipv6_cidr, ipv6_in));
}   // insideIPv6CIDRSQL

// ----------------------------------------------------------------------------
/*
Copy below code so it can be use as loadable extension to be used in sqlite3
command interface (together with andIPv6 and insideIPv6CIDR from stk_ipv6)

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
// ----------------------------------------------------------------------------
sqlite3_extension_init(sqlite3* db, char** pzErrMsg,
                       const sqlite3_api_routines* pApi)
{
    SQLITE_EXTENSION_INIT2(pApi)
    sqlite3_create_function(db, "insideIPv6CIDR", 2, SQLITE_UTF8, NULL,
        insideIPv6CIDRSQL, NULL, NULL);
    sqlite3_create_function(db, "upperIPv6", 1, SQLITE_UTF8,  0, upperIPv6SQL,
        0, 0);
    return 0;
}   // sqlite3_extension_init
*/

//-----------------------------------------------------------------------------
/** When a peer disconnects from the server, this function saves to the
 *   database peer's disconnection time and statistics (ping and packet loss).
 *  \param peer Disconnecting peer.
 */
void SQLiteDatabase::writeDisconnectInfoTable(STKPeer* peer)
{
    if (m_server_stats_table.empty())
        return;
    std::string query = StringUtils::insertValues(
        "UPDATE %s SET disconnected_time = datetime('now'), "
        "ping = %d, packet_loss = %d "
        "WHERE host_id = %u;", m_server_stats_table.c_str(),
        peer->getAveragePing(), peer->getPacketLoss(),
        peer->getHostId());
    easySQLQuery(query);
}   // writeDisconnectInfoTable

//-----------------------------------------------------------------------------
/** Creates necessary tables and views if they don't exist yet in the database.
 *   As the function is invoked during the server launch, it also updates rows
 *   related to players whose disconnection time wasn't written, and loads
 *   last used host id.
 */
void SQLiteDatabase::initServerStatsTable()
{
    if (!ServerConfig::m_sql_management || !m_db)
        return;
    std::string table_name = std::string("v") +
        StringUtils::toString(ServerConfig::m_server_db_version) + "_" +
        ServerConfig::m_server_uid + "_stats";

    std::ostringstream oss;
    oss << "CREATE TABLE IF NOT EXISTS " << table_name << " (\n"
        "    host_id INTEGER UNSIGNED NOT NULL PRIMARY KEY, -- Unique host id in STKHost of each connection session for a STKPeer\n"
        "    ip INTEGER UNSIGNED NOT NULL, -- IP decimal of host\n";
    if (ServerConfig::m_ipv6_connection)
        oss << "    ipv6 TEXT NOT NULL DEFAULT '', -- IPv6 (if exists) in string of host\n";
    oss << "    port INTEGER UNSIGNED NOT NULL, -- Port of host\n"
        "    online_id INTEGER UNSIGNED NOT NULL, -- Online if of the host (0 for offline account)\n"
        "    username TEXT NOT NULL, -- First player name in the host (if the host has splitscreen player)\n"
        "    player_num INTEGER UNSIGNED NOT NULL, -- Number of player(s) from the host, more than 1 if it has splitscreen player\n"
        "    country_code TEXT NULL DEFAULT NULL, -- 2-letter country code of the host\n"
        "    version TEXT NOT NULL, -- SuperTuxKart version of the host\n"
        "    os TEXT NOT NULL, -- Operating system of the host\n"
        "    connected_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Time when connected\n"
        "    disconnected_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, -- Time when disconnected (saved when disconnected)\n"
        "    ping INTEGER UNSIGNED NOT NULL DEFAULT 0, -- Ping of the host\n"
        "    packet_loss INTEGER NOT NULL DEFAULT 0 -- Mean packet loss count from ENet (saved when disconnected)\n"
        ") WITHOUT ROWID;";
    std::string query = oss.str();

    if (easySQLQuery(query))
        m_server_stats_table = table_name;

    if (m_server_stats_table.empty())
        return;

    // Extra default table _countries:
    // Server owner need to initialise this table himself, check NETWORKING.md
    std::string country_table_name = std::string("v") + StringUtils::toString(
        ServerConfig::m_server_db_version) + "_countries";
    query = StringUtils::insertValues(
        "CREATE TABLE IF NOT EXISTS %s (\n"
        "    country_code TEXT NOT NULL PRIMARY KEY UNIQUE, -- Unique 2-letter country code\n"
        "    country_flag TEXT NOT NULL, -- Unicode country flag representation of 2-letter country code\n"
        "    country_name TEXT NOT NULL -- Readable name of this country\n"
        ") WITHOUT ROWID;", country_table_name.c_str());
    easySQLQuery(query);

    // Default views:
    // _full_stats
    // Full stats with ip in human readable format and time played of each
    // players in minutes
    std::string full_stats_view_name = std::string("v") +
        StringUtils::toString(ServerConfig::m_server_db_version) + "_" +
        ServerConfig::m_server_uid + "_full_stats";
    oss.str("");
    oss << "CREATE VIEW IF NOT EXISTS " << full_stats_view_name << " AS\n"
        << "    SELECT host_id, ip,\n"
        << "    ((ip >> 24) & 255) ||'.'|| ((ip >> 16) & 255) ||'.'|| ((ip >>  8) & 255) ||'.'|| ((ip ) & 255) AS ip_readable,\n";
    if (ServerConfig::m_ipv6_connection)
        oss << "    ipv6,";
    oss << "    port, online_id, username, player_num,\n"
        << "    " << m_server_stats_table << ".country_code AS country_code, country_flag, country_name, version, os,\n"
        << "    ROUND((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0, 2) AS time_played,\n"
        << "    connected_time, disconnected_time, ping, packet_loss FROM " << m_server_stats_table << "\n"
        << "    LEFT JOIN " << country_table_name << " ON "
        <<      country_table_name << ".country_code = " << m_server_stats_table << ".country_code\n"
        << "    ORDER BY connected_time DESC;";
    query = oss.str();
    easySQLQuery(query);

    // _current_players
    // Current players in server with ip in human readable format and time
    // played of each players in minutes
    std::string current_players_view_name = std::string("v") +
        StringUtils::toString(ServerConfig::m_server_db_version) + "_" +
        ServerConfig::m_server_uid + "_current_players";
    oss.str("");
    oss.clear();
    oss << "CREATE VIEW IF NOT EXISTS " << current_players_view_name << " AS\n"
        << "    SELECT host_id, ip,\n"
        << "    ((ip >> 24) & 255) ||'.'|| ((ip >> 16) & 255) ||'.'|| ((ip >>  8) & 255) ||'.'|| ((ip ) & 255) AS ip_readable,\n";
    if (ServerConfig::m_ipv6_connection)
        oss << "    ipv6,";
    oss << "    port, online_id, username, player_num,\n"
        << "    " << m_server_stats_table << ".country_code AS country_code, country_flag, country_name, version, os,\n"
        << "    ROUND((STRFTIME(\"%s\", 'now') - STRFTIME(\"%s\", connected_time)) / 60.0, 2) AS time_played,\n"
        << "    connected_time, ping FROM " << m_server_stats_table << "\n"
        << "    LEFT JOIN " << country_table_name << " ON "
        <<      country_table_name << ".country_code = " << m_server_stats_table << ".country_code\n"
        << "    WHERE connected_time = disconnected_time;";
    query = oss.str();
    easySQLQuery(query);

    // _player_stats
    // All players with online id and username with their time played stats
    // in this server since creation of this database
    // If sqlite supports window functions (since 3.25), it will include last session player info (ip, country, ping...)
    std::string player_stats_view_name = std::string("v") +
        StringUtils::toString(ServerConfig::m_server_db_version) + "_" +
        ServerConfig::m_server_uid + "_player_stats";
    oss.str("");
    oss.clear();
    if (sqlite3_libversion_number() < 3025000)
    {
        oss << "CREATE VIEW IF NOT EXISTS " << player_stats_view_name << " AS\n"
            << "    SELECT online_id, username, COUNT(online_id) AS num_connections,\n"
            << "    MIN(connected_time) AS first_connected_time,\n"
            << "    MAX(connected_time) AS last_connected_time,\n"
            << "    ROUND(SUM((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS total_time_played,\n"
            << "    ROUND(AVG((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS average_time_played,\n"
            << "    ROUND(MIN((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS min_time_played,\n"
            << "    ROUND(MAX((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS max_time_played\n"
            << "    FROM " << m_server_stats_table << "\n"
            << "    WHERE online_id != 0 GROUP BY online_id ORDER BY num_connections DESC;";
    }
    else
    {
        oss << "CREATE VIEW IF NOT EXISTS " << player_stats_view_name << " AS\n"
            << "    SELECT a.online_id, a.username, a.ip, a.ip_readable,\n";
        if (ServerConfig::m_ipv6_connection)
            oss << "    a.ipv6,";
        oss << "    a.port, a.player_num,\n"
            << "    a.country_code, a.country_flag, a.country_name, a.version, a.os, a.ping, a.packet_loss,\n"
            << "    b.num_connections, b.first_connected_time, b.first_disconnected_time,\n"
            << "    a.connected_time AS last_connected_time, a.disconnected_time AS last_disconnected_time,\n"
            << "    a.time_played AS last_time_played, b.total_time_played, b.average_time_played,\n"
            << "    b.min_time_played, b.max_time_played\n"
            << "    FROM\n"
            << "    (\n"
            << "        SELECT *,\n"
            << "        ROW_NUMBER() OVER\n"
            << "        (\n"
            << "            PARTITION BY online_id\n"
            << "            ORDER BY connected_time DESC\n"
            << "        ) RowNum\n"
            << "        FROM " << full_stats_view_name << " where online_id != 0\n"
            << "    ) as a\n"
            << "    JOIN\n"
            << "    (\n"
            << "        SELECT online_id, COUNT(online_id) AS num_connections,\n"
            << "        MIN(connected_time) AS first_connected_time,\n"
            << "        MIN(disconnected_time) AS first_disconnected_time,\n"
            << "        ROUND(SUM((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS total_time_played,\n"
            << "        ROUND(AVG((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS average_time_played,\n"
            << "        ROUND(MIN((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS min_time_played,\n"
            << "        ROUND(MAX((STRFTIME(\"%s\", disconnected_time) - STRFTIME(\"%s\", connected_time)) / 60.0), 2) AS max_time_played\n"
            << "        FROM " << m_server_stats_table << " WHERE online_id != 0 GROUP BY online_id\n"
            << "    ) AS b\n"
            << "    ON b.online_id = a.online_id\n"
            << "    WHERE RowNum = 1 ORDER BY num_connections DESC;\n";
    }
    query = oss.str();
    easySQLQuery(query);

    uint32_t last_host_id = 0;
    query = StringUtils::insertValues("SELECT MAX(host_id) FROM %s;",
        m_server_stats_table.c_str());

    std::vector<std::vector<std::string>> output;
    if (easySQLQuery(query, &output))
    {
        if (!output.empty() && !output[0].empty()
                && StringUtils::fromString(output[0][0], last_host_id))
        {
            Log::info("DatabaseConnector", "%u was last server session max host id.",
                last_host_id);
        }
    }
    else
    {
        m_server_stats_table = "";
    }

    STKHost::get()->setNextHostId(last_host_id);

    // Update disconnected time (if stk crashed it will not be written)
    query = StringUtils::insertValues(
        "UPDATE %s SET disconnected_time = datetime('now') "
        "WHERE connected_time = disconnected_time;",
        m_server_stats_table.c_str());
    easySQLQuery(query);
}   // initServerStatsTable

//-----------------------------------------------------------------------------
/** Writes a report of one player about another player.
 *  \param reporter Peer that sends the report.
 *  \param reporter_npp Player profile that sends the report.
 *  \param reporting Peer that is reported.
 *  \param reporting_npp Player profile that is reported.
 *  \param info The report message.
 *  \return True if the database query succeeded.
 */
bool SQLiteDatabase::writeReport(
       STKPeer* reporter, std::shared_ptr<NetworkPlayerProfile> reporter_npp,
       STKPeer* reporting, std::shared_ptr<NetworkPlayerProfile> reporting_npp,
       irr::core::stringw& info)
{
    std::string query;

    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();
    if (ServerConfig::m_ipv6_connection)
    {
        query = StringUtils::insertValues(
            "INSERT INTO %s "
            "(server_uid, reporter_ip, reporter_ipv6, reporter_online_id, reporter_username, "
            "info, reporting_ip, reporting_ipv6, reporting_online_id, reporting_username) "
            "VALUES (%s, %u, \"%s\", %u, %s, %s, %u, \"%s\", %u, %s);",
            ServerConfig::m_player_reports_table.c_str(),
            Binder(coll, ServerConfig::m_server_uid, "server_uid"),
            !reporter->getAddress().isIPv6() ? reporter->getAddress().getIP() : 0,
            reporter->getAddress().isIPv6() ? reporter->getAddress().toString(false) : "",
            reporter_npp->getOnlineId(),
            Binder(coll, StringUtils::wideToUtf8(reporter_npp->getName()), "reporter_name"),
            Binder(coll, StringUtils::wideToUtf8(info), "info"),
            !reporting->getAddress().isIPv6() ? reporting->getAddress().getIP() : 0,
            reporting->getAddress().isIPv6() ? reporting->getAddress().toString(false) : "",
            reporting_npp->getOnlineId(),
            Binder(coll, StringUtils::wideToUtf8(reporting_npp->getName()), "reporting_name")
        );
    }
    else
    {
        query = StringUtils::insertValues(
            "INSERT INTO %s "
            "(server_uid, reporter_ip, reporter_online_id, reporter_username, "
            "info, reporting_ip, reporting_online_id, reporting_username) "
            "VALUES (%s, %u, %u, %s, %s, %u, %u, %s);",
            ServerConfig::m_player_reports_table.c_str(),
            Binder(coll, ServerConfig::m_server_uid, "server_uid"),
            reporter->getAddress().getIP(),
            reporter_npp->getOnlineId(),
            Binder(coll, StringUtils::wideToUtf8(reporter_npp->getName()), "reporter_name"),
            Binder(coll, StringUtils::wideToUtf8(info), "info"),
            reporting->getAddress().getIP(),
            reporting_npp->getOnlineId(),
            Binder(coll, StringUtils::wideToUtf8(reporting_npp->getName()), "reporting_name")
        );
    }
    return easySQLQuery(query, nullptr, coll->getBindFunction());
}   // writeReport

//-----------------------------------------------------------------------------
/** Gets the rows from IPv4 ban table, either all of them (for polling
 *   purposes), or those describing a certain address (if only one peer has to
 *   be checked).
 *  \param ip The IP address to check the database for. If zero, all rows
 *            will be given.
 *  \return A vector of rows in the form of IpBanTableData structures.
 */
std::vector<AbstractDatabase::IpBanTableData>
SQLiteDatabase::getIpBanTableData(uint32_t ip) const
{
    std::vector<IpBanTableData> result;
    if (!m_ip_ban_table_exists)
    {
        return result;
    }
    bool single_ip = (ip != 0);
    std::ostringstream oss;
    oss << "SELECT rowid, ip_start, ip_end, reason, description FROM ";
    oss << (std::string)ServerConfig::m_ip_ban_table << " WHERE ";
    if (single_ip)
        oss << "ip_start <= " << ip << " AND ip_end >= " << ip << " AND ";
    oss << "datetime('now') > datetime(starting_time) AND "
        "(expired_days is NULL OR datetime"
        "(starting_time, '+'||expired_days||' days') > datetime('now'))";
    if (single_ip)
        oss << " LIMIT 1";
    oss << ";";
    std::string query = oss.str();

    std::vector<std::vector<std::string>> output;
    easySQLQuery(query, &output);

    for (std::vector<std::string>& row: output)
    {
        IpBanTableData element;
        if (!StringUtils::fromString(row[0], element.row_id))
            continue;
        if (!StringUtils::fromString(row[1], element.ip_start))
            continue;
        if (!StringUtils::fromString(row[2], element.ip_end))
            continue;
        element.reason = row[3];
        element.description = row[4];
        result.push_back(element);
    }
    return result;
}   // getIpBanTableData

//-----------------------------------------------------------------------------
/** For a peer that turned out to be banned by IPv4, this function increases
 *   the trigger count.
 *  \param ip_start Start of IP ban range corresponding to peer.
 *  \param ip_end End of IP ban range corresponding to peer.
 */
void SQLiteDatabase::increaseIpBanTriggerCount(uint32_t ip_start, uint32_t ip_end) const
{
    std::string query = StringUtils::insertValues(
        "UPDATE %s SET trigger_count = trigger_count + 1, "
        "last_trigger = datetime('now') "
        "WHERE ip_start = %u AND ip_end = %u;",
        ServerConfig::m_ip_ban_table.c_str(), ip_start, ip_end);
    easySQLQuery(query);
}   // getIpBanTableData

//-----------------------------------------------------------------------------
/** Gets the rows from IPv6 ban table, either all of them (for polling
 *   purposes), or those describing a certain address (if only one peer has to
 *   be checked).
 *  \param ip The IPv6 address to check the database for. If empty, all rows
 *            will be given.
 *  \return A vector of rows in the form of Ipv6BanTableData structures.
 */
std::vector<AbstractDatabase::Ipv6BanTableData>
SQLiteDatabase::getIpv6BanTableData(std::string ipv6) const
{
    std::vector<Ipv6BanTableData> result;
    if (!m_ipv6_ban_table_exists)
    {
        return result;
    }
    bool single_ip = !ipv6.empty();
    std::string query;
    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();

    query = StringUtils::insertValues(
        "SELECT rowid, ipv6_cidr, reason, description FROM %s WHERE ",
        ServerConfig::m_ipv6_ban_table.c_str()
    );
    if (single_ip)
        query += StringUtils::insertValues(
            "insideIPv6CIDR(ipv6_cidr, %s) = 1 AND ",
            Binder(coll, ipv6, "ipv6")
        );

    query += "datetime('now') > datetime(starting_time) AND "
        "(expired_days is NULL OR datetime"
        "(starting_time, '+'||expired_days||' days') > datetime('now'))";

    if (single_ip)
        query += " LIMIT 1;";

    std::vector<std::vector<std::string>> output;
    easySQLQuery(query, &output, coll->getBindFunction());

    for (std::vector<std::string>& row: output)
    {
        Ipv6BanTableData element;
        if (!StringUtils::fromString(row[0], element.row_id))
            continue;
        element.ipv6_cidr = row[1];
        element.reason = row[2];
        element.description = row[3];
        result.push_back(element);
    }
    return result;
}   // getIpv6BanTableData

//-----------------------------------------------------------------------------
/** For a peer that turned out to be banned by IPv6, this function increases
 *   the trigger count.
 *  \param ipv6_cidr Block of IPv6 addresses corresponding to the peer.
 */
void SQLiteDatabase::increaseIpv6BanTriggerCount(const std::string& ipv6_cidr) const
{
    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();
    std::string query = StringUtils::insertValues(
        "UPDATE %s SET trigger_count = trigger_count + 1, "
        "last_trigger = datetime('now') "
        "WHERE ipv6_cidr = %s;",
        ServerConfig::m_ipv6_ban_table.c_str(),
        Binder(coll, ipv6_cidr, "ipv6_cidr")
    );
    easySQLQuery(query, nullptr, coll->getBindFunction());
}   // increaseIpv6BanTriggerCount

//-----------------------------------------------------------------------------
/** Gets the rows from online id ban table, either all of them (for polling
 *   purposes), or those describing a certain online id (if only one peer has
 *   to be checked).
 *  \param online_id The online id to check the database for. If empty, all
 *                   rows will be given.
 *  \return A vector of rows in the form of OnlineIdBanTableData structures.
 */
std::vector<AbstractDatabase::OnlineIdBanTableData>
SQLiteDatabase::getOnlineIdBanTableData(uint32_t online_id) const
{
    std::vector<OnlineIdBanTableData> result;
    if (!m_online_id_ban_table_exists)
    {
        return result;
    }
    bool single_id = (online_id != 0);
    std::ostringstream oss;
    oss << "SELECT rowid, online_id, reason, description FROM ";
    oss << (std::string)ServerConfig::m_online_id_ban_table;
    oss << " WHERE ";
    if (single_id)
        oss << "online_id = " << online_id << " AND ";
    oss << "datetime('now') > datetime(starting_time) AND "
        "(expired_days is NULL OR datetime"
        "(starting_time, '+'||expired_days||' days') > datetime('now'))";
    if (single_id)
        oss << " LIMIT 1";
    oss << ";";
    std::string query = oss.str();
    sqlite3_exec(m_db, query.c_str(),
        [](void* ptr, int count, char** data, char** columns)
        {
            std::vector<OnlineIdBanTableData>* vec = (std::vector<OnlineIdBanTableData>*)ptr;
            OnlineIdBanTableData element;
            if (!StringUtils::fromString(data[0], element.row_id))
                return 0;
            if (!StringUtils::fromString(data[1], element.online_id))
                return 0;
            element.reason = std::string(data[2]);
            element.description = std::string(data[3]);
            vec->push_back(element);
            return 0;
        }, &result, NULL);
    return result;
}   // getOnlineIdBanTableData

//-----------------------------------------------------------------------------
/** For a peer that turned out to be banned by online id, this function
 *   increases the trigger count.
 *  \param online_id Online id of the peer.
 */
void SQLiteDatabase::increaseOnlineIdBanTriggerCount(uint32_t online_id) const
{
    std::string query = StringUtils::insertValues(
        "UPDATE %s SET trigger_count = trigger_count + 1, "
        "last_trigger = datetime('now') "
        "WHERE online_id = %u;",
        ServerConfig::m_online_id_ban_table.c_str(), online_id);
    easySQLQuery(query);
}   // increaseOnlineIdBanTriggerCount

//-----------------------------------------------------------------------------
/** Clears reports that are older than a certain number of days
 *   (specified in the server config).
 */
void SQLiteDatabase::clearOldReports()
{
    if (m_player_reports_table_exists &&
        ServerConfig::m_player_reports_expired_days != 0.0f)
    {
        std::string query = StringUtils::insertValues(
            "DELETE FROM %s "
            "WHERE datetime"
            "(reported_time, '+%f days') < datetime('now');",
            ServerConfig::m_player_reports_table.c_str(),
            ServerConfig::m_player_reports_expired_days);
        easySQLQuery(query);
    }
}   // clearOldReports

//-----------------------------------------------------------------------------
/** Sets disconnection times for those peers that already left the server, but
 *   whose disconnection times wasn't set yet.
 *  \param present_hosts List of online ids of present peers.
 */
void SQLiteDatabase::setDisconnectionTimes(std::vector<uint32_t>& present_hosts)
{
    if (!hasServerStatsTable())
        return;
    std::ostringstream oss;
        oss << "UPDATE " << m_server_stats_table
            << "    SET disconnected_time = datetime('now')"
            << "    WHERE connected_time = disconnected_time";
    if (present_hosts.empty())
    {
        oss << ";";
    }
    else
    {
        oss << " AND host_id NOT IN (";
        for (unsigned i = 0; i < present_hosts.size(); i++)
        {
            if (i > 0)
                oss << ",";
            oss << present_hosts[i];
        }
        oss << ");";
    }
    std::string query = oss.str();
    easySQLQuery(query);
}   // setDisconnectionTimes

//-----------------------------------------------------------------------------
/** Adds a specified IP address to the IPv4 ban table. Usually invoked from
 *   network console.
 *  \param addr Address to ban.
 */
void SQLiteDatabase::saveAddressToIpBanTable(const SocketAddress& addr)
{
    if (addr.isIPv6() || !m_db || !m_ip_ban_table_exists)
        return;

    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (ip_start, ip_end) "
        "VALUES (%u, %u);",
        ServerConfig::m_ip_ban_table.c_str(), addr.getIP(), addr.getIP());
    easySQLQuery(query);
}   // saveAddressToIpBanTable

//-----------------------------------------------------------------------------
/** Called when the player joins the server, inserts player info into database.
 *  \param peer The peer that joins.
 *  \param online_id Player's online id.
 *  \param player_count Number of players joining using a single peer.
 *  \param country_code Country code deduced by global or local IP mapping.
 */
void SQLiteDatabase::onPlayerJoinQueries(std::shared_ptr<STKPeer> peer,
        uint32_t online_id, unsigned player_count, const std::string& country_code)
{
    if (m_server_stats_table.empty() || peer->isAIPeer())
        return;
    std::string query;
    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();
    auto version_os = StringUtils::extractVersionOS(peer->getUserVersion());
    if (ServerConfig::m_ipv6_connection && peer->getAddress().isIPv6())
    {
        query = StringUtils::insertValues(
            "INSERT INTO %s "
            "(host_id, ip, ipv6, port, online_id, username, player_num, "
            "country_code, version, os, ping) "
            "VALUES (%u, 0, \"%s\", %u, %u, %s, %u, %s, %s, %s, %u);",
            m_server_stats_table.c_str(),
            peer->getHostId(),
            peer->getAddress().toString(false),
            peer->getAddress().getPort(),
            online_id,
            Binder(coll, StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName()), "player_name"),
            player_count,
            Binder(coll, country_code, "country_code", true),
            Binder(coll, version_os.first, "version"),
            Binder(coll, version_os.second, "os"),
            peer->getAveragePing()
        );
    }
    else
    {
        query = StringUtils::insertValues(
            "INSERT INTO %s "
            "(host_id, ip, port, online_id, username, player_num, "
            "country_code, version, os, ping) "
            "VALUES (%u, %u, %u, %u, %s, %u, %s, %s, %s, %u);",
            m_server_stats_table.c_str(),
            peer->getHostId(),
            peer->getAddress().getIP(),
            peer->getAddress().getPort(),
            online_id,
            Binder(coll, StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName()), "player_name"),
            player_count,
            Binder(coll, country_code, "country_code", true),
            Binder(coll, version_os.first, "version"),
            Binder(coll, version_os.second, "os"),
            peer->getAveragePing()
        );
    }
    easySQLQuery(query, nullptr, coll->getBindFunction());
}   // onPlayerJoinQueries

//-----------------------------------------------------------------------------
/** Prints all rows of the IPv4 ban table. Called from the network console. */
void SQLiteDatabase::listBanTable()
{
    if (!m_db)
        return;
    auto printer = [](void* data, int argc, char** argv, char** name)
    {
        for (int i = 0; i < argc; i++)
        {
            std::cout << name[i] << " = " << (argv[i] ? argv[i] : "NULL")
                << "\n";
        }
        std::cout << "\n";
        return 0;
    };
    if (m_ip_ban_table_exists)
    {
        std::string query = "SELECT * FROM ";
        query += ServerConfig::m_ip_ban_table;
        query += ";";
        std::cout << "IP ban list:\n";
        sqlite3_exec(m_db, query.c_str(), printer, NULL, NULL);
    }
    if (m_online_id_ban_table_exists)
    {
        std::string query = "SELECT * FROM ";
        query += ServerConfig::m_online_id_ban_table;
        query += ";";
        std::cout << "Online Id ban list:\n";
        sqlite3_exec(m_db, query.c_str(), printer, NULL, NULL);
    }
}   // listBanTable

int SQLiteDatabase::loadPermissionLevelForOID(const uint32_t online_id)
{
    int lvl = 0;
    char* errmsg;
    std::string query = StringUtils::insertValues(
        "SELECT level FROM %s WHERE online_id = %u;",
        ServerConfig::m_permissions_table.c_str(),
        online_id);
    sqlite3_exec(m_db, query.c_str(),
            [](void* ptr, int amount, char** data, char** columns) {
                int* target = (int*)ptr;
                *target = std::atoi(data[0]);
                return 0;
            }, &lvl, &errmsg);
    if (errmsg)
    {
        Log::error("DatabaseConnector", "loadPermissionLevelForOID failure: %s", errmsg);
        sqlite3_free(errmsg);
        return 0;
    }
    return lvl;
}
int SQLiteDatabase::loadPermissionLevelForUsername(const irr::core::stringw& name)
{
    if (!m_db || !m_permissions_table_exists)
        return PERM_PLAYER;

    uint32_t online_id = lookupOID(name);
    if (ServerConfig::m_server_owner != -1 
            && online_id == ServerConfig::m_server_owner)
        return std::numeric_limits<int>::max();

    std::string query = StringUtils::insertValues(
            "SELECT p.level FROM %s AS p"
            " INNER JOIN %s AS s ON (p.online_id = s.online_id) WHERE s.username = ?;",
            ServerConfig::m_permissions_table.c_str(),
            m_server_stats_table
            );
    sqlite3_stmt* stmt = NULL;
    int res = sqlite3_prepare_v2(m_db, query.c_str(), query.size(), &stmt, NULL);
    if (res != SQLITE_OK || !stmt)
    {
        Log::error("ServerLobby::loadPermissionLevelForUsername", "Unable to prepare the statement: %d, %s",
                res, sqlite3_errmsg(m_db));
        return PERM_PLAYER;
    }

    res = sqlite3_bind_text(stmt, 1, StringUtils::wideToUtf8(name).c_str(), -1, SQLITE_TRANSIENT);
    if (res != SQLITE_OK)
    {
        Log::error("ServerLobby::loadPermissionLevelForUsername", "Failed to bind arg #1.");
        return PERM_PLAYER;
    }

    res = sqlite3_step(stmt);
    if (res == SQLITE_DONE)
    {
        // nothing found
        sqlite3_finalize(stmt);
        return PERM_PLAYER;
    }
    else if (res == SQLITE_ROW)
    {
        uint32_t result = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return result;
    }
    else 
    {
        Log::error("ServerLobby::loadPermissionLevelForUsername", "Unable to execute the statement: %d, %s",
                res, sqlite3_errmsg(m_db));
        sqlite3_finalize(stmt);
        return PERM_PLAYER;
    }
}
void SQLiteDatabase::writePermissionLevelForOID(uint32_t online_id, int lvl)
{
    if (!m_db || !m_permissions_table_exists)
        return;

    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (online_id, level) VALUES (%u, %d) "
        "ON CONFLICT (online_id) DO UPDATE SET level = %d;",
        ServerConfig::m_permissions_table.c_str(),
        online_id, lvl, lvl
    );
    easySQLQuery(query);
}
void SQLiteDatabase::writePermissionLevelForUsername(const irr::core::stringw& name, int lvl)
{
    if (!m_db || !m_permissions_table_exists)
        return;

    //Binder(coll, (std::string) ServerConfig::m_permissions_table.c_str()),
    std::shared_ptr<BinderCollection> coll = std::make_shared<BinderCollection>();
    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (online_id, level) "
        "SELECT online_id, %d AS level FROM %s WHERE "
        "username = %s"
        "ON CONFLICT (online_id) DO UPDATE SET level = %d;",
        (std::string) ServerConfig::m_permissions_table.c_str(),
        lvl, m_server_stats_table, 
        Binder(coll, StringUtils::wideToUtf8(name), "username"), lvl
    );
    easySQLQuery(query, nullptr, coll->getBindFunction());
}
std::tuple<uint32_t, std::string> SQLiteDatabase::loadRestrictionsForOID(uint32_t online_id)
{
    if (!m_db || !m_restrictions_table_exists)
        return std::make_tuple(0, "");

    int res;
    struct target {
        uint32_t lvl = 0;
        std::string kart_id;
    } __target;

    char* errmsg;
    std::string query = StringUtils::insertValues(
        "SELECT flags, kart_id FROM %s WHERE online_id = %u;",
        ServerConfig::m_restrictions_table.c_str(),
        online_id);
    res = sqlite3_exec(m_db, query.c_str(),
            [](void* ptr, int amount, char** data, char** columns) {
                struct target* target = (struct target*)ptr;
                target->lvl = std::atol(data[0]);
                char kart_buf[121];
                if (data[1])
                    std::strncpy(kart_buf, data[1], 120);
                else
                    kart_buf[0] = 0;
                target->kart_id = std::string(kart_buf);
                return 0;
            }, &__target, &errmsg);
    if (res != SQLITE_OK && errmsg)
    {
        Log::error("DatabaseConnector", "loadRestrictionsForOID failure: %s", errmsg);
        sqlite3_free(errmsg);
        return std::make_tuple(__target.lvl, __target.kart_id);
    }
    Log::verbose("DatabaseConnector", "%u restrictions = %u", online_id, __target.lvl);
    return std::make_tuple(__target.lvl, __target.kart_id);
}
std::tuple<uint32_t, std::string> SQLiteDatabase::loadRestrictionsForUsername(const irr::core::stringw& name)
{
    if (!m_db || !m_restrictions_table_exists)
        return std::make_tuple(0, "");

    uint32_t df = PRF_OK;
    int res;
    auto default_ = std::make_tuple(df, "");
    std::string query = StringUtils::insertValues(
        "SELECT flags, kart_id FROM %s AS r INNER JOIN %s AS s ON (r.online_id = s.online_id) WHERE username = ?;",
        ServerConfig::m_restrictions_table.c_str(),
        m_server_stats_table);
    sqlite3_stmt* stmt = NULL;
    res = sqlite3_prepare_v2(m_db, query.c_str(), query.size(), &stmt, NULL);
    if (res != SQLITE_OK || !stmt)
    {
        Log::error("DatabaseConnector", "loadRestrictionsForUsername failure: %s",
                sqlite3_errmsg(m_db));
        return default_;
    }
    res = sqlite3_bind_text(stmt, 1, 
            StringUtils::wideToUtf8(name).c_str(), -1, SQLITE_TRANSIENT);
    if (res != SQLITE_OK)
    {
        Log::error("SQLiteDatabase::loadRestrictionsForUsername", "Failed to bind username.");
        return default_;
    }

    res = sqlite3_step(stmt);
    if (res == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        return default_;
    }
    if (res == SQLITE_ROW)
    {
        uint32_t flags = sqlite3_column_int(stmt, 0);
        const char* c_kart_id = (const char*)sqlite3_column_text(stmt, 1);
        std::string kart_id;
        if (c_kart_id)
            kart_id = c_kart_id;
        sqlite3_finalize(stmt);
        return std::make_tuple(flags, kart_id);
    }
    Log::error("DatabaseConnector", "loadRestrictionsForUsername failed to dispatch: %s",
            sqlite3_errmsg(m_db));
    sqlite3_finalize(stmt);
    return default_;
}
void SQLiteDatabase::writeRestrictionsForOID(uint32_t online_id, uint32_t flags)
{
    if (!m_db || !m_restrictions_table_exists)
        return;

    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (online_id, flags) VALUES (%u, %u) "
        "ON CONFLICT (online_id) DO UPDATE SET flags = %u;",
        ServerConfig::m_restrictions_table.c_str(),
        online_id, flags, flags
    );
    easySQLQuery(query);
}
void SQLiteDatabase::writeRestrictionsForOID(uint32_t online_id, uint32_t flags, const std::string& kart_id)
{
    if (!m_db || !m_restrictions_table_exists)
        return;

    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (online_id, flags, kart_id) VALUES (%u, %u, ?1) "
        "ON CONFLICT (online_id) DO UPDATE SET flags = %u, kart_id = ?1;",
        ServerConfig::m_restrictions_table.c_str(),
        online_id, flags, flags
    );
    // Binder cannot be used here: the value repeats twice (?1)
    easySQLQuery(query, nullptr,
        [kart_id](sqlite3_stmt* stmt)
        {
            if (kart_id.empty() ? (sqlite3_bind_null(stmt, 1) != SQLITE_OK)
                    :
                    (sqlite3_bind_text(stmt, 1,
                    kart_id.c_str(),
                    -1, SQLITE_TRANSIENT))
                    != SQLITE_OK)
            {
                Log::error("easySQLQuery", "Failed to bind %s.",
                    kart_id.c_str());
            }
        });
}
void SQLiteDatabase::writeRestrictionsForOID(uint32_t online_id, const std::string& kart_id)
{
    if (!m_db || !m_restrictions_table_exists)
        return;

    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (online_id, kart_id) VALUES (%u, ?1) "
        "ON CONFLICT (online_id) DO UPDATE SET kart_id = ?1;",
        ServerConfig::m_restrictions_table.c_str(),
        online_id
    );
    easySQLQuery(query, nullptr,
        [kart_id](sqlite3_stmt* stmt)
        {
            if (kart_id.empty() ? (sqlite3_bind_null(stmt, 1) != SQLITE_OK)
                    :
                    (sqlite3_bind_text(stmt, 1,
                    kart_id.c_str(),
                    -1, SQLITE_TRANSIENT))
                    != SQLITE_OK)
            {
                Log::error("easySQLQuery", "Failed to bind %s.",
                    kart_id.c_str());
            }
        });

}
void SQLiteDatabase::writeRestrictionsForUsername(const irr::core::stringw& name, uint32_t flags)
{
    if (!m_db || !m_restrictions_table_exists)
        return;

    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (online_id, flags) "
        "SELECT online_id, %d AS flags FROM %s WHERE "
        "username = ?"
        "ON CONFLICT (online_id) DO UPDATE SET flags = %d;",
        ServerConfig::m_restrictions_table.c_str(),
        flags, m_server_stats_table, flags
    );
    easySQLQuery(query, nullptr,
        [name](sqlite3_stmt* stmt)
        {
            if ((sqlite3_bind_text(stmt, 1,
                    StringUtils::wideToUtf8(name).c_str(),
                    -1, SQLITE_TRANSIENT))
                    != SQLITE_OK)
            {
                Log::error("easySQLQuery", "Failed to bind %s.",
                    name.c_str());
            }
            return 0;
        });
}
void SQLiteDatabase::writeRestrictionsForUsername(const irr::core::stringw& name, uint32_t flags, const std::string& kart_id)
{
    if (!m_db || !m_restrictions_table_exists)
        return;

    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (online_id, flags, kart_id) "
        "SELECT online_id, %d AS flags, ?1 AS kart_id FROM %s WHERE "
        "username = ?2"
        "ON CONFLICT (online_id) DO UPDATE SET flags = %d, kart_id = ?1;",
        ServerConfig::m_restrictions_table.c_str(),
        flags, m_server_stats_table, flags
    );
    easySQLQuery(query, nullptr,
        [name, kart_id](sqlite3_stmt* stmt)
        {
            if (kart_id.empty() ? (sqlite3_bind_null(stmt, 1) != SQLITE_OK)
                    :
                    (sqlite3_bind_text(stmt, 1,
                    kart_id.c_str(),
                    -1, SQLITE_TRANSIENT))
                    != SQLITE_OK)
            {
                Log::error("easySQLQuery", "Failed to bind %s.",
                    kart_id.c_str());
            }
            if ((sqlite3_bind_text(stmt, 2,
                    StringUtils::wideToUtf8(name).c_str(),
                    -1, SQLITE_TRANSIENT))
                    != SQLITE_OK)
            {
                Log::error("easySQLQuery", "Failed to bind %s.",
                    name.c_str());
            }
            return 0;
        });
}
void SQLiteDatabase::writeRestrictionsForUsername(const irr::core::stringw& name, const std::string& kart_id)
{
    if (!m_db || !m_restrictions_table_exists)
        return;

    std::string query = StringUtils::insertValues(
        "INSERT INTO %s (online_id, kart_id) "
        "SELECT online_id, ?1 AS kart_id FROM %s WHERE "
        "username = ?2"
        "ON CONFLICT (online_id) DO UPDATE SET kart_id = ?1;",
        ServerConfig::m_restrictions_table.c_str(),
        m_server_stats_table
    );
    easySQLQuery(query, nullptr,
        [name, kart_id](sqlite3_stmt* stmt)
        {
            if (kart_id.empty() ? (sqlite3_bind_null(stmt, 1) != SQLITE_OK)
                    :
                    (sqlite3_bind_text(stmt, 1,
                    kart_id.c_str(),
                    -1, SQLITE_TRANSIENT))
                    != SQLITE_OK)
            {
                Log::error("easySQLQuery", "Failed to bind %s.",
                    kart_id.c_str());
            }
            if ((sqlite3_bind_text(stmt, 2,
                    StringUtils::wideToUtf8(name).c_str(),
                    -1, SQLITE_TRANSIENT))
                    != SQLITE_OK)
            {
                Log::error("easySQLQuery", "Failed to bind %s.",
                    name.c_str());
            }
            return 0;
        });
}
uint32_t SQLiteDatabase::lookupOID(const std::string& name)
{
    if (name.empty() || !m_db)
        return 0;

    std::string query = StringUtils::insertValues(
        "SELECT online_id FROM %s WHERE username = ? LIMIT 1;",
        m_server_stats_table
    );
    sqlite3_stmt* stmt = NULL;
    int res = sqlite3_prepare_v2(m_db, query.c_str(), query.size(), &stmt, NULL);
    if (res != SQLITE_OK || !stmt)
    {
        Log::error("ServerLobby", "Error in lookupOID, sqlite3_prepare_v2 returned %d: %s",
                res, sqlite3_errmsg(m_db));
        return 0;
    }
    res = sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (res != SQLITE_OK)
    {
        Log::error("ServerLobby::lookupOID", "Failed to bind %s.",
            name.c_str());
        return 0;
    }

    res = sqlite3_step(stmt);
    if (res == SQLITE_ROW)
    {
        uint32_t ret = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return ret;
    }
    if (res == SQLITE_DONE)
    {
        Log::verbose("ServerLobby", "lookupOID: %s not found.",
                name.c_str());
        sqlite3_finalize(stmt);
        // not found
        return 0;
    }
    // error occurred
    Log::error("ServerLobby", "Error in lookupOID, step returned %d: %s",
            res, sqlite3_errmsg(m_db));
    sqlite3_finalize(stmt);
    return 0;
}
uint32_t SQLiteDatabase::lookupOID(const core::stringw& name)
{
    if (name.empty() || !m_db)
        return 0;

    std::string query = StringUtils::insertValues(
        "SELECT online_id FROM %s WHERE username = ? LIMIT 1;",
        m_server_stats_table
    );
    sqlite3_stmt* stmt = NULL;
    int res = sqlite3_prepare_v2(m_db, query.c_str(), query.size(), &stmt, NULL);
    if (res != SQLITE_OK || !stmt)
    {
        Log::error("ServerLobby", "Error in lookupOID, sqlite3_prepare_v2 returned %d: %s",
                res, sqlite3_errmsg(m_db));
        return 0;
    }
    res = sqlite3_bind_text(stmt, 1, StringUtils::wideToUtf8(name).c_str(), -1, SQLITE_TRANSIENT);
    if (res != SQLITE_OK)
    {
        Log::error("ServerLobby::lookupOID", "Failed to bind %s.",
            name.c_str());
        return 0;
    }

    res = sqlite3_step(stmt);
    if (res == SQLITE_ROW)
    {
        uint32_t ret = sqlite3_column_int(stmt, 0);
        Log::verbose("ServerLobby", "lookupOID: %s = %d.",
                StringUtils::wideToUtf8(name).c_str(),
                ret);
        sqlite3_finalize(stmt);
        return ret;
    }
    if (res == SQLITE_DONE)
    {
        Log::verbose("ServerLobby", "lookupOID: %s not found.",
                StringUtils::wideToUtf8(name).c_str());
        sqlite3_finalize(stmt);
        // not found
        return 0;
    }
    // error occurred
    Log::error("ServerLobby", "Error in lookupOID, step returned %d: %s",
            res, sqlite3_errmsg(m_db));
    sqlite3_finalize(stmt);
    return 0;
}
int SQLiteDatabase::banPlayer(const std::string& name, const std::string& reason, int days)
{
    if (!m_db || !m_online_id_ban_table_exists)
        return -1;

    if (name.empty())
        return 1;
    
    std::string query = StringUtils::insertValues(
            "INSERT INTO %s (online_id, reason, expired_days) "
            "SELECT online_id, ?1 AS reason, ?2 AS expired_days FROM %s "
            "WHERE online_id > 0 AND username = ?3 ON CONFLICT (online_id) DO "
            "UPDATE SET reason = ?1, expired_days = ?2;",
        ServerConfig::m_online_id_ban_table.c_str(),
        m_server_stats_table
    );
    sqlite3_stmt* stmt = NULL;
    int res = sqlite3_prepare_v2(m_db, query.c_str(), query.size(), &stmt, NULL);
    if (res != SQLITE_OK || !stmt)
    {
        Log::error("ServerLobby", "Error in banPlayer, sqlite3_prepare_v2 returned %d: %s",
                res, sqlite3_errmsg(m_db));
        return 2;
    }
    if (reason.empty())
    {
        res = sqlite3_bind_null(stmt, 1);
        if (res != SQLITE_OK)
        {
            Log::error("ServerLobby::banPlayer", "Failed to bind arg #1 (null).");
            return 2;
        }
    }
    else
    {
        res = sqlite3_bind_text(stmt, 1, reason.c_str(), -1, SQLITE_TRANSIENT);
        if (res != SQLITE_OK)
        {
            Log::error("ServerLobby::banPlayer", "Failed to bind arg #1.");
            return 2;
        }
    }
    if (days < 0)
    {
        res = sqlite3_bind_null(stmt, 2);
        if (res != SQLITE_OK)
        {
            Log::error("ServerLobby::banPlayer", "Failed to bind arg #2 (null).");
            return 2;
        }
    }
    else
    {
        res = sqlite3_bind_int(stmt, 2, days);
        if (res != SQLITE_OK)
        {
            Log::error("ServerLobby::banPlayer", "Failed to bind arg #2.");
            return 2;
        }
    }
    res = sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
    if (res != SQLITE_OK)
    {
        Log::error("ServerLobby::banPlayer", "Failed to bind arg #3.");
        return 2;
    }

    res = sqlite3_step(stmt);
    if (res != SQLITE_DONE)
    {
        Log::error("ServerLobby", "Error in banPlayer, step returned %d: %s",
            res, sqlite3_errmsg(m_db));
        sqlite3_finalize(stmt);
        return 2;
    }
    if ((res = sqlite3_changes(m_db)) == 0)
    {
        sqlite3_finalize(stmt);
        // nothing is done
        return 1;
    }

    sqlite3_finalize(stmt);
    // player is banned, attempt to kick the player if there's one online

    std::shared_ptr<STKPeer> peer = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(name));
    if (peer)
        peer->kick();

    return 0;
}
int SQLiteDatabase::unbanPlayer(const std::string& name) 
{
    if (!m_db || !m_online_id_ban_table_exists)
        return -2;

    if (name.empty())
        return 1;

    std::string query = StringUtils::insertValues(
            "DELETE FROM %s WHERE online_id IN ("
            "SELECT online_id FROM %s "
            "WHERE online_id > 0 AND username = ? LIMIT 1);",
        ServerConfig::m_online_id_ban_table.c_str(),
        m_server_stats_table
    );
    sqlite3_stmt* stmt = NULL;
    int res = sqlite3_prepare_v2(m_db, query.c_str(), query.size(), &stmt, NULL);
    if (res != SQLITE_OK || !stmt)
    {
        Log::error("ServerLobby", "Error in unbanPlayer, sqlite3_prepare_v2 returned %d: %s",
                res, sqlite3_errmsg(m_db));
        return 2;
    }
    res = sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (res != SQLITE_OK)
    {
        Log::error("ServerLobby::unbanPlayer", "Failed to bind arg #1.");
        return 2;
    }

    res = sqlite3_step(stmt);
    if (res != SQLITE_DONE)
    {
    Log::error("ServerLobby", "Error in unbanPlayer, step returned %d: %s",
            res, sqlite3_errmsg(m_db));
        sqlite3_finalize(stmt);
        return 2;
    }
    if ((res = sqlite3_changes(m_db)) == 0)
    {
        sqlite3_finalize(stmt);
        // nothing is done, which means player wasn't banned
        return 1;
    }
#if 0
    if (res > 1)
    {
        // multiple players are banned?
        Log::error("ServerLobby::unbanPlayer",
                "Multiple players were unbanned (%d rows affected)", res);
        sqlite3_finalize(stmt);
        return 2;
    }
#endif

    sqlite3_finalize(stmt);
    return 0;
}
const std::string SQLiteDatabase::formatBanList(unsigned int page, unsigned int psize)
{
    if (!m_db || !m_online_id_ban_table_exists || !psize)
        return "";

    // get number of pages first
    std::string query_agg = StringUtils::insertValues(
            "SELECT (count(*) / 8 + iif(count(*) %% 8 > 0, 1, 0)) AS num_pages FROM %s;",
            ServerConfig::m_online_id_ban_table.c_str());
    sqlite3_stmt* stmt = NULL;
    int res = sqlite3_prepare_v2(m_db, query_agg.c_str(), query_agg.size(), &stmt, NULL);
    if (res != SQLITE_OK || !stmt)
    {
        Log::error("ServerLobby::formatBanList", "Unable to prepare the statement: %d, %s",
                res, sqlite3_errmsg(m_db));
        return "";
    }
    
    unsigned int pages = 0;
    res = sqlite3_step(stmt);
    if (res == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        return "No players have been banned.\n(Page 1 of 1)";
    }
    if (res == SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        pages = sqlite3_column_int64(stmt, 0);
    }
    else
    {
        Log::error("ServerLobby::formatBanList", "Unable to execute the statement: %d, %s",
                res, sqlite3_errmsg(m_db));
        sqlite3_finalize(stmt);
        return "";
    }
    sqlite3_finalize(stmt);

    irr::core::clamp<unsigned>(page, 1, pages);

    // aggregate information acquired
    std::string query = StringUtils::insertValues(
            "SELECT DISTINCT b.online_id, s.username, reason, expired_days FROM %s AS b"
            "RIGHT OUTER JOIN %s AS s ON (s.online_id = b.online_id) "
            "LIMIT %u OFFSET %u * %u;",
            ServerConfig::m_online_id_ban_table.c_str(),
            m_server_stats_table, psize, psize, page);
    
    stmt = NULL;
    res = sqlite3_prepare_v2(m_db, query.c_str(), query.size(), &stmt, NULL);
    if (res != SQLITE_OK || !stmt)
    {
        Log::error("ServerLobby::formatBanList", "Unable to prepare the statement: %d, %s",
                res, sqlite3_errmsg(m_db));
        return "";
    }

    std::string result = StringUtils::insertValues(
            "Online ID bans (page %d of %d):\n", page, pages
            );

    while ((res = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        unsigned int online_id = sqlite3_column_int(stmt, 0);
        const unsigned char* username = sqlite3_column_text(stmt, 1);
        const unsigned char* reason;
        if (sqlite3_column_type(stmt, 2) == SQLITE_NULL)
        {
            reason = (const unsigned char*)"[UNSPECIFIED]";
        }
        else
        {
            reason = sqlite3_column_text(stmt, 2);
        }

        result += StringUtils::insertValues(
                "[%u] %s: %s", online_id, username, reason);

        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
        {
            result += (std::string(" (expires in ") +
                    std::to_string(sqlite3_column_int(stmt, 3))
                    + " days).");
        }
        result += "\n";
    }
    if (res != SQLITE_ROW)
    {
        Log::error("ServerLobby", "Could not make a proper ban list with consistency... "
                "sqlite3_step returns %d", res);
    }
    sqlite3_finalize(stmt);
    return result;
}
const std::string SQLiteDatabase::formatBanInfo(const std::string& name)
{
    if (!m_db || !m_online_id_ban_table_exists || name.empty())
        return "";

    // get number of pages first
    std::string query_agg = StringUtils::insertValues(
            "SELECT DISTINCT b.online_id, s.username, reason, expired_days FROM %s "
            "INNER JOIN %s AS s ON (b.online_id = s.online_id) WHERE s.username = ?;",
            ServerConfig::m_online_id_ban_table.c_str(),
            m_server_stats_table);
    sqlite3_stmt* stmt = NULL;
    int res = sqlite3_prepare_v2(m_db, query_agg.c_str(), query_agg.size(), &stmt, NULL);
    if (res != SQLITE_OK || !stmt)
    {
        Log::error("ServerLobby::formatBanList", "Unable to prepare the statement: %d, %s",
                res, sqlite3_errmsg(m_db));
        return "";
    }

    res = sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    if (res != SQLITE_OK)
    {
        Log::error("ServerLobby::unbanPlayer", "Failed to bind arg #1.");
        return "";
    }
    
    res = sqlite3_step(stmt);
    if (res == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        return "This player has not been banned.";
    }
    if (res == SQLITE_ROW)
    {
        std::string result = StringUtils::insertValues(
            "Online ID: %u, %s banned because: %s, days: %s",
            sqlite3_column_int(stmt, 0),
            sqlite3_column_text(stmt, 1),
            (sqlite3_column_type(stmt, 2) == SQLITE_TEXT) ?
                sqlite3_column_text(stmt, 2) :
                (const unsigned char*)"[UNSPECIFIED]",
            (sqlite3_column_type(stmt, 3) == SQLITE_INTEGER) ?
                std::to_string(sqlite3_column_int(stmt, 3)) :
                "[FOREVER]");
        sqlite3_finalize(stmt);
        return result;
    }
    else
    {
        sqlite3_finalize(stmt);
        Log::error("ServerLobby::formatBanList", "Unable to execute the statement: %d, %s",
                res, sqlite3_errmsg(m_db));
        return "";
    }
    sqlite3_finalize(stmt);
    return "";
}

#endif // ENABLE_SQLITE3
