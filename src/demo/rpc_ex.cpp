// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2017-2018 WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "txdb.h"

#include "base58.h"
#include "rpc/rpcserver.h"
#include "init.h"
#include "net.h"
#include "netbase.h"
#include "util.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "syncdatadb.h"
//#include "checkpoints.h"
#include "configuration.h"
#include "miner.h"
#include "main.h"
#include "vm/script.h"
#include "vm/vmrunenv.h"
#include <stdint.h>

#include <boost/assign/list_of.hpp>
#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"
#include "json/json_spirit_reader.h"

#include "boost/tuple/tuple.hpp"
#define revert(height) ((height<<24) | (height << 8 & 0xff0000) |  (height>>8 & 0xff00) | (height >> 24))

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;


Value getcontractscript(const Array& params, bool fHelp) {
    if (fHelp || params.size() != 2) {
        throw runtime_error(
            "getcontractscript \"txhash\"\n"
            "\nget the transaction detail by given transaction hash.\n"
            "\nArguments:\n"
            "1.\"txhash\": (string,required) The hash of transaction.\n"
            "2.\"scriptpath\": (string required), the file path of the app script\n"
            "\nResult an object of the transaction detail\n"
            "\nResult:\n"
            "\n\"txhash\"\n"
            "\nExamples:\n"
            + HelpExampleCli("getcontractscript","\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\" \"/tmp/script.lua\"\n")
            + "\nAs json rpc call\n"
            + HelpExampleRpc("getcontractscript", "\"c5287324b89793fdf7fa97b6203dfd814b8358cfa31114078ea5981916d7a8ac\" \"/tmp/script.lua\"\n"));
    }
    uint256 txhash(uint256S(params[0].get_str()));
    std::string filePath = params[1].get_str();

    Object obj;
    CRegisterContractTx *regContractTx = NULL;
    std::shared_ptr<CBaseTransaction> pBaseTx;
    {
        LOCK(cs_main);
        CBlock genesisblock;
        CBlockIndex* pgenesisblockindex = mapBlockIndex[SysCfg().HashGenesisBlock()];
        ReadBlockFromDisk(genesisblock, pgenesisblockindex);
        assert(genesisblock.GetHashMerkleRoot() == genesisblock.BuildMerkleTree());
        for (unsigned int i = 0; i < genesisblock.vptx.size(); ++i) {
            if (txhash == genesisblock.GetTxHash(i)) {
                if(genesisblock.vptx[i]->nTxType != REG_CONT_TX) {
                    throw JSONRPCError(-1001, "the tx is not registercontracttx");
                }
                regContractTx = (CRegisterContractTx *)genesisblock.vptx[i].get();
            }
        }

        if(regContractTx == NULL) {
            if (SysCfg().IsTxIndex()) {
                CDiskTxPos postx;
                if (pScriptDBTip->ReadTxIndex(txhash, postx)) {
                    CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
                    CBlockHeader header;
                    try {
                        file >> header;
                        fseek(file, postx.nTxOffset, SEEK_CUR);
                        file >> pBaseTx;
                        if(pBaseTx->nTxType != REG_CONT_TX) {
                            throw JSONRPCError(-1001, "the tx is not registercontracttx");
                        }
                        regContractTx = (CRegisterContractTx *)pBaseTx.get();

                    } catch (std::exception &e) {
                        throw runtime_error(tfm::format("%s : Deserialize or I/O error - %s", __func__, e.what()).c_str());
                    }                    
                }
            }

        }

        if(regContractTx == NULL) {
            pBaseTx = mempool.Lookup(txhash);
            if (!(pBaseTx)) {
                regContractTx = (CRegisterContractTx *)pBaseTx.get();
            }
        }

        if(regContractTx == NULL) {
            throw JSONRPCError(-1002, "the tx is not existed");
        }

        CVmScript vmScript;
        CDataStream stream(regContractTx->script, SER_DISK, CLIENT_VERSION);
        try {
            stream >> vmScript;
        } catch (std::exception &e) {
            throw JSONRPCError(-1003, std::string("parse vmScript err") + e.what());
        }
        if(!vmScript.IsValid())
            throw JSONRPCError(-1004, "vmScript invalid");
        
        FILE* file = fopen(filePath.c_str(), "w");
        if (file != NULL) {
            fwrite(&vmScript.Rom[0], 1, vmScript.Rom.size(), file);
            fclose(file);        
        }
    }
    return obj;    
}

typedef boost::variant<CNullID, int64_t, std::string> TableVariant;

typedef std::map<std::string, TableVariant> TableRowMap;


class TableInt {
public:
    bool isSet;
    int64_t value;
};

class TableString {
public:
    bool isSet;
    std::string value;
};

typedef std::vector<std::string> TableFields;

const TableFields txFields = {
    "id",
    "hash",
    "Height",
    "txIndex",
    "txType",
    "version",
    "validHeight",
    "runStep",
    "fuelRate",
    "userIdType",
    "userId",
    "fees",
    "minerIdType",
    "minerId",
    "destIdType",
    "destId",
    "values",
    "contract",
    "scriptSize",
    "scriptFile",
    "description",
    "voteCount",
    "signature"
};

void SaveRow(std::ofstream &f, TableRowMap &row, TableFields fields) {
    for (size_t i = 0; i < fields.size(); i++) {
        auto it = row.find(fields[i]);
        if (i > 0) {
            f << ",";
        }
        if (it == row.end()) {
            f << "\\N";
        } else {
            auto value = std::get<1>(*it);
            if (value.type() == typeid(int64_t)) {
                f << boost::get<int64_t>(value);
            } else {
                f << "\"" << boost::get<std::string>(value) << "\"";
            }
        }
    }
    f << std::endl;

}

const TableFields voteFields = {
    "id",
    "height",
    "txIndex",
    "destPubKey",
    "value"
};

const int txTypeNamesCount = 7;

const std::string TxTypeNames[txTypeNamesCount] = {
    "UnkownTx",
    "RewardTx",             // REWARD_TX   = 1,  //!< reward tx
    "RegisterAccountTx",    // REG_ACCT_TX = 2,  //!< tx that used to register account
    "CommonTx",             // COMMON_TX   = 3,  //!< transfer coin from one account to another
    "CallContractTx",       // CONTRACT_TX = 4,  //!< contract tx
    "RegisterContractTx",   // REG_CONT_TX = 5,  //!< register contract
    "DelegateTx"            // DELEGATE_TX = 6,  //!< delegate tx    
};

std::string GetTxTypeName(int txType) {
    std::string ret = TxTypeNames[0];
    if (txType >= 0 && txType < txTypeNamesCount) {
        ret = TxTypeNames[txType];
    }
    return ret;
}


const std::string UID_NAME_REG_ID = "RegId";
const std::string UID_NAME_KEY_ID = "KeyId";
const std::string UID_NAME_PUB_KEY = "PubKey";


//typedef boost::variant<CNullID, CRegID, CKeyID, CPubKey> CUserID;
std::string  GetUidTypeName(CUserID id) { 
    if (id.type() == typeid(CRegID)) {
        return UID_NAME_REG_ID;
    } else if (id.type() == typeid(CKeyID)) {
        return UID_NAME_KEY_ID;
    } else if (id.type() == typeid(CPubKey)) {
        return UID_NAME_PUB_KEY;
    } else {
        return ""; 
    }
}

//typedef boost::variant<CNullID, CRegID, CKeyID, CPubKey> CUserID;
std::string  GetUidString(CUserID id) { 
    if (id.type() == typeid(CRegID)) {
        return boost::get<CRegID>(id).ToString();
    } else if (id.type() == typeid(CKeyID)) {
        return boost::get<CKeyID>(id).ToString();
    } else if (id.type() == typeid(CPubKey)) {
        return boost::get<CPubKey>(id).ToString();
    } else {
        return "";
    }
}

Value exportblockdata(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error("exportblockdata \"dir\"\n"
            "\nImports keys from a wallet dump file (see dumpwallet).\n"
            "\nArguments:\n"
            "1. \"dir\"    (string, required) The dir where blocks will be exported\n"
            "\nExamples:\n"
            "\n"
            + HelpExampleCli("exportblockdata", "\"dir\"") +
            "\nImport the wallet\n"
            + HelpExampleCli("exportblockdata", "\"dir\"") +
            "\nImport using the json rpc call\n"
            + HelpExampleRpc("exportblockdata", "\"dir\"")
        );

    //    LOCK2(cs_main, pwalletMain->cs_wallet);

    //EnsureWalletIsUnlocked();

    std::string dir = params[0].get_str();

    filesystem::file_status dirStatus = filesystem::status(dir);
    if (!filesystem::exists(dirStatus)) {
        filesystem::create_directories(dir);
    } else if (!filesystem::is_directory(dirStatus)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "The " + dir + " is exists but is not directory");
    }

    std::string scriptDir = dir + "/scripts";

    filesystem::file_status scriptDirStatus = filesystem::status(scriptDir);
    if (!filesystem::exists(scriptDirStatus)) {
        filesystem::create_directories(scriptDir);
    } else if (!filesystem::is_directory(scriptDirStatus)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "The " + scriptDir + " is exists but is not directory");
    }

    std::string blockPath = dir + "/" + "blocks.csv";
    std::ofstream blockFile;
    blockFile.open(blockPath.c_str(), ios::out | ios::trunc);
    if (!blockFile.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open " + blockPath + " file for writing");

    std::string txPath = dir + "/" + "transactions.csv";
    std::ofstream txFile;
    txFile.open(txPath.c_str(), ios::out | ios::trunc);
    if (!blockFile.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open " + txPath + " file for writing");

    std::string votePath = dir + "/" + "votes.csv";
    std::ofstream voteFile;
    voteFile.open(votePath.c_str(), ios::out | ios::trunc);
    if (!voteFile.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open " + votePath + " file for writing");

    blockFile << "height"  << ","
            << "hash" << ","
            << "txCount" << ","
            << "time"  << ","
            << "version" << ","
            << "nonce"  << ","
            << "fuel"  << ","
            << "fuelRate"  << ","
            << "hashPrevBlock" << ","
            << "hashMerkleRoot" << ","
            << "signature"
            << std::endl;

    // init tx fields in header row
    for (size_t i = 0; i < txFields.size(); i++) {
        if (i == 0) {
            txFile << txFields[i];
        } else {
            txFile << "," << txFields[i];
        }
    }
    txFile << std::endl;
    // init vote fields in header row
    for (size_t i = 0; i < voteFields.size(); i++) {
        if (i == 0) {
            voteFile << voteFields[i];
        } else {
            voteFile << "," << voteFields[i];
        }
    }
    voteFile << std::endl;

    int64_t txTableId = 0;
    int64_t voteTableId = 0;

    // for earch block
    for(int height = 0; height < chainActive.Height(); height++) {
        CBlockIndex* pblockindex = chainActive[height];

        CBlock block;
        if (!ReadBlockFromDisk(block, pblockindex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "ReadBlockFromDisk failed! height=" + std::to_string(height));
        }

        blockFile << block.GetHeight() << ","
                << "\"" << block.GetHash().ToString() << "\","
                << block.vptx.size() << ","
                << block.GetTime() << ","
                << block.GetVersion() << ","
                << block.GetNonce() << ","             
                << block.GetFuel() << ","
                << block.GetFuelRate() << ","
                << "\"" << block.GetHashPrevBlock().ToString() << "\","
                << "\"" << block.GetHashMerkleRoot().ToString() << "\","
                << "\"" << HexStr(block.GetSignature()) << "\""
                << std::endl;

        // for earch tx in block        
        for(size_t txIndex = 0; txIndex < block.vptx.size(); txIndex++) {
            TableRowMap txRow;
            txTableId++;
            CBaseTransaction &tx = *block.vptx[txIndex];
            txRow["id"] = txTableId;
            txRow["hash"] = tx.GetHash().ToString();
            txRow["Height"] = block.GetHeight();
            txRow["txIndex"] = txIndex;
            txRow["txType"] = GetTxTypeName(tx.nTxType);
            txRow["version"] = tx.nVersion;
            txRow["validHeight"] = tx.nValidHeight;
            txRow["runStep"] = tx.nRunStep;
            txRow["fuelRate"] = tx.nFuelRate;

            if (tx.nTxType == REWARD_TX) {
                CRewardTransaction &rewardTx = (CRewardTransaction&)tx;

                txRow["userIdType"] = GetUidTypeName(rewardTx.account);
                txRow["userId"] = GetUidString(rewardTx.account);
                txRow["values"] = rewardTx.rewardValue;
            } else if (tx.nTxType == REG_ACCT_TX) {
                CRegisterAccountTx &regAcctTx = (CRegisterAccountTx&)tx;

                txRow["userIdType"] = GetUidTypeName(regAcctTx.userId);
                txRow["userId"] = GetUidString(regAcctTx.userId);

                auto minerId = 
                txRow["minerIdType"] = GetUidTypeName(regAcctTx.minerId);
                txRow["minerId"] = GetUidString(regAcctTx.minerId);;

                txRow["fees"] = regAcctTx.llFees;
                txRow["signature"] = HexStr(regAcctTx.signature);
            } else if (tx.nTxType == COMMON_TX || tx.nTxType == CONTRACT_TX) {
                CTransaction &transaction = (CTransaction&)tx;

                txRow["userIdType"] = GetUidTypeName(transaction.srcRegId);
                txRow["userId"] = GetUidString(transaction.srcRegId);


                auto destUserId = 
                txRow["destIdType"] = GetUidTypeName(transaction.desUserId);
                txRow["destId"] = GetUidString(transaction.desUserId);
                txRow["fees"] = transaction.llFees;
                txRow["values"] = transaction.llValues;
                if (tx.nTxType == COMMON_TX) {
                    txRow["description"] = HexStr(transaction.vContract);
                } else {
                    txRow["contract"] = HexStr(transaction.vContract);
                }
                txRow["signature"] = HexStr(transaction.signature);
            } else if (tx.nTxType == REG_CONT_TX) {
                CRegisterContractTx &regContractTx = (CRegisterContractTx&)tx;

                auto userId = 
                txRow["userIdType"] = GetUidTypeName(regContractTx.regAcctId);
                txRow["userId"] = GetUidString(regContractTx.regAcctId);

                CVmScript vmScript;
                CDataStream stream(regContractTx.script, SER_DISK, CLIENT_VERSION);
                try {
                    stream >> vmScript;
                } catch (std::exception& e) {
                    throw JSONRPCError(RPC_MISC_ERROR, "Unserialize VM script error! tx height=" 
                        + std::to_string(block.GetHeight()) + " txIndex=" + std::to_string(txIndex));
                }
                std::string scriptFilePath = scriptDir + "/script" + std::to_string(block.GetHeight()) + "-" + std::to_string(txIndex);

                std::ofstream scriptFile;
                scriptFile.open(scriptFilePath.c_str(), ios::out | ios::trunc);

                if (!scriptFile.is_open())
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open " + scriptFilePath + " script file for writing");
                
                if (vmScript.Rom.size() > 0) {
                    scriptFile.write((const char*)&vmScript.Rom[0], vmScript.Rom.size() * sizeof(unsigned char));
                }
                scriptFile.close();

                txRow["scriptSize"] = vmScript.Rom.size();
                txRow["scriptFile"] = scriptFilePath;
                txRow["description"] = HexStr(vmScript.ScriptMemo);

                txRow["fees"] = regContractTx.llFees;

                txRow["signature"] = HexStr(regContractTx.signature);
            } else if (tx.nTxType == DELEGATE_TX) {
                CDelegateTransaction &delegateTx = (CDelegateTransaction&)tx;

                auto userId = 
                txRow["userIdType"] = GetUidTypeName(delegateTx.userId);
                txRow["userId"] = GetUidString(delegateTx.userId);
                txRow["voteCount"] = delegateTx.operVoteFunds.size();
                txRow["fees"] = delegateTx.llFees;
                txRow["signature"] = HexStr(delegateTx.signature);

                for(size_t seq = 0; seq < delegateTx.operVoteFunds.size(); seq++) {
                    COperVoteFund &voteFund = delegateTx.operVoteFunds[seq];
                    voteTableId++;
                    TableRowMap voteRow;
                    voteRow["id"] = voteTableId;
                    voteRow["height"] = block.GetHeight();
                    voteRow["txIndex"] = txIndex;
                    voteRow["destPubKey"] = voteFund.fund.pubKey.ToString();
                    int64_t voteValue = voteFund.fund.value;
                    if (voteFund.operType == MINUS_FUND) { 
                        voteValue = - voteValue;
                    }
                    voteRow["value"] = voteValue;
                    SaveRow(voteFile, voteRow, voteFields);
                }//for each vote in delegatetx
            } // switch tx type

            SaveRow(txFile, txRow, txFields);
        }// for earch tx in block
    }// for earch block

    voteFile.close();
    txFile.close();
    blockFile.close();
    Object obj;
    return obj;
}