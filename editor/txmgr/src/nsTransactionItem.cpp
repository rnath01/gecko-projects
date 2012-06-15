/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsITransaction.h"
#include "nsTransactionStack.h"
#include "nsTransactionManager.h"
#include "nsTransactionItem.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"

nsTransactionItem::nsTransactionItem(nsITransaction *aTransaction)
    : mTransaction(aTransaction), mUndoStack(0), mRedoStack(0)
{
}

nsTransactionItem::~nsTransactionItem()
{
  delete mRedoStack;

  delete mUndoStack;
}

nsrefcnt
nsTransactionItem::AddRef()
{
  ++mRefCnt;
  NS_LOG_ADDREF(this, mRefCnt, "nsTransactionItem",
                sizeof(nsTransactionItem));
  return mRefCnt;
}

nsrefcnt
nsTransactionItem::Release() {
  --mRefCnt;
  NS_LOG_RELEASE(this, mRefCnt, "nsTransactionItem");
  if (mRefCnt == 0) {
    mRefCnt = 1;
    delete this;
    return 0;
  }
  return mRefCnt;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsTransactionItem)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_NATIVE(nsTransactionItem)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mTransaction)
  if (tmp->mRedoStack) {
    tmp->mRedoStack->DoUnlink();
  }
  if (tmp->mUndoStack) {
    tmp->mUndoStack->DoUnlink();
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_BEGIN(nsTransactionItem)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mTransaction)
  if (tmp->mRedoStack) {
    tmp->mRedoStack->DoTraverse(cb);
  }
  if (tmp->mUndoStack) {
    tmp->mUndoStack->DoTraverse(cb);
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(nsTransactionItem, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(nsTransactionItem, Release)

nsresult
nsTransactionItem::AddChild(nsTransactionItem *aTransactionItem)
{
  NS_ENSURE_TRUE(aTransactionItem, NS_ERROR_NULL_POINTER);

  if (!mUndoStack) {
    mUndoStack = new nsTransactionStack(nsTransactionStack::FOR_UNDO);
  }

  mUndoStack->Push(aTransactionItem);

  return NS_OK;
}

nsresult
nsTransactionItem::GetTransaction(nsITransaction **aTransaction)
{
  NS_ENSURE_TRUE(aTransaction, NS_ERROR_NULL_POINTER);

  NS_IF_ADDREF(*aTransaction = mTransaction);

  return NS_OK;
}

nsresult
nsTransactionItem::GetIsBatch(bool *aIsBatch)
{
  NS_ENSURE_TRUE(aIsBatch, NS_ERROR_NULL_POINTER);

  *aIsBatch = !mTransaction;

  return NS_OK;
}

nsresult
nsTransactionItem::GetNumberOfChildren(PRInt32 *aNumChildren)
{
  nsresult result;

  NS_ENSURE_TRUE(aNumChildren, NS_ERROR_NULL_POINTER);

  *aNumChildren = 0;

  PRInt32 ui = 0;
  PRInt32 ri = 0;

  result = GetNumberOfUndoItems(&ui);

  NS_ENSURE_SUCCESS(result, result);

  result = GetNumberOfRedoItems(&ri);

  NS_ENSURE_SUCCESS(result, result);

  *aNumChildren = ui + ri;

  return NS_OK;
}

nsresult
nsTransactionItem::GetChild(PRInt32 aIndex, nsTransactionItem **aChild)
{
  NS_ENSURE_TRUE(aChild, NS_ERROR_NULL_POINTER);

  *aChild = 0;

  PRInt32 numItems = 0;
  nsresult result = GetNumberOfChildren(&numItems);

  NS_ENSURE_SUCCESS(result, result);

  if (aIndex < 0 || aIndex >= numItems)
    return NS_ERROR_FAILURE;

  // Children are expected to be in the order they were added,
  // so the child first added would be at the bottom of the undo
  // stack, or if there are no items on the undo stack, it would
  // be at the top of the redo stack.

  result = GetNumberOfUndoItems(&numItems);

  NS_ENSURE_SUCCESS(result, result);

  if (numItems > 0 && aIndex < numItems) {
    NS_ENSURE_TRUE(mUndoStack, NS_ERROR_FAILURE);

    nsRefPtr<nsTransactionItem> child = mUndoStack->GetItem(aIndex);
    child.forget(aChild);
    return *aChild ? NS_OK : NS_ERROR_FAILURE;
  }

  // Adjust the index for the redo stack:

  aIndex -=  numItems;

  result = GetNumberOfRedoItems(&numItems);

  NS_ENSURE_SUCCESS(result, result);

  NS_ENSURE_TRUE(mRedoStack && numItems != 0 && aIndex < numItems, NS_ERROR_FAILURE);

  nsRefPtr<nsTransactionItem> child = mRedoStack->GetItem(aIndex);
  child.forget(aChild);
  return *aChild ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsTransactionItem::DoTransaction()
{
  if (mTransaction)
    return mTransaction->DoTransaction();
  return NS_OK;
}

nsresult
nsTransactionItem::UndoTransaction(nsTransactionManager *aTxMgr)
{
  nsresult result = UndoChildren(aTxMgr);

  if (NS_FAILED(result)) {
    RecoverFromUndoError(aTxMgr);
    return result;
  }

  if (!mTransaction)
    return NS_OK;

  result = mTransaction->UndoTransaction();

  if (NS_FAILED(result)) {
    RecoverFromUndoError(aTxMgr);
    return result;
  }

  return NS_OK;
}

nsresult
nsTransactionItem::UndoChildren(nsTransactionManager *aTxMgr)
{
  nsRefPtr<nsTransactionItem> item;
  nsresult result = NS_OK;
  PRInt32 sz = 0;

  if (mUndoStack) {
    if (!mRedoStack && mUndoStack) {
      mRedoStack = new nsTransactionStack(nsTransactionStack::FOR_REDO);
    }

    /* Undo all of the transaction items children! */
    sz = mUndoStack->GetSize();

    while (sz-- > 0) {
      item = mUndoStack->Peek();

      if (!item) {
        return NS_ERROR_FAILURE;
      }

      nsCOMPtr<nsITransaction> t;

      result = item->GetTransaction(getter_AddRefs(t));

      if (NS_FAILED(result)) {
        return result;
      }

      bool doInterrupt = false;

      result = aTxMgr->WillUndoNotify(t, &doInterrupt);

      if (NS_FAILED(result)) {
        return result;
      }

      if (doInterrupt) {
        return NS_OK;
      }

      result = item->UndoTransaction(aTxMgr);

      if (NS_SUCCEEDED(result)) {
        item = mUndoStack->Pop();
        mRedoStack->Push(item);
      }

      nsresult result2 = aTxMgr->DidUndoNotify(t, result);

      if (NS_SUCCEEDED(result)) {
        result = result2;
      }
    }
  }

  return result;
}

nsresult
nsTransactionItem::RedoTransaction(nsTransactionManager *aTxMgr)
{
  nsresult result;

  nsCOMPtr<nsITransaction> kungfuDeathGrip(mTransaction);
  if (mTransaction) {
    result = mTransaction->RedoTransaction();

    NS_ENSURE_SUCCESS(result, result);
  }

  result = RedoChildren(aTxMgr);

  if (NS_FAILED(result)) {
    RecoverFromRedoError(aTxMgr);
    return result;
  }

  return NS_OK;
}

nsresult
nsTransactionItem::RedoChildren(nsTransactionManager *aTxMgr)
{
  nsRefPtr<nsTransactionItem> item;
  nsresult result = NS_OK;

  if (!mRedoStack)
    return NS_OK;

  /* Redo all of the transaction items children! */
  PRInt32 sz = mRedoStack->GetSize();

  while (sz-- > 0) {
    item = mRedoStack->Peek();

    if (!item) {
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsITransaction> t;

    result = item->GetTransaction(getter_AddRefs(t));

    if (NS_FAILED(result)) {
      return result;
    }

    bool doInterrupt = false;

    result = aTxMgr->WillRedoNotify(t, &doInterrupt);

    if (NS_FAILED(result)) {
      return result;
    }

    if (doInterrupt) {
      return NS_OK;
    }

    result = item->RedoTransaction(aTxMgr);

    if (NS_SUCCEEDED(result)) {
      item = mRedoStack->Pop();
      mUndoStack->Push(item);
    }

    nsresult result2 = aTxMgr->DidUndoNotify(t, result);

    if (NS_SUCCEEDED(result)) {
      result = result2;
    }
  }

  return result;
}

nsresult
nsTransactionItem::GetNumberOfUndoItems(PRInt32 *aNumItems)
{
  NS_ENSURE_TRUE(aNumItems, NS_ERROR_NULL_POINTER);

  if (!mUndoStack) {
    *aNumItems = 0;
    return NS_OK;
  }

  *aNumItems = mUndoStack->GetSize();
  return *aNumItems ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsTransactionItem::GetNumberOfRedoItems(PRInt32 *aNumItems)
{
  NS_ENSURE_TRUE(aNumItems, NS_ERROR_NULL_POINTER);

  if (!mRedoStack) {
    *aNumItems = 0;
    return NS_OK;
  }

  *aNumItems = mRedoStack->GetSize();
  return *aNumItems ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsTransactionItem::RecoverFromUndoError(nsTransactionManager *aTxMgr)
{
  //
  // If this method gets called, we never got to the point where we
  // successfully called UndoTransaction() for the transaction item itself.
  // Just redo any children that successfully called undo!
  //
  return RedoChildren(aTxMgr);
}

nsresult
nsTransactionItem::RecoverFromRedoError(nsTransactionManager *aTxMgr)
{
  //
  // If this method gets called, we already successfully called
  // RedoTransaction() for the transaction item itself. Undo all
  // the children that successfully called RedoTransaction(),
  // then undo the transaction item itself.
  //

  nsresult result;

  result = UndoChildren(aTxMgr);

  if (NS_FAILED(result)) {
    return result;
  }

  if (!mTransaction)
    return NS_OK;

  return mTransaction->UndoTransaction();
}

