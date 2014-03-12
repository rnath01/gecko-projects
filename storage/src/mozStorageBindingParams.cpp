/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <limits.h>

#include "nsString.h"

#include "mozStorageError.h"
#include "mozStoragePrivateHelpers.h"
#include "mozStorageBindingParams.h"
#include "mozStorageBindingParamsArray.h"
#include "Variant.h"

namespace mozilla {
namespace storage {

////////////////////////////////////////////////////////////////////////////////
//// Local Helper Objects

namespace {

struct BindingColumnData
{
  BindingColumnData(sqlite3_stmt *aStmt,
                    int aColumn)
  : stmt(aStmt)
  , column(aColumn)
  {
  }
  sqlite3_stmt *stmt;
  int column;
};

////////////////////////////////////////////////////////////////////////////////
//// Variant Specialization Functions (variantToSQLiteT)

int
sqlite3_T_int(BindingColumnData aData,
              int aValue)
{
  return ::sqlite3_bind_int(aData.stmt, aData.column + 1, aValue);
}

int
sqlite3_T_int64(BindingColumnData aData,
                sqlite3_int64 aValue)
{
  return ::sqlite3_bind_int64(aData.stmt, aData.column + 1, aValue);
}

int
sqlite3_T_double(BindingColumnData aData,
                 double aValue)
{
  return ::sqlite3_bind_double(aData.stmt, aData.column + 1, aValue);
}

int
sqlite3_T_text(BindingColumnData aData,
               const nsCString& aValue)
{
  return ::sqlite3_bind_text(aData.stmt,
                             aData.column + 1,
                             aValue.get(),
                             aValue.Length(),
                             SQLITE_TRANSIENT);
}

int
sqlite3_T_text16(BindingColumnData aData,
                 const nsString& aValue)
{
  return ::sqlite3_bind_text16(aData.stmt,
                               aData.column + 1,
                               aValue.get(),
                               aValue.Length() * 2, // Length in bytes!
                               SQLITE_TRANSIENT);
}

int
sqlite3_T_null(BindingColumnData aData)
{
  return ::sqlite3_bind_null(aData.stmt, aData.column + 1);
}

int
sqlite3_T_blob(BindingColumnData aData,
               const void *aBlob,
               int aSize)
{
  return ::sqlite3_bind_blob(aData.stmt, aData.column + 1, aBlob, aSize,
                             NS_Free);

}

#include "variantToSQLiteT_impl.h"

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//// BindingParams

BindingParams::BindingParams(mozIStorageBindingParamsArray *aOwningArray,
                             Statement *aOwningStatement)
: mLocked(false)
, mOwningArray(aOwningArray)
, mOwningStatement(aOwningStatement)
{
  (void)mOwningStatement->GetParameterCount(&mParamCount);
  (void)mParameters.SetCapacity(mParamCount);
}

BindingParams::BindingParams(mozIStorageBindingParamsArray *aOwningArray)
: mLocked(false)
, mOwningArray(aOwningArray)
, mOwningStatement(nullptr)
, mParamCount(0)
{
}

AsyncBindingParams::AsyncBindingParams(
  mozIStorageBindingParamsArray *aOwningArray
)
: BindingParams(aOwningArray)
{
}

void
BindingParams::lock()
{
  NS_ASSERTION(mLocked == false, "Parameters have already been locked!");
  mLocked = true;

  // We no longer need to hold a reference to our statement or our owning array.
  // The array owns us at this point, and it will own a reference to the
  // statement.
  mOwningStatement = nullptr;
  mOwningArray = nullptr;
}

void
BindingParams::unlock(Statement *aOwningStatement)
{
  NS_ASSERTION(mLocked == true, "Parameters were not yet locked!");
  mLocked = false;
  mOwningStatement = aOwningStatement;
}

const mozIStorageBindingParamsArray *
BindingParams::getOwner() const
{
  return mOwningArray;
}

PLDHashOperator
AsyncBindingParams::iterateOverNamedParameters(const nsACString &aName,
                                               nsIVariant *aValue,
                                               void *voidClosureThunk)
{
  NamedParameterIterationClosureThunk *closureThunk =
    static_cast<NamedParameterIterationClosureThunk *>(voidClosureThunk);

  // We do not accept any forms of names other than ":name", but we need to add
  // the colon for SQLite.
  nsAutoCString name(":");
  name.Append(aName);
  int oneIdx = ::sqlite3_bind_parameter_index(closureThunk->statement,
                                              name.get());

  if (oneIdx == 0) {
    nsAutoCString errMsg(aName);
    errMsg.Append(NS_LITERAL_CSTRING(" is not a valid named parameter."));
    closureThunk->err = new Error(SQLITE_RANGE, errMsg.get());
    return PL_DHASH_STOP;
  }

  // XPCVariant's AddRef and Release are not thread-safe and so we must not do
  // anything that would invoke them here on the async thread.  As such we can't
  // cram aValue into self->mParameters using ReplaceObjectAt so that we can
  // freeload off of the BindingParams::Bind implementation.
  int rc = variantToSQLiteT(BindingColumnData(closureThunk->statement,
                                              oneIdx - 1),
                            aValue);
  if (rc != SQLITE_OK) {
    // We had an error while trying to bind.  Now we need to create an error
    // object with the right message.  Note that we special case
    // SQLITE_MISMATCH, but otherwise get the message from SQLite.
    const char *msg = "Could not covert nsIVariant to SQLite type.";
    if (rc != SQLITE_MISMATCH)
      msg = ::sqlite3_errmsg(::sqlite3_db_handle(closureThunk->statement));

    closureThunk->err = new Error(rc, msg);
    return PL_DHASH_STOP;
  }
  return PL_DHASH_NEXT;
}

////////////////////////////////////////////////////////////////////////////////
//// nsISupports

NS_IMPL_ISUPPORTS2(
  BindingParams
, mozIStorageBindingParams
, IStorageBindingParamsInternal
)


////////////////////////////////////////////////////////////////////////////////
//// IStorageBindingParamsInternal

already_AddRefed<mozIStorageError>
BindingParams::bind(sqlite3_stmt *aStatement)
{
  // Iterate through all of our stored data, and bind it.
  for (int32_t i = 0; i < mParameters.Count(); i++) {
    int rc = variantToSQLiteT(BindingColumnData(aStatement, i), mParameters[i]);
    if (rc != SQLITE_OK) {
      // We had an error while trying to bind.  Now we need to create an error
      // object with the right message.  Note that we special case
      // SQLITE_MISMATCH, but otherwise get the message from SQLite.
      const char *msg = "Could not covert nsIVariant to SQLite type.";
      if (rc != SQLITE_MISMATCH)
        msg = ::sqlite3_errmsg(::sqlite3_db_handle(aStatement));

      nsCOMPtr<mozIStorageError> err(new Error(rc, msg));
      return err.forget();
    }
  }

  return nullptr;
}

already_AddRefed<mozIStorageError>
AsyncBindingParams::bind(sqlite3_stmt * aStatement)
{
  // We should bind by index using the super-class if there is nothing in our
  // hashtable.
  if (!mNamedParameters.Count())
    return BindingParams::bind(aStatement);

  // Enumerate over everyone in the map, propagating them into mParameters if
  // we can and creating an error immediately when we cannot.
  NamedParameterIterationClosureThunk closureThunk = {this, aStatement, nullptr};
  (void)mNamedParameters.EnumerateRead(iterateOverNamedParameters,
                                       (void *)&closureThunk);

  return closureThunk.err.forget();
}


///////////////////////////////////////////////////////////////////////////////
//// mozIStorageBindingParams

NS_IMETHODIMP
BindingParams::BindByName(const nsACString &aName,
                          nsIVariant *aValue)
{
  NS_ENSURE_FALSE(mLocked, NS_ERROR_UNEXPECTED);

  // Get the column index that we need to store this at.
  uint32_t index;
  nsresult rv = mOwningStatement->GetParameterIndex(aName, &index);
  NS_ENSURE_SUCCESS(rv, rv);

  return BindByIndex(index, aValue);
}

NS_IMETHODIMP
AsyncBindingParams::BindByName(const nsACString &aName,
                               nsIVariant *aValue)
{
  NS_ENSURE_FALSE(mLocked, NS_ERROR_UNEXPECTED);

  mNamedParameters.Put(aName, aValue);
  return NS_OK;
}


NS_IMETHODIMP
BindingParams::BindUTF8StringByName(const nsACString &aName,
                                    const nsACString &aValue)
{
  nsCOMPtr<nsIVariant> value(new UTF8TextVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindStringByName(const nsACString &aName,
                                const nsAString &aValue)
{
  nsCOMPtr<nsIVariant> value(new TextVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindDoubleByName(const nsACString &aName,
                                double aValue)
{
  nsCOMPtr<nsIVariant> value(new FloatVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindInt32ByName(const nsACString &aName,
                               int32_t aValue)
{
  nsCOMPtr<nsIVariant> value(new IntegerVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindInt64ByName(const nsACString &aName,
                               int64_t aValue)
{
  nsCOMPtr<nsIVariant> value(new IntegerVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindNullByName(const nsACString &aName)
{
  nsCOMPtr<nsIVariant> value(new NullVariant());
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindBlobByName(const nsACString &aName,
                              const uint8_t *aValue,
                              uint32_t aValueSize)
{
  NS_ENSURE_ARG_MAX(aValueSize, INT_MAX);
  std::pair<const void *, int> data(
    static_cast<const void *>(aValue),
    int(aValueSize)
  );
  nsCOMPtr<nsIVariant> value(new BlobVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByName(aName, value);
}


NS_IMETHODIMP
BindingParams::BindAdoptedBlobByName(const nsACString &aName,
                                     uint8_t *aValue,
                                     uint32_t aValueSize)
{
  NS_ENSURE_ARG_MAX(aValueSize, INT_MAX);
  std::pair<uint8_t *, int> data(
    aValue,
    int(aValueSize)
  );
  nsCOMPtr<nsIVariant> value(new AdoptedBlobVariant(data));

  return BindByName(aName, value);
}

NS_IMETHODIMP
BindingParams::BindByIndex(uint32_t aIndex,
                           nsIVariant *aValue)
{
  NS_ENSURE_FALSE(mLocked, NS_ERROR_UNEXPECTED);
  ENSURE_INDEX_VALUE(aIndex, mParamCount);

  // Store the variant for later use.
  NS_ENSURE_TRUE(mParameters.ReplaceObjectAt(aValue, aIndex),
                 NS_ERROR_OUT_OF_MEMORY);
  return NS_OK;
}

NS_IMETHODIMP
AsyncBindingParams::BindByIndex(uint32_t aIndex,
                                nsIVariant *aValue)
{
  NS_ENSURE_FALSE(mLocked, NS_ERROR_UNEXPECTED);
  // In the asynchronous case we do not know how many parameters there are to
  // bind to, so we cannot check the validity of aIndex.

  // Store the variant for later use.
  NS_ENSURE_TRUE(mParameters.ReplaceObjectAt(aValue, aIndex),
                 NS_ERROR_OUT_OF_MEMORY);
  return NS_OK;
}

NS_IMETHODIMP
BindingParams::BindUTF8StringByIndex(uint32_t aIndex,
                                     const nsACString &aValue)
{
  nsCOMPtr<nsIVariant> value(new UTF8TextVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindStringByIndex(uint32_t aIndex,
                                 const nsAString &aValue)
{
  nsCOMPtr<nsIVariant> value(new TextVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindDoubleByIndex(uint32_t aIndex,
                                 double aValue)
{
  nsCOMPtr<nsIVariant> value(new FloatVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindInt32ByIndex(uint32_t aIndex,
                                int32_t aValue)
{
  nsCOMPtr<nsIVariant> value(new IntegerVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindInt64ByIndex(uint32_t aIndex,
                                int64_t aValue)
{
  nsCOMPtr<nsIVariant> value(new IntegerVariant(aValue));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindNullByIndex(uint32_t aIndex)
{
  nsCOMPtr<nsIVariant> value(new NullVariant());
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindBlobByIndex(uint32_t aIndex,
                               const uint8_t *aValue,
                               uint32_t aValueSize)
{
  NS_ENSURE_ARG_MAX(aValueSize, INT_MAX);
  std::pair<const void *, int> data(
    static_cast<const void *>(aValue),
    int(aValueSize)
  );
  nsCOMPtr<nsIVariant> value(new BlobVariant(data));
  NS_ENSURE_TRUE(value, NS_ERROR_OUT_OF_MEMORY);

  return BindByIndex(aIndex, value);
}

NS_IMETHODIMP
BindingParams::BindAdoptedBlobByIndex(uint32_t aIndex,
                                      uint8_t *aValue,
                                      uint32_t aValueSize)
{
  NS_ENSURE_ARG_MAX(aValueSize, INT_MAX);
  std::pair<uint8_t *, int> data(
    static_cast<uint8_t *>(aValue),
    int(aValueSize)
  );
  nsCOMPtr<nsIVariant> value(new AdoptedBlobVariant(data));

  return BindByIndex(aIndex, value);
}

} // namespace storage
} // namespace mozilla
