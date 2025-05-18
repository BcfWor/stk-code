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

#include "network/database/abstract_database.hpp"
#include "utils/cpp2011.hpp"

// This is an implementation for SQLite3 database used mainly by the ranking,
// network moderation system + access control and other solutions.
// Implements threadsafe interface.

class SQLiteDatabase : public AbstractDatabase
{
public:
    SQLiteDatabase();
    explicit SQLiteDatabase(SQLiteDatabase&) = delete;
    explicit SQLiteDatabase(SQLiteDatabase&&) = delete;

    virtual void init() OVERRIDE;
    virtual void finalize() OVERRIDE;
    // add more methods...
};

#endif // SQLITE_DATABASE_HPP
