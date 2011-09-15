#include "AppInfo.h"
#include "FileUtils.h"
#include "Log.h"
#include "Platform.h"
#include "ProcessUtils.h"
#include "StringUtils.h"
#include "UpdateScript.h"
#include "UpdaterOptions.h"

#include "tinythread.h"

#if defined(PLATFORM_LINUX)
  #include "UpdateDialogGtkWrapper.h"
  #include "UpdateDialogAscii.h"
#endif

#if defined(PLATFORM_MAC)
  #include "MacBundle.h"
  #include "UpdateDialogCocoa.h"
#endif

#if defined(PLATFORM_WINDOWS)
  #include "UpdateDialogWin32.h"
#endif

#include <iostream>

#define UPDATER_VERSION "0.6"

void runWithUi(int argc, char** argv, UpdateInstaller* installer);

void runUpdaterThread(void* arg)
{
#ifdef PLATFORM_MAC
	// create an autorelease pool to free any temporary objects
	// created by Cocoa whilst handling notifications from the UpdateInstaller
	void* pool = UpdateDialogCocoa::createAutoreleasePool();
#endif

	try
	{
		UpdateInstaller* installer = static_cast<UpdateInstaller*>(arg);
		installer->run();
	}
	catch (const std::exception& ex)
	{
		LOG(Error,"Unexpected exception " + std::string(ex.what()));
	}

#ifdef PLATFORM_MAC
	UpdateDialogCocoa::releaseAutoreleasePool(pool);
#endif
}

#ifdef PLATFORM_MAC
extern unsigned char Info_plist[];
extern unsigned int Info_plist_len;

extern unsigned char mac_icns[];
extern unsigned int mac_icns_len;

bool unpackBundle(int argc, char** argv)
{
	MacBundle bundle(FileUtils::tempPath(),AppInfo::name());
	std::string currentExePath = ProcessUtils::currentProcessPath();

	if (currentExePath.find(bundle.bundlePath()) != std::string::npos)
	{
		// already running from a bundle
		return false;
	}
	LOG(Info,"Creating bundle " + bundle.bundlePath());

	// create a Mac app bundle
	std::string plistContent(reinterpret_cast<const char*>(Info_plist),Info_plist_len);
	std::string iconContent(reinterpret_cast<const char*>(mac_icns),mac_icns_len);
	bundle.create(plistContent,iconContent,ProcessUtils::currentProcessPath());

	std::list<std::string> args;
	for (int i = 1; i < argc; i++)
	{
		args.push_back(argv[i]);
	}
	ProcessUtils::runSync(bundle.executablePath(),args);
	return true;
}
#endif

int main(int argc, char** argv)
{
#ifdef PLATFORM_MAC
	void* pool = UpdateDialogCocoa::createAutoreleasePool();
#endif
	
	Log::instance()->open(AppInfo::logFilePath());

#ifdef PLATFORM_MAC
	// when the updater is run for the first time, create a Mac app bundle
	// and re-launch the application from the bundle.  This permits
	// setting up bundle properties (such as application icon)
	if (unpackBundle(argc,argv))
	{
		return 0;
	}
#endif

	UpdaterOptions options;
	options.parse(argc,argv);
	if (options.showVersion)
	{
		std::cout << "Update installer version " << UPDATER_VERSION << std::endl;
		return 0;
	}

	UpdateInstaller installer;
	UpdateScript script;

	if (!options.scriptPath.empty())
	{
		script.parse(FileUtils::makeAbsolute(options.scriptPath.c_str(),options.packageDir.c_str()));
	}

	LOG(Info,"started updater. install-dir: " + options.installDir
	         + ", package-dir: " + options.packageDir
	         + ", wait-pid: " + intToStr(options.waitPid)
	         + ", script-path: " + options.scriptPath
	         + ", mode: " + intToStr(options.mode));

	installer.setMode(options.mode);
	installer.setInstallDir(options.installDir);
	installer.setPackageDir(options.packageDir);
	installer.setScript(&script);
	installer.setWaitPid(options.waitPid);
	installer.setForceElevated(options.forceElevated);

	if (options.mode == UpdateInstaller::Main)
	{
		runWithUi(argc,argv,&installer);
	}
	else
	{
		installer.run();
	}

#ifdef PLATFORM_MAC
	UpdateDialogCocoa::releaseAutoreleasePool(pool);
#endif

	return 0;
}

#ifdef PLATFORM_LINUX
void runWithUi(int argc, char** argv, UpdateInstaller* installer)
{
	UpdateDialogAscii asciiDialog;
	UpdateDialogGtkWrapper dialog;
	bool useGtk = dialog.init(argc,argv);
	if (useGtk)
	{
		installer->setObserver(&dialog);
	}
	else
	{
		asciiDialog.init();
		installer->setObserver(&asciiDialog);
	}
	tthread::thread updaterThread(runUpdaterThread,installer);
	if (useGtk)
	{
		dialog.exec();
	}
	updaterThread.join();
}
#endif

#ifdef PLATFORM_MAC
void runWithUi(int argc, char** argv, UpdateInstaller* installer)
{
	UpdateDialogCocoa dialog;
	installer->setObserver(&dialog);
	dialog.init();
	tthread::thread updaterThread(runUpdaterThread,installer);
	dialog.exec();
	updaterThread.join();
}
#endif

#ifdef PLATFORM_WINDOWS
// application entry point under Windows
int CALLBACK WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine,
                     int nCmdShow)
{
	int argc = 0;
	char** argv;
	ProcessUtils::convertWindowsCommandLine(GetCommandLineW(),argc,argv);
	return main(argc,argv);
}

void runWithUi(int argc, char** argv, UpdateInstaller* installer)
{
	UpdateDialogWin32 dialog;
	installer->setObserver(&dialog);
	dialog.init();
	tthread::thread updaterThread(runUpdaterThread,installer);
	dialog.exec();
	updaterThread.join();
}
#endif
