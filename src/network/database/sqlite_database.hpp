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

#ifndef SQLITE_DATABASE_HPP
#define SQLITE_DATABASE_HPP

extern "C"
{

#include <sqlite3.h>

}

#include "network/database/abstract_database.hpp"
#include "utils/cpp2011.hpp"
#include <functional>

// This is an implementation for SQLite3 database used mainly by the ranking,
// network moderation system + access control and other solutions.
// Most of the code is moved from the original stk-code branch
// Implements threadsafe interface.

/** The purpose of Binder and BinderCollection structures is to allow
 *   putting values to bind inside StringUtils::insertValues, which is commonly
 *   used for values that don't require binding (such as integers).
 *  Unlike previously used approach with separate query formation and binding,
 *   the arguments are written in the code in the order of query appearance
 *   (even though real binding still happens later). It also avoids repeated
 *   binding code.
 *
 *  Syntax looks as follows:
 *  std::shared_ptr<BinderCollection> coll = std::make_shared...;
 *  std::string query_string = StringUtils::insertValues(
 *      "query contents with wildcards of type %d, %s, %u, ..."
 *      "where %s is put for values that will be bound later",
 *      values to insert, ..., Binder(coll, other parameters), ...);
 *  Then the bind function (e.g. for usage in easySQLQuery) should be
 *  coll->getBindFunction().
 */

struct Binder;

/** BinderCollection is a structure that collects Binder objects used in an
 *   SQL query formed with insertValues() (see above). For a single query, a
 *   single instance of BinderCollection should be used. After a query is
 *   formed, BinderCollection can produce bind function to use with sqlite3.
 */
struct BinderCollection
{
    std::vector<std::shared_ptr<Binder>> m_binders;

    std::function<void(sqlite3_stmt* stmt)> getBindFunction() const;
};

/** Binder is a wrapper for a string to be bound into an SQL query. See above
 *   for its usage in insertValues(). When it's printed to an output stream
 *   (in particular, this is done in insertValues implementation), this Binder
 *   is added to the query's BinderCollection, and the '?'-placeholder is added
 *   to the query string instead of %s.
 *
 *  When using Binder, make sure that:
 *   - operator << is invoked on it exactly once;
 *   - operator << is invoked on several Binders in the order in which they go
 *     in the query;
 *   - before calling insertValues, there is a %-wildcard corresponding to the
 *     Binder in the query string (and not '?').
 *  For example, when the query formed inside of a function depends on its
 *   arguments, it should be formed part by part, from left to right.
 *  Of course, you can choose the "default" way, binding values separately from
 *   insertValues() call.
 */
struct Binder
{
    std::weak_ptr<BinderCollection> m_collection;
    std::string m_value;
    std::string m_name;
    bool m_use_null_if_empty;

    Binder(std::shared_ptr<BinderCollection> collection, std::string value,
           std::string name = "", bool use_null_if_empty = false):
        m_collection(collection), m_value(value),
        m_name(name), m_use_null_if_empty(use_null_if_empty) {}
};

std::ostream& operator << (std::ostream& os, const Binder& binder);

class SQLiteDatabase : public AbstractDatabase
{
    sqlite3* m_db;
public:
    SQLiteDatabase();
    // calls finalize()
    virtual ~SQLiteDatabase() OVERRIDE;
    explicit SQLiteDatabase(SQLiteDatabase&) = delete;
    explicit SQLiteDatabase(SQLiteDatabase&&) = delete;

    virtual void init() OVERRIDE;
    virtual void finalize() OVERRIDE;

    // Specific to SQLite for now.
    bool easySQLQuery(const std::string& query,
                       std::vector<std::vector<std::string>>* output = nullptr,
               std::function<void(sqlite3_stmt* stmt)> bind_function = nullptr,
                                            std::string null_value = "") const;
    virtual void checkTableExists(const std::string& table, bool& result) OVERRIDE;

    virtual std::string ip2Country(const SocketAddress& addr) const OVERRIDE;

    virtual std::string ipv62Country(const SocketAddress& addr) const OVERRIDE;

    virtual void writeDisconnectInfoTable(STKPeer* peer) OVERRIDE;
    virtual void initServerStatsTable() OVERRIDE;
    virtual bool writeReport(
         STKPeer* reporter, std::shared_ptr<NetworkPlayerProfile> reporter_npp,
       STKPeer* reporting, std::shared_ptr<NetworkPlayerProfile> reporting_npp,
                                                     irr::core::stringw& info) OVERRIDE;
    virtual bool hasDatabase() const OVERRIDE { return m_db != nullptr; }
    virtual std::vector<IpBanTableData> getIpBanTableData(uint32_t ip = 0) const OVERRIDE;
    virtual std::vector<Ipv6BanTableData> getIpv6BanTableData(std::string ipv6 = "") const OVERRIDE;
    virtual std::vector<OnlineIdBanTableData> getOnlineIdBanTableData(uint32_t online_id = 0) const OVERRIDE;
    virtual void increaseIpBanTriggerCount(uint32_t ip_start, uint32_t ip_end) const OVERRIDE;
    virtual void increaseIpv6BanTriggerCount(const std::string& ipv6_cidr) const OVERRIDE;
    virtual void increaseOnlineIdBanTriggerCount(uint32_t online_id) const OVERRIDE;
    virtual void clearOldReports() OVERRIDE;
    virtual void setDisconnectionTimes(std::vector<uint32_t>& present_hosts) OVERRIDE;
    virtual void saveAddressToIpBanTable(const SocketAddress& addr) OVERRIDE;
    virtual void onPlayerJoinQueries(std::shared_ptr<STKPeer> peer, uint32_t online_id,
        unsigned player_count, const std::string& country_code) OVERRIDE;
    virtual void listBanTable() OVERRIDE;
    /* Moderation toolkit */
    virtual int loadPermissionLevelForOID(uint32_t online_id) OVERRIDE;
    virtual int loadPermissionLevelForUsername(const irr::core::stringw& name) OVERRIDE;
    virtual void writePermissionLevelForOID(uint32_t online_id, int lvl) OVERRIDE;
    virtual void writePermissionLevelForUsername(const irr::core::stringw& name, int lvl) OVERRIDE;
    virtual std::tuple<uint32_t, std::string> loadRestrictionsForOID(uint32_t online_id) OVERRIDE;
    virtual std::tuple<uint32_t, std::string> loadRestrictionsForUsername(const irr::core::stringw& name) OVERRIDE;
    virtual void writeRestrictionsForOID(uint32_t online_id, uint32_t flags) OVERRIDE;
    virtual void writeRestrictionsForOID(uint32_t online_id, uint32_t flags, const std::string& set_kart) OVERRIDE;
    virtual void writeRestrictionsForOID(uint32_t online_id, const std::string& set_kart) OVERRIDE;
    virtual void writeRestrictionsForUsername(const irr::core::stringw& name, uint32_t flags) OVERRIDE;
    virtual void writeRestrictionsForUsername(const irr::core::stringw& name, uint32_t flags, const std::string& set_kart) OVERRIDE;
    virtual void writeRestrictionsForUsername(const irr::core::stringw& name, const std::string& set_kart) OVERRIDE;
    virtual uint32_t lookupOID(const std::string& name) OVERRIDE;
    virtual uint32_t lookupOID(const irr::core::stringw& name) OVERRIDE;
    virtual int banPlayer(const std::string& name, const std::string& reason, int days = -1) OVERRIDE;
    virtual int unbanPlayer(const std::string& name) OVERRIDE;
    virtual const std::string formatBanList(unsigned int page, unsigned int psize) OVERRIDE;
    virtual const std::string formatBanInfo(const std::string& name) OVERRIDE;
    /* /Moderation toolkit */

    static void upperIPv6SQL(sqlite3_context* context, int argc, sqlite3_value** argv);
    static void insideIPv6CIDRSQL(sqlite3_context* context, int argc, sqlite3_value** argv);
};

#endif // SQLITE_DATABASE_HPP
