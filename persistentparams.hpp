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

#ifndef __p44utils__PersistentParams__
#define __p44utils__PersistentParams__

#include "sqlite3persistence.hpp"

using namespace std;

namespace p44 {

  typedef struct {
    const char *fieldName;
    int dataTypeCode;
  } FieldDefinition;


  class ParamStore : public SQLite3Persistence
  {
    typedef SQLite3Persistence inherited;

  public:
    uint32_t mWriteOpsCount; ///< global number of write operations (INSERT, UPDATE, DELETE)

    ParamStore() : mWriteOpsCount(0) {};

  };



  /// @note this class does NOT derive from P44Obj, so it can be added as "interface" using multiple-inheritance
  class PersistentParams
  {
    bool mDirty; ///< if set, means that values need to be saved

  protected:
    ParamStore &mParamStore; ///< the associated parameter store

  public:
    PersistentParams(ParamStore &aParamStore);
    uint64_t mRowId; ///< ROWID of the persisted data, 0 if not yet persisted

    /// @name interface to be implemented for specific parameter sets in subclasses
    /// @{

    /// get name of DB table to store persistent parameters in
    /// @return table name
    /// @note derived classes might extend an existing table (as long as base class' fields
    ///   are supported) or define a separate table to store the derived objects.
    virtual const char *tableName() = 0;

    /// @return number of key field definitions
    virtual size_t numKeyDefs();

    /// get primary key field definitions
    /// @param aIndex the field definition index, 0..numKeyDefs().
    /// @return pointer to FieldDefinition, NULL if index is out of range.
    ///   These fields together build the primary key, which must be unique (only one record with a given
    ///   combination of key values may exist in the DB)
    /// @note the first field must be the one which identifies the parent.
    //    Other key fields may be needed if parent can have more than one child
    /// @note for the base class, this returns a single string field named "parentID",
    ///   which is usually an ID for the entity for which this is the parameter set
    virtual const FieldDefinition *getKeyDef(size_t aIndex);

    /// @return number of data field definitions
    virtual size_t numFieldDefs() { return 0; };

    /// get data field definitions
    /// @param aIndex the field definition index, 0..numFieldDefs().
    /// @return pointer to FieldDefinition, NULL if index is out of range.
    virtual const FieldDefinition *getFieldDef(size_t aIndex) { return NULL; }

    /// load values from passed row
    /// @param aRow result row to get parameter values from
    /// @param aIndex index of first column to load
    /// @param aCommonFlagsP flag word already containing flags from superclasses (which are included in a flagword saved by subclasses)
    /// @note the base class loads ROWID and the parent identifier (first item in keyDefs) automatically.
    ///   subclasses should always call inherited loadFromRow() FIRST, if they actually have a parent ID
    ///   (otherwise, they should call loadFromRowWithoutParentId)
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);

    /// load values from passed row, which does not have a parentID
    /// @param aRow result row to get parameter values from
    /// @param aIndex index of first column to load
    /// @param aCommonFlagsP flag word already containing flags from superclasses (which are included in a flagword saved by subclasses)
    /// @note this method only loads ROWID but no parent identifier. This can be used instead of loadFromRow() for datasets with no parentId
    void loadFromRowWithoutParentId(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);

    /// bind values to passed statement
    /// @param aStatement statement to bind parameter values to
    /// @param aIndex index of first column to bind, will be incremented past the last bound column
    /// @param aCommonFlags flag word already containing flags from superclasses (which are included in a flagword saved by subclasses)
    /// @note the base class binds ROWID and the parent identifier (first item in keyDefs) automatically.
    ///   subclasses should always call inherited bindToStatement() FIRST
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

    /// load child parameters (if any)
    virtual ErrorPtr loadChildren() { return ErrorPtr(); };

    /// save child parameters (if any)
    virtual ErrorPtr saveChildren() { return ErrorPtr(); };

    /// delete child parameters (if any)
    virtual ErrorPtr deleteChildren() { return ErrorPtr(); };

    /// @}

    /// mark the parameter set dirty (so it will be saved to DB next time saveToStore is called
    virtual void markDirty();

    /// mark the parameter set clean (e.g. after setting default values that must not be saved, but which trigger dirty flag)
    virtual void markClean();

    /// @return true if needs to be saved
    bool isDirty() { return mDirty; }

    /// set a variable representing a persistent value and mark PersistentParams object dirty if value has changed
    /// @param aTargetValue the variable to update
    /// @param aNewValue the new value
    /// @note marks PersistentParams dirty if variable is actually changed
    template<typename T> bool setPVar(T &aTargetValue, T aNewValue)
    {
      if (aTargetValue!=aNewValue) {
        aTargetValue = aNewValue;
        markDirty();
        return true; // changed value
      }
      return false; // not changed value
    };


    /// get parameter set from persistent storage
    /// @param aParentIdentifier identifies the parent of this parameter set (a string (G)UID or the ROWID of a parent parameter set)
    ///   (can be NULL when there is no parent parameter set)
    ErrorPtr loadFromStore(const char *aParentIdentifier);

    /// save parameter set to persistent storage if dirty
    /// @param aParentIdentifier identifies the parent of this parameter set (a string (G)UID or the ROWID of a parent parameter set)
    ///   (can be NULL when there is no parent parameter set)
    /// @param aMultipleInstancesAllowed if true, multiple instances with the same parentIdentifier are allowed. If false, DB might be cleaned
    ///   from additional instances for that parent that might exist from failed operations.
    ErrorPtr saveToStore(const char *aParentIdentifier, bool aMultipleInstancesAllowed);

    /// delete this parameter set from the store
    ErrorPtr deleteFromStore();

    /// helper for implementation of loadChildren()
    /// @return a prepared query set up to iterate through all records with a given parent identifier (if any), or NULL on error
    /// @param aParentIdentifier identifies the parent of this parameter set (a string (G)UID or the ROWID of a parent parameter set)
    ///   (can be NULL when there is no parent parameter set, which will cause the entire table to be read)
    sqlite3pp::query *newLoadAllQuery(const char *aParentIdentifier);

  private:
    /// check and update schema to hold the parameters
    void checkAndUpdateSchema();
    /// append field list
    size_t appendfieldList(string &sql, bool keyFields, bool aAppendFields, bool aWithParamAssignment);

  };


} // namespace


#endif /* defined(__p44utils__PersistentParams__) */
