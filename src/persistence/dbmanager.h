// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PERSIST_DB_MANAGER_H
#define PERSIST_DB_MANAGER_H

#include "dbconf.h"
#include "dbaccess.h"
#include "blockdb.h"

using namespace std;

struct DBAccessCacheSizes {
    size_t blockTreeCacheSize;
    size_t accountCacheSize;
    size_t contractCacheSize;
    size_t delegateCacheSize;
    size_t cdpCacheSize;
    size_t dexCacheSize;
};

class CDBAccessManager {
public:
    CBlockTreeDB blockTreeDb;
    CDBAccess accountDb;
    CDBAccess contractDb;
    CDBAccess delegateDb;
    CDBAccess cdpDb;
    CDBAccess dexDb;
public:
    CDBAccessManager(const DBAccessCacheSizes& sizes, bool fReIndex, bool fMemory):
        /* dbVariant      dbName              cacheSize                        */
        /* ---------    ---------   ------------------------                 */
        blockTreeDb    ("index",    sizes.blockTreeCacheSize, fMemory, fReIndex),
        accountDb      ("account",  sizes.accountCacheSize, fMemory, fReIndex),
        contractDb     ("contract", sizes.contractCacheSize, fMemory, fReIndex),
        delegateDb     ("delegate", sizes.delegateCacheSize, fMemory, fReIndex),
        cdpDb          ("cdp",      sizes.cdpCacheSize, fMemory, fReIndex),
        dexDb          ("dex",      sizes.dexCacheSize, fMemory, fReIndex)
    {}
};


class CDBCacheManager {
//public:
//    DB_CACHE_LIST2(DECLARE_MultiValueCache, DECLARE_ScalarValueCache)
    #define DEF_CACHE_ITEM
public:

/*       type               prefixType               key                     value                 variable               */
/*  ----------------   -------------------------   -----------------------  ------------------   ------------------------ */
    /////////// ContractDB
    // scriptRegId -> script content
    CDBMultiValueCache< dbk::CONTRACT_DEF,         string,                   string >               scriptCache;
    // txId -> vector<CVmOperate>    
    CDBMultiValueCache< dbk::CONTRACT_TX_OUT,      uint256,                  vector<CVmOperate> >   txOutputCache;          
    // keyId,height,index -> txid
    CDBMultiValueCache< dbk::LIST_KEYID_TX,        tuple<CKeyID, int, int>,  uint256>               acctTxListCache;
    // txId -> DiskTxPos        
    CDBMultiValueCache< dbk::TXID_DISKINDEX,       uint256,                  CDiskTxPos >           txDiskPosCache;         
    // contractTxId -> relatedAccounts
    CDBMultiValueCache< dbk::CONTRACT_RELATED_KID, uint256,                  set<CKeyID> >          contractRelatedKidCache;
    // pair<scriptId, scriptKey> -> scriptData
    CDBMultiValueCache< dbk::CONTRACT_DATA,        pair<string, string>,     string >               contractDataCache;
    // scriptId -> contractItemCount
    CDBMultiValueCache< dbk::CONTRACT_ITEM_NUM,    string,                   CDBCountValue >        contractItemCountCache; 
    // scriptId -> contractItemCount
    CDBMultiValueCache< dbk::CONTRACT_ACCOUNT,     pair<string, string>,     CAppUserAccount >      contractAccountCache;   

    /////////// AccountDB
    // best blockHash
    CDBScalarValueCache< dbk::BEST_BLOCKHASH,      /* none */                uint256>               blockHashCache;     
    // <KeyID -> Account>
    CDBMultiValueCache< dbk::KEYID_ACCOUNT,        CKeyID,                   CAccount >             keyId2AccountCache;     
    // <RegID str -> KeyID>
    CDBMultiValueCache< dbk::REGID_KEYID,          string,                   CKeyID >               regId2KeyIdCache;        
    // <NickID -> KeyID> 
    CDBMultiValueCache< dbk::NICKID_KEYID,         CNickID,                  CKeyID>                nickId2KeyIdCache;       
 

public:
    CDBCacheManager(CDBAccessManager *pDbAccessManager):
   
        // ContractDb
        scriptCache(&pDbAccessManager->contractDb),
        txOutputCache(&pDbAccessManager->contractDb),
        acctTxListCache(&pDbAccessManager->contractDb),
        txDiskPosCache(&pDbAccessManager->contractDb),
        contractRelatedKidCache(&pDbAccessManager->contractDb),
        contractDataCache(&pDbAccessManager->contractDb),
        contractItemCountCache(&pDbAccessManager->contractDb),
        contractAccountCache(&pDbAccessManager->contractDb),
        //AccountDb
        blockHashCache(&pDbAccessManager->accountDb),
        keyId2AccountCache(&pDbAccessManager->accountDb),
        regId2KeyIdCache(&pDbAccessManager->accountDb),
        nickId2KeyIdCache(&pDbAccessManager->accountDb),

        pBaseCacheManager(nullptr){}


    void SetBase(CDBCacheManager *pBaseCacheManagerIn) {
     
        // ContractDb
        scriptCache.SetBase(&pBaseCacheManagerIn->scriptCache);
        txOutputCache.SetBase(&pBaseCacheManagerIn->txOutputCache);
        acctTxListCache.SetBase(&pBaseCacheManagerIn->acctTxListCache);
        txDiskPosCache.SetBase(&pBaseCacheManagerIn->txDiskPosCache);
        contractRelatedKidCache.SetBase(&pBaseCacheManagerIn->contractRelatedKidCache);
        contractDataCache.SetBase(&pBaseCacheManagerIn->contractDataCache);
        contractItemCountCache.SetBase(&pBaseCacheManagerIn->contractItemCountCache);
        // AccountDb
        blockHashCache.SetBase(&pBaseCacheManagerIn->blockHashCache);
        keyId2AccountCache.SetBase(&pBaseCacheManagerIn->keyId2AccountCache);
        regId2KeyIdCache.SetBase(&pBaseCacheManagerIn->regId2KeyIdCache);
        nickId2KeyIdCache.SetBase(&pBaseCacheManagerIn->nickId2KeyIdCache);
        pBaseCacheManager = pBaseCacheManagerIn;
    }

public:
    void Flush() {
        // TODO: Flush all Cache
    }

    json_spirit::Object ToJsonObj() {
        // TODO: ToJsonObj
        return json_spirit::Object();
    }

    CDBCacheManager* GetBase() { return pBaseCacheManager; }

private:
    CDBCacheManager *pBaseCacheManager;
};

#endif//PERSIST_DB_MANAGER_H