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
#include "vm/vmrunevn.h"
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
            pBaseTx = mempool.lookup(txhash);
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

