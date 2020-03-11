/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "extension.h"
#include "sh_memory.h"

 /**
  * @file extension.cpp
  * @brief Implement extension code here.
 */

LSF g_LSF;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_LSF);

IGameConfig *g_pGameConf;
void* g_pCallCookieEnd;

struct
{
	void* addr = nullptr;
	uint8_t memsave[5];
	
	void Save(void* pCall)
	{
		addr = pCall;
		memcpy(&memsave, pCall, sizeof(memsave));
	}
	void Restore()
	{
		if(addr)
		{
			SourceHook::SetMemAccess(addr, sizeof(memsave), SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
			
			memcpy(addr, &memsave, sizeof(memsave));
		}
	}
} original_call;



#if defined(WIN32)
	uint64_t (__thiscall* CBitRead_ReadLongLong)(void* pThis);
#else	
	uint64_t (__cdecl* CBitRead_ReadLongLong)(void* pThis);
#endif

uint64_t __cdecl CookieFIX(intptr_t pServer, CUtlVector<NetMsg_SplitPlayerConnect*>& pSplitPlayerConnectVector, void* pCBitRead)
{
	uint64_t cl_session = CBitRead_ReadLongLong(pCBitRead); //Original
	
#if defined(WIN32)
	uint64_t* m_nReservationCookie = reinterpret_cast<uint64_t*>(pServer + 0x2F0);
	uint64_t* xSession = *reinterpret_cast<uint64_t **>(pServer + 0x2F8);
#else	
	uint64_t* m_nReservationCookie = reinterpret_cast<uint64_t*>(pServer + 0x2EC);
	uint64_t* xSession = *reinterpret_cast<uint64_t **>(pServer + 0x2F4);
#endif
	
	if(*m_nReservationCookie == 0) //When there no players
	{
		*m_nReservationCookie = *xSession;
	}
	
	if(cl_session != *m_nReservationCookie) //Client has curve session
	{
		for (int i = 0; i < pSplitPlayerConnectVector.Count(); i++)
		{
			auto split = pSplitPlayerConnectVector[i];
			
			if (split->has_convars())
			{
				auto& cvars = split->convars();
				
				for (int c = 0; c < cvars.cvars_size(); c++)
				{
					auto& cvar = const_cast<CMsg_CVars_CVar&>(cvars.cvars(c));
					
					if (cvar.has_name() && cvar.name() == "cl_session")
					{
						char sBuf[64];
						snprintf(sBuf, sizeof(sBuf), "$%llx", *m_nReservationCookie);
						
						cvar.set_value(sBuf);
						
						return *m_nReservationCookie;
					}
				}
			}
		}
	}
	
	return *m_nReservationCookie;
}

__declspec(naked) void CallCookieFIX()
{
#if defined(WIN32)
	__asm push ecx // CBitRead
	__asm lea ecx, dword ptr [ebp-6Ch] //CUtlVector<NetMsg_SplitPlayerConnect*>
	__asm push ecx
	__asm push edi //CBaseServer
#else
	__asm push dword ptr [ebp-10D4h] //CUtlVector<NetMsg_SplitPlayerConnect*>
	__asm push dword ptr [ebp+0x8] //CBaseServer
#endif
	__asm call CookieFIX
#if defined(WIN32)
	__asm add esp, 12
#else	
	__asm add esp, 8
#endif
	__asm mov ecx, g_pCallCookieEnd
	__asm jmp ecx
}

bool LSF::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	char sBuf[255];
	
	if(!gameconfs->LoadGameConfigFile("LobbySessionFixer", &g_pGameConf, sBuf, sizeof(sBuf)))
	{
		snprintf(error, maxlength, "Could not read LobbySessionFixer: %s", sBuf);
		
		return false;
	}
	
	void* addr;
	
	if (!g_pGameConf->GetMemSig("CBaseServer::ProcessConnectionlessPacket", &addr))
	{
		snprintf(error, maxlength, "Failed to get CBaseServer::ProcessConnectionlessPacket function.");
		
		return false;
	}
	
	//CBitRead::ReadLongLong
#if defined(WIN32)
	intptr_t pPatch = reinterpret_cast<intptr_t>(addr) + 0x80B;
#else
	intptr_t pPatch = reinterpret_cast<intptr_t>(addr) + 0xDC3;
#endif
	
	if(*(uint8_t*)pPatch != 0xE8)
	{
		snprintf(error, maxlength, "Found not what they expected.");
		
		return false;
	}
	
	original_call.Save(reinterpret_cast<void*>(pPatch));
	
	g_pCallCookieEnd = reinterpret_cast<void*>(pPatch + 5); //Return location
	
	*reinterpret_cast<void**>(&CBitRead_ReadLongLong) = 
		reinterpret_cast<void*>(pPatch + 5 + *reinterpret_cast<int32_t*>(pPatch + 1)); //Getting original function
	
	
	SourceHook::SetMemAccess(reinterpret_cast<void*>(pPatch), 5, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
	
	*reinterpret_cast<uint8_t*>(pPatch) = 0xE9; //JMP
	*reinterpret_cast<int32_t*>(pPatch + 1) = reinterpret_cast<intptr_t>(&CallCookieFIX) - pPatch - 5; //Redirect to CallCookieFIX
	
	return true;
}

void LSF::SDK_OnUnload()
{
	original_call.Restore();
	
	gameconfs->CloseGameConfigFile(g_pGameConf);
}