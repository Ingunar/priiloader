/*
Preloader/Priiloader Installer - An installation utiltiy for preloader (c) 2008-2020 crediar

Copyright (c) 2020 DacoTaco

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <malloc.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

//project includes
#include "title.h"
#include "nand.h"
#include "installer.h"
#include "gecko.h"
#include "IOS.h"
#include "Video.h"
#include "vWii.h"

//rev version
#include "gitrev.h"
#include "version.h"

// Application binaries
#include "certs_bin.h"
#include "su_tmd.h"
#include "su_tik.h"

static const u64 _targetTitleId = 0x0000000100000002LL;

typedef struct init_states {
	s8 AHBPROT;
} init_states;
static init_states wii_state = {};

bool UserYesNoStop()
{
	u16 pDown;
	u16 GCpDown;
	while(1)
	{
		WPAD_ScanPads();
		PAD_ScanPads();
		pDown = WPAD_ButtonsDown(0);
		GCpDown = PAD_ButtonsDown(0);
		if (pDown & WPAD_BUTTON_A || GCpDown & PAD_BUTTON_A)
		{
			return true;
		}
		if (pDown & WPAD_BUTTON_B || GCpDown & PAD_BUTTON_B)
		{
			return false;
		}
		if (pDown & WPAD_BUTTON_HOME || GCpDown & PAD_BUTTON_START)
		{
			return false;
		}
	}
	//it should never get here, but to kill that silly warning... :)
	return false;
}
void abort(const char* msg, ...)
{
	va_list args;
	char text[4096];
	va_start( args, msg );
	strcpy( text + vsnprintf( text,4095,msg,args ),""); 
	va_end( args );
	printf("\x1b[%d;%dm", 36, 1);
	gprintf("%s -> aborting mission...", text);
	printf("%s\nPress A to exit back to loader...\r\n",text);
	UserYesNoStop();
	printf("exiting...\r\n");
	printf("\x1b[%d;%dm", 37, 1);
	VIDEO_WaitVSync();
	exit(0);
}
bool HaveNandPermissions( void )
{
	gprintf("testing permissions...");
	s32 temp = ISFS_Open("/title/00000001/00000002/content/title.tmd",ISFS_OPEN_RW);
	if ( temp < 0 )
	{
		gprintf("no permissions.error %d",temp);
		return false;
	}
	else
	{
		ISFS_Close(temp);
		gprintf("and bingo was his name-O");
		return true;
	}
}
void ClearScreen()
{
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	VIDEO_WaitVSync();
	printf("\x1b[2J");
	fflush(stdout);
	return;	
}
void InitializeSystem()
{
	if( IOS_GetVersion() >= 200 )
		throw "using a non-valid IOS(" + std::to_string(IOS_GetVersion()) + ")";

	//if we are loaded without AHBPROT being disabled we will move to IOS36, and hope for fakesign lol
	if(ReadRegister32(0x0d800064) != 0xFFFFFFFF)
	{
		ReloadIOS(36, 0);
		if ( ( IOS_GetRevision() < 200 ) || ( IOS_GetRevision() > 0xFF01 ) || ( IOS_GetVersion() != 36 ) )
			throw "IOSPAGHETTI(Cios Infected) IOS detected";
	}

	//patch and reload IOS so we don't end up with possible shit from the loading application ( looking at you HBC! >_< )
	//we don't need it in dolphin though
	s32 dolphinFd = -1; 
	s32 ret = 0;
	
	dolphinFd = IOS_Open("/dev/dolphin", IPC_OPEN_NONE); 
	if (dolphinFd >= 0)
	{
		IOS_Close(dolphinFd);
		gprintf("Dolphin detected. We don't need AHBPROT");
	}
	else if(ReadRegister32(0x0d800064) == 0xFFFFFFFF)
	{
		ret = ReloadIOS(IOS_GetVersion(), 1);
		if(ret < 0)
			throw "failed to do AHBPROT magic: error " + std::to_string(ret);
	}

	wii_state.AHBPROT = ReadRegister32(0x0d800064) == 0xFFFFFFFF || dolphinFd >= 0;
	if (wii_state.AHBPROT && dolphinFd < 0 && PatchIOS({ SetUidPatcher, NandAccessPatcher, FakeSignOldPatch, FakeSignPatch, EsIdentifyPatch }) < 0)
		throw "failed to patch IOS. is AHBPROT set correctly?";
	

	u64 titleID = 0;
	if(wii_state.AHBPROT)
	{
		ret = ES_SetUID(_targetTitleId);
		gprintf("ES_SetUID : %d",ret);
		//kinda unstable attempt to indentify as SM. it *seems* to work sometimes. but its nice if it does. dont know what triggers it to work tho :/
		//if it works, ES_Identify says ticket/tmd is invalid but identifes us as SM anyway X'D
		//but it seems to cause a crash in ipc so this is why we dont do it
		/*ret = ES_Identify( (signed_blob *)certs_bin, certs_bin_size, (signed_blob *)su_tmd, su_tmd_size, (signed_blob *)su_tik, su_tik_size, 0);
		gprintf("ES_Identify : %d",ret);*/
		/*ret = ES_GetTitleID(&titleID);
		gprintf("identified as = 0x%08X%08X",TITLE_UPPER(titleID),TITLE_LOWER(titleID));*/
	}
	else
	{
		u32 keyId = 0;
		ret = ES_Identify( (signed_blob*)certs_bin, certs_bin_size, (signed_blob*)su_tmd, su_tmd_size, (signed_blob*)su_tik, su_tik_size, &keyId);
		gprintf("ES_Identify : %d",ret);
		if(ret < 0)
			throw "Failed to Identify as SU, error " + std::to_string(ret);
	}

	ES_GetTitleID(&titleID);
	gprintf("identified as = 0x%08X%08X", TITLE_UPPER(titleID), TITLE_LOWER(titleID));

	if (ISFS_Initialize() < 0)
		throw "Failed to init ISFS";
	
	auto haveNandAccess = HaveNandPermissions();
	if(!haveNandAccess && wii_state.AHBPROT)
	{
		gprintf("attempting ios 36...");
		ReloadIOS(36, 0);
		wii_state.AHBPROT = 0;
		haveNandAccess = HaveNandPermissions();
	}

	if(!haveNandAccess)
		throw "missing nand access on IOS" + std::to_string(IOS_GetVersion()) + ".is it patched?";

	ret = SHA_Init();
	if(ret < 0)
		throw "Failed to initialise SHA engine";
}
int main(int argc, char **argv)
{
	s32 ret;
	CheckForGecko();
	InitVideoWithConsole();
	bool isvWii = CheckvWii();

	printf("IOS %d rev %d\t%s\r\n\r\n", IOS_GetVersion(), IOS_GetRevision(), isvWii ? "(vWii detected)" : "");

#if RCVERSION > 0
	printf("\t\tPriiloader v%d.%d.%dRC%d(r0x%08x) Installation/Removal Tool\n\n\n\n", VERSION.major, VERSION.minor, VERSION.patch, VERSION.sub_version, GIT_REV);
#else
	printf("\t\tPriiloader v%d.%d.%d(r0x%08x) Installation / Removal Tool\n\n\n\n", VERSION.major, VERSION.minor, VERSION.patch, GIT_REV);
#endif

	printf("\t                   PLEASE READ THIS CAREFULLY\n");
	printf("\t         THIS PROGRAM/TOOL COMES WITHOUT ANY WARRANTIES!\n");
	printf("\t        YOU ACCEPT THAT YOU INSTALL THIS AT YOUR OWN RISK\n");
	printf("\tTHE AUTHOR(S) CANNOT BE HELD LIABLE FOR ANY DAMAGE IT MIGHT CAUSE\n");
	printf("\t      IF YOU DO NOT AGREE WITH THESE TERMS TURN YOUR WII OFF\n\n\n\n");
	printf("\t                   Please wait while we init...");

	std::string msg = "";
	try
	{
		//init & investigate IOS
		InitializeSystem();

		//IOS Shit Done, prepare installer
		InitializeInstaller(_targetTitleId, isvWii);
	}
	catch (const std::string& ex)
	{
		msg = ex;
	}
	catch (char const* ex)
	{
		msg = std::string(ex);
	}
	catch(...)
	{
		msg = "Unknown Error Occurred";
	}

	WPAD_Init();
	PAD_Init();
	
	//did an error occur
	if(!msg.empty())
	{
		ClearScreen();
		abort("%s", msg.c_str());
	}

	//wait a bit for the system to settle and have all unexpected events to have happened (if any)
	sleep(5);

	printf("\r\t         Press (+/A) to install or update Priiloader\r\n");
	printf("\t    Press (-/Y) to remove Priiloader and restore system menu\r\n");
	if( !wii_state.AHBPROT && IOS_GetVersion() != 36 )
		printf("\t         Hold Down (B) with any above options to use IOS36\r\n");
	printf("\t    Press (HOME/Start) to chicken out and quit the installer!\r\n\r\n");
	printf("\t                         Enjoy! DacoTaco \r\n");

	bool reloadIOS = false;
	InstallerAction action = InstallerAction::None;
	while(1)
	{
		WPAD_ScanPads();
		PAD_ScanPads();
		u16 pDown = WPAD_ButtonsDown(0);
		u16 GCpDown = PAD_ButtonsDown(0);

		reloadIOS = (WPAD_ButtonsHeld(0) & WPAD_BUTTON_B) || (PAD_ButtonsHeld(0) & PAD_BUTTON_B);
		//install Priiloader
		if (pDown & WPAD_BUTTON_PLUS || GCpDown & PAD_BUTTON_A)
		{
			action = InstallerAction::Install;
			break;
		}
		//remove Priiloader
		else if (pDown & WPAD_BUTTON_MINUS || GCpDown & PAD_BUTTON_Y )
		{
			action = InstallerAction::Remove;
			break;
		}
		//Cancel
		else if ( GCpDown & PAD_BUTTON_START || pDown & WPAD_BUTTON_HOME) 
			break;
	}

	if(action != InstallerAction::None && reloadIOS && !wii_state.AHBPROT && IOS_GetVersion() != 36)
	{
		WPAD_Shutdown();
		ReloadIOS(36, 0);
		WPAD_Init();
		if(!HaveNandPermissions())
			abort("Failed to retrieve nand permissions from nand!ios 36 isn't patched!");
	}

	ClearScreen();
	printf("IOS %d rev %d\t%s\r\n\r\n", IOS_GetVersion(), IOS_GetRevision(), isvWii ? "(vWii detected)" : "");
	try
	{
		const char* installMsg;
		switch(action)
		{
			case InstallerAction::Install:
#if RCVERSION > 0
				printf("\x1b[%d;%dm", 33, 1);
				printf("\nWARNING : ");
				printf("\x1b[%d;%dm", 37, 1);
				printf("this is a Release Canidate. are you SURE you want to install this?\n+/A to confirm, Home/Start to abort\r\n");
				sleep(1);
				if(!UserYesNoStop())
					abort("user cancelled");
#endif
				action = PriiloaderInstalled() ? InstallerAction::Update : InstallerAction::Install;
				installMsg = action == InstallerAction::Update 
					? "Priiloader installation found.\r\nUpdating Priiloader...\r\n"
					: "Priiloader not found.\r\nInstalling Priiloader...\r\n";
				printf("%s", installMsg);
				CopyTicket();
				WritePriiloader(action);
				ret = PatchTMD(action);
				if(ret < 0)
					abort("\npatching TMD error %d!\r\n",ret);

				DeletePriiloaderFiles(action);
				abort("\r\n%s\r\n", action == InstallerAction::Update ? "Update done!" : "Install done!");
				break;
			case InstallerAction::Remove:
				if(!PriiloaderInstalled())
					abort("Priiloader not found");
				
				printf("Priiloader installation found.\n\r\n");
				RemovePriiloader();
				PatchTMD(action);
				DeletePriiloaderFiles(action);
				printf("Done!\r\n\r\n");
				abort("Removal done.\r\n");
				break;
			case InstallerAction::None:
			default:		
				abort("Installation cancelled");
		}
	}
	catch (const std::string& ex)
	{
		gprintf("str!");
		abort("%s", ex.c_str());
	}
	catch (char const* ex)
	{
		gprintf("char!");
		abort(ex);
	}
	catch(...)
	{
		gprintf("eep!");
		abort("Unknown Error Occurred");
	}

	return 0;
}
