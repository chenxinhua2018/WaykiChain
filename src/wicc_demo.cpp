// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The WaykiChain developers
// Copyright (c) 2016 The Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "./rpc/rpcserver.h"
#include "./rpc/rpcclient.h"
#include "init.h"
#include "main.h"
#include "noui.h"
#include "ui_interface.h"
#include "util.h"
#include "cuiserver.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for a new crypto currency called WICC (http://www.waykichain.com),
 * which enables instant payments to anyone, anywhere in the world. WICC uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

static bool fDaemon =false;

void DetectShutdownThread(boost::thread_group* threadGroup) {
	bool fShutdown = ShutdownRequested();
	// Tell the main threads to shutdown.
	while (!fShutdown) {
		MilliSleep(200);
		fShutdown = ShutdownRequested();
	}
	if (threadGroup) {
		threadGroup->interrupt_all();
		threadGroup->join_all();
	}
	uiInterface.NotifyMessage("server closed");
	CUIServer::StopServer();
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit(int argc, char* argv[], boost::thread_group &threadGroup) {
//	boost::thread* detectShutdownThread = NULL;

	bool fRet = false;
	try {
		//
		// Parameters
		//
		// If Qt is used, parameters/coin.conf are parsed in qt/Coin.cpp's main()
		CBaseParams::InitializeParams(argc, argv);
		SysCfg().InitialConfig();

		if (SysCfg().IsArgCount("-?") || SysCfg().IsArgCount("--help")) {
			// First part of help message is specific to coind / RPC client
			std::string strUsage = _("WaykiChain Coin Daemon") + " " + _("version") + " " + FormatFullVersion() + "\n\n"
					+ _("Usage:") + "\n" + "  coind [options]                     " + _("Start Coin Core Daemon")
					+ "\n" + _("Usage (deprecated, use Coin-cli):") + "\n"
					+ "  coin [options] <command> [params]  " + _("Send command to Coin Core") + "\n"
					+ "  coin [options] help                " + _("List commands") + "\n"
					+ "  coin [options] help <command>      " + _("Get help for a command") + "\n";

			strUsage += "\n" + HelpMessage();
			strUsage += "\n" + HelpMessageCli(false);

			fprintf(stdout, "%s", strUsage.c_str());
			return false;
		}

		// Command-line RPC
		bool fCommandLine = false;
		for (int i = 1; i < argc; i++)
			if (!IsSwitchChar(argv[i][0]) && !boost::algorithm::istarts_with(argv[i], "Coin:"))
				fCommandLine = true;

		if (fCommandLine) {
			int ret = CommandLineRPC(argc, argv);
			exit(ret);
		}
#ifndef WIN32
		fDaemon = SysCfg().GetBoolArg("-daemon", false);
		if (fDaemon)
		{
			fprintf(stdout, "Coin server starting\n");

			// Daemonize
			pid_t pid = fork();
			if (pid < 0)
			{
				fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
				return false;
			}
			if (pid > 0) // Parent process, pid is child process id
			{
				CreatePidFile(GetPidFile(), pid);
				return true;
			}
			// Child process falls through to rest of initialization

			pid_t sid = setsid();
			if (sid < 0)
			fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
		}
#endif
		SysCfg().SoftSetBoolArg("-server", true);

		fRet = AppInit(threadGroup);
	} catch (std::exception& e) {
		PrintExceptionContinue(&e, "AppInit()");
	} catch (...) {
		PrintExceptionContinue(NULL, "AppInit()");
	}

	return fRet;
}
std::tuple<bool, boost::thread*> RunCoin(int argc, char* argv[])
{
	boost::thread* detectShutdownThread = NULL;
	static boost::thread_group threadGroup;
	SetupEnvironment();

	bool fRet = false;

	// Connect coind signal handlers
	noui_connect();

	fRet = AppInit(argc, argv,threadGroup);

	detectShutdownThread = new boost::thread(boost::bind(&DetectShutdownThread, &threadGroup));

	if (!fRet) {
		if (detectShutdownThread)
			detectShutdownThread->interrupt();

		threadGroup.interrupt_all();

		// threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
		// the startup-failure cases to make sure they don't result in a hang due to some
		// thread-blocking-waiting-for-another-thread-during-startup case
	}
  return std::make_tuple (fRet,detectShutdownThread);
}

const std::string secretStr = "12345678901234567890123456789012";
const std::vector<unsigned char> secret = std::vector<unsigned char>(secretStr.begin(), secretStr.end());

int test1() {
    CKey key;
    key.Set(secret.begin(), secret.end(), true);
    if (!key.IsValid()) {
        std::cout << "error: key invalid" << std::endl;
        return 1;
    }

    std::cout << "secret hex: " << HexStr(key.begin(), key.end()) << std::endl;
    CPrivKey privKey = key.GetPrivKey();
    std::cout << "priv key hex: " << HexStr(privKey.begin(), privKey.end()) << std::endl;
    CPubKey pubKey = key.GetPubKey();
    std::cout << "pub key hex: " << HexStr(pubKey.begin(), pubKey.end()) << std::endl;

    uint256 hash(secret);
    vector<unsigned char> vchSig;

    std::cout << "sig hash: " << HexStr(hash.begin(), hash.end()) << std::endl;
    if (!key.Sign(hash, vchSig)) {
        std::cout << "error: key.Sign failed" << std::endl;
        return 1;
    }

    std::cout << "signature hex: " << HexStr(vchSig) << std::endl;
    return 0;
}

int test2() {
    std::cout << "-----------------------------------------------" << std::endl;
    CKey key;
    key.Set(secret.begin(), secret.end(), true);
    if (!key.IsValid()) {
        std::cout << "error: key invalid" << std::endl;
        return 1;
    }

    std::string wiccSignature = "3045022100fa05d7bc263accb4e3a57091bd5c5aab6303358188cb4f2f8ed703f785642dd30220256a58de3fec38d859747aed67dc71c4d5bba6da8430c35b8410ecdc2af86bff";
    std::string btcSignature = "304402200648ea12a0a4e8ff2d0632b8c99560769eb311e062a7e2ae06e05ec7c95060530220235f8d19eda9d77cea86f5883615b80d25c103d1f3064336363ca5bbe3c9d48a";

    CPubKey pubKey = key.GetPubKey();
    if (!pubKey.IsValid()) {
        std::cout << "error: pubKey invalid" << std::endl;
        return 1;
    }
    //std::cout << "pub key hex: " << HexStr(pubKey.begin(), pubKey.end()) << std::endl;

    uint256 hash(secret);
    auto wiccSign = ParseHex(wiccSignature);
    assert(wiccSign.size() == 71);
    bool wiccRet = pubKey.Verify(hash, wiccSign);
    std::cout << "wicc verify signature: " << wiccRet << std::endl;

    auto btcSign = ParseHex(btcSignature);
    assert(btcSign.size() == 70);
    bool btcRet = pubKey.Verify(hash, btcSign);
    std::cout << "btc verify signature: " << btcRet << std::endl;

    return 0;
}

const std::string str123 = "123";
int testHash() {
    auto h1 = Hash(str123.begin(), str123.end());
    std::cout << "str123 hash: " << h1.ToString() << std::endl;
    return 0;
}

int main(int argc, char* argv[]) {
    test1();
    test2();
    testHash();
    return 0;
/*
	std::tuple<bool, boost::thread*> ret = RunCoin(argc,argv);

	boost::thread* detectShutdownThread  = std::get<1>(ret);

	bool fRet = std::get<0>(ret);

	if (detectShutdownThread) {
		detectShutdownThread->join();
		delete detectShutdownThread;
		detectShutdownThread = NULL;
	}

	Shutdown();

#ifndef WIN32
		fDaemon = SysCfg().GetBoolArg("-daemon", false);
#endif

	if (fRet && fDaemon)
		return 0;

	return (fRet ? 0 : 1);
    */
}
