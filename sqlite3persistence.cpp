//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44utils.
//
//  p44utils is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44utils is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44utils. If not, see <http://www.gnu.org/licenses/>.
//

#include "sqlite3persistence.hpp"

#include "logger.hpp"

using namespace p44;


const char *SQLite3Error::domain()
{
  return "SQLite3";
}


const char *SQLite3Error::getErrorDomain() const
{
  return SQLite3Error::domain();
};


SQLite3Error::SQLite3Error(int aSQLiteError, const char *aSQLiteMessage, const char *aContextMessage) :
  Error(ErrorCode(aSQLiteError), string(nonNullCStr(aContextMessage)).append(nonNullCStr(aSQLiteMessage)))
{
}


ErrorPtr SQLite3Error::err(int aSQLiteError, const char *aSQLiteMessage, const char *aContextMessage)
{
  if (aSQLiteError==SQLITE_OK)
    return ErrorPtr();
  return ErrorPtr(new SQLite3Error(aSQLiteError, aSQLiteMessage, aContextMessage));
}


SQLite3Persistence::SQLite3Persistence() :
  mInitialized(false)
{
}


SQLite3Persistence::~SQLite3Persistence()
{
  disconnectDatabase();
}


void SQLite3Persistence::disconnectDatabase()
{
  if (mInitialized) {
    disconnect();
    mInitialized = false;
  }
}


ErrorPtr SQLite3Persistence::connectDatabase(const char *aDatabaseFileName, bool aFactoryReset)
{
  int err;

  if (aFactoryReset) {
    // make sure we are disconnected
    disconnect();
    // first delete the database entirely
    unlink(aDatabaseFileName);
  }
  // now initialize the DB
  if (!mInitialized) {
    err = connect(aDatabaseFileName);
    if (err!=SQLITE_OK) {
      LOG(LOG_ERR, "SQLite3Persistence: Cannot open %s : %s", aDatabaseFileName, error_msg());
      return error();
    }
  }
  return ErrorPtr();
}


ErrorPtr SQLite3Persistence::error(const char *aContextMessage)
{
  return SQLite3Error::err(error_code(), error_msg(), aContextMessage);
}




// MARK: SQLite3TableGroup

bool SQLite3TableGroup::isAvailable()
{
  return mPersistenceP && mPersistenceP->mInitialized && mSchemaReady;
}


SQLite3Persistence& SQLite3TableGroup::db()
{
  assert(mPersistenceP);
  return *mPersistenceP;
}


#define PREFIX_PLACEHOLDER "$PREFIX_"

string SQLite3TableGroup::prefixedSql(const string& aSqlTemplate, string aPrefix)
{
  #if DEBUG
  // safety check: MUST contain $PREFIX_
  assert(aSqlTemplate.find(PREFIX_PLACEHOLDER)!=string::npos);
  #endif
  if (!aPrefix.empty()) aPrefix += "_";
  return string_substitute(aSqlTemplate, PREFIX_PLACEHOLDER, aPrefix);
}


ErrorPtr SQLite3TableGroup::prefixedExecute(const char* aTemplate, ...)
{
  ErrorPtr err;
  string sql;
  va_list args;
  va_start(args, aTemplate);
  boost::shared_ptr<char> msql(sqlite3_vmprintf(aTemplate, args), sqlite3_free);
  va_end(args);
  sql = prefixedSql(msql.get());
  if (db().execute(sql.c_str())!=SQLITE_OK) {
    err = db().error();
  }
  return err;
}


string SQLite3TableGroup::schemaUpgradeSQL(int aFromVersion, int &aToVersion)
{
  string s;
  // default is creating the globs table when starting from scratch
  if (aFromVersion==0) {
    s =
      "DROP TABLE IF EXISTS $PREFIX_globs;"
      "CREATE TABLE $PREFIX_globs ("
      " ROWID INTEGER PRIMARY KEY AUTOINCREMENT,"
      " schemaVersion INTEGER"
      ");"
      "INSERT INTO $PREFIX_globs (schemaVersion) VALUES (0);";
  }
  return s;
}





ErrorPtr SQLite3TableGroup::initialize(SQLite3Persistence& aPersistence, const string aTablesPrefix, int aNeededSchemaVersion, int aLowestValidSchemaVersion, const char* aDatabaseToMigrateFrom)
{
  int sq3err;
  int currentSchemaVersion;
  ErrorPtr err;

  mPersistenceP = &aPersistence;
  mTablesPrefix = aTablesPrefix;
  #if SQLITE3_UNIFY_DB_MIGRATION
  bool tryMigration = aDatabaseToMigrateFrom!=nullptr; // assume not yet migrated, but not if we don't have a name
  #endif
  while(true) {
    currentSchemaVersion = 0; // assume table group not yet existing
    mSchemaReady = false;
    // query the DB version
    sqlite3pp::query qry(db());
    if (qry.prepare(prefixedSql("SELECT schemaVersion FROM $PREFIX_globs").c_str())==SQLITE_OK) {
      sqlite3pp::query::iterator row = qry.begin();
      if (row!=qry.end()) {
        // get current db version
        currentSchemaVersion = row->get<int>(0);
        #if SQLITE3_UNIFY_DB_MIGRATION
        // we have the table group already, do not try migrating any more
        tryMigration = false;
        #endif
      }
      qry.finish();
    }
    #if SQLITE3_UNIFY_DB_MIGRATION
    if (tryMigration) {
      tryMigration = false; // don't try twice
      if (db().executef("ATTACH DATABASE '%s' AS old;", aDatabaseToMigrateFrom)==SQLITE_OK) {
        LOG(LOG_WARNING, "%s: Migrating from separate database file '%s' now", mTablesPrefix.c_str(), aDatabaseToMigrateFrom);
        // Query all table names in the attached database
        sqlite3pp::query tableqry(db());
        if (tableqry.prepare("SELECT name,sql FROM old.sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';")!=SQLITE_OK) {
          err = db().error("Error getting old table names and schemas: ");
        }
        else {
          for (sqlite3pp::query::iterator namerow = tableqry.begin(); namerow!=tableqry.end(); namerow++) {
            string tname = namerow->get<const char *>(0);
            string tsql = namerow->get<const char *>(1);
            string tablecreate = prefixedSql(string_substitute(tsql, tname, string_format("$PREFIX_%s", tname.c_str()), 1));
            if (db().execute(tablecreate.c_str())!=SQLITE_OK) {
              err = db().error("creating new table: ");
              break;
            }
            string datacopy = prefixedSql(string_format("INSERT INTO $PREFIX_%s SELECT * FROM old.%s", tname.c_str(), tname.c_str()));
            if (db().execute(datacopy.c_str())!=SQLITE_OK) {
              err = db().error("copying table data: ");
              break;
            }
          }
        }
        // always detach
        db().executef("DETACH DATABASE old;");
        // must read current schema version again from migrated table
        continue;
      }
    }
    #endif // SQLITE3_UNIFY_DB_MIGRATION
    break;
  }
  // check for obsolete (ancient, not-to-be-upgraded DB versions)
  if (currentSchemaVersion>0 && aLowestValidSchemaVersion!=0 && currentSchemaVersion<aLowestValidSchemaVersion) {
    // there is a DB, but it is obsolete and should be deleted
    LOG(LOG_WARNING, "table group '%s' has non-upgradeable ancient schemaVersion (%d) -> will be reset", mTablesPrefix.c_str(), currentSchemaVersion);
    currentSchemaVersion = 0; // force re-creating
  }
  // migrate if needed
  if (currentSchemaVersion>aNeededSchemaVersion) {
    err = SQLite3Error::err(SQLITE_PERSISTENCE_ERR_SCHEMATOONEW,"Database has too new schema version: cannot be used");
  }
  else {
    while (currentSchemaVersion<aNeededSchemaVersion) {
      // get SQL statements for schema updating
      int nextSchemaVersion = aNeededSchemaVersion;
      string tmpl = schemaUpgradeSQL(currentSchemaVersion, nextSchemaVersion);
      // safety check: MUST contain $PREFIX_
      if (tmpl.find(PREFIX_PLACEHOLDER)==string::npos) {
        err = SQLite3Error::err(SQLITE_PERSISTENCE_ERR_MIGRATION, "fatal internal error: template does not contain table prefix(es)");
        break;
      }
      if (tmpl.size()==0) {
        err = SQLite3Error::err(SQLITE_PERSISTENCE_ERR_MIGRATION, string_format("Database migration error: no update path available from %d to %d", currentSchemaVersion, nextSchemaVersion).c_str());
        break;
      }
      string upgradeSQL = prefixedSql(tmpl);
      // execute the schema upgrade SQL
      sqlite3pp::command upgradeCmd(db());
      sq3err = upgradeCmd.prepare(upgradeSQL.c_str());
      if (sq3err==SQLITE_OK)
        sq3err = upgradeCmd.execute_all();
      if (sq3err!=SQLITE_OK) {
        LOG(LOG_ERR,
          "SQLite3TableGroup: Error executing schema upgrade SQL from version %d to %d = %s : %s",
          currentSchemaVersion, nextSchemaVersion, upgradeSQL.c_str(), db().error_msg()
        );
        err = db().error("Error executing migration SQL: ");
        break;
      }
      upgradeCmd.finish();
      // successful, we have reached a new version
      currentSchemaVersion = nextSchemaVersion;
      // set it in globs
      sq3err = db().executef(prefixedSql("UPDATE $PREFIX_globs SET schemaVersion = %d").c_str(), currentSchemaVersion);
      if (sq3err!=SQLITE_OK) {
        LOG(LOG_ERR, "SQLite3TableGroup: Cannot set schemaVersion = %d: %s", currentSchemaVersion, db().error_msg());
        err = db().error("Error setting schema version: ");
        break;
      }
    }
  }
  // done
  if (Error::isOK(err)) {
    mSchemaReady = true;
  }
  else {
    LOG(LOG_ERR, "Error initializing SQLite3TableGroup: %s", err->text());
  }
  return err;
}


// MARK: SQLiteTGQuery

SQLiteTGQuery::SQLiteTGQuery(SQLite3TableGroup& aTableGroup) :
  mTableGroup(aTableGroup),
  inherited(aTableGroup.db())
{
}

/// prepared query from template with $PREFIX\_ in it
ErrorPtr SQLiteTGQuery::prefixedPrepare(const char* aTemplate, ...)
{
  ErrorPtr err;
  va_list args;
  va_start(args, aTemplate);
  boost::shared_ptr<char> msql(sqlite3_vmprintf(aTemplate, args), sqlite3_free);
  va_end(args);
  string sql = mTableGroup.prefixedSql(msql.get());
  if (inherited::prepare(sql.c_str())!=SQLITE_OK) {
    err = mTableGroup.db().error();
  }
  return err;
}


// MARK: SQLiteTGCommand

SQLiteTGCommand::SQLiteTGCommand(SQLite3TableGroup& aTableGroup) :
  mTableGroup(aTableGroup),
  inherited(aTableGroup.db())
{
}

/// prepared command from template with $PREFIX\_ in it
ErrorPtr SQLiteTGCommand::prefixedPrepare(const char* aTemplate, ...)
{
  ErrorPtr err;
  va_list args;
  va_start(args, aTemplate);
  boost::shared_ptr<char> msql(sqlite3_vmprintf(aTemplate, args), sqlite3_free);
  va_end(args);
  string sql = mTableGroup.prefixedSql(msql.get());
  if (inherited::prepare(sql.c_str())!=SQLITE_OK) {
    err = mTableGroup.db().error();
  }
  return err;
}

