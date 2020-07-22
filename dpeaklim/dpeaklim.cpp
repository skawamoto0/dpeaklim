// dpeaklim.cpp : コンソール アプリケーションのエントリ ポイントを定義します。
//

#include "stdafx.h"
#include "InpOut32.h"


#ifndef PAGE_SIZE
#if defined(_X86_)
#define PAGE_SIZE							4096
#elif defined(_AMD64_)
#define PAGE_SIZE							4096
#else
#error Not supported.
#endif
#endif

BOOL Load()
{
	BOOL bResult;
	bResult = FALSE;
#if defined(_X86_)
	if(LoadInpOut32(_T("inpout32.dll")))
		bResult = TRUE;
#elif defined(_AMD64_)
	if(LoadInpOut32(_T("inpoutx64.dll")))
		bResult = TRUE;
#else
#error Not supported.
#endif
	return bResult;
}

void Unload()
{
	UnloadInpOut32();
}

struct PHYSICAL_MEMORY_PATCH
{
	DWORD FirstBits;
	BYTE Hash[16];
	DWORD Offset;
	DWORD Mask;
	DWORD Data;
};

void GenerateHash(void* pHash, void* pData, size_t Size)
{
	DWORD a[2];
	DWORD d[4];
	memset(&d, 0, 16);
	while(Size >= sizeof(DWORD))
	{
		a[0] = *(DWORD*)pData;
		a[1] = (a[0] << 16) | (a[0] >> 16);
		d[0] += ~a[0] ^ ~d[2];
		d[1] += a[0] ^ ~d[3];
		d[2] += a[1] ^ d[1];
		d[3] += ~a[1] ^ d[0];
		pData = (BYTE*)pData + sizeof(DWORD);
		Size -= sizeof(DWORD);
	}
	if(Size > 0)
	{
		a[0] = 0;
		memcpy(&a[0], pData, Size);
		a[1] = (a[0] << 16) | (a[0] >> 16);
		d[0] += ~a[0] ^ ~d[2];
		d[1] += a[0] ^ ~d[3];
		d[2] += a[1] ^ d[1];
		d[3] += ~a[1] ^ d[0];
	}
	memcpy(pHash, &d, 16);
}

BOOL ApplyPatch(PHYSICAL_MEMORY_PATCH* pPatch, DWORD Count)
{
	BOOL bResult;
	HKEY hKey;
	DWORD TempSize;
	BYTE Temp[1024];
	DWORD MaxMemoryMap;
	DWORD MemoryMapStride;
	DWORD MemoryMap;
	DWORD_PTR MemoryAddress;
	DWORD_PTR MemorySize;
	DWORD_PTR Address;
	DWORD_PTR MapSize;
	HANDLE hMap;
	PBYTE pMap;
	DWORD_PTR MapAddress;
	BOOL bMatch;
	BOOL bHash;
	DWORD FirstBits;
	DWORD i;
	BYTE Hash[16];
	DWORD dw;
	bResult = FALSE;
	if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("HARDWARE\\RESOURCEMAP\\System Resources\\Physical Memory"), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		TempSize = 1024;
		if(RegQueryValueEx(hKey, _T(".Translated"), NULL, NULL, (LPBYTE)&Temp, &TempSize) == ERROR_SUCCESS)
		{
			if(*(DWORD*)((BYTE*)&Temp) == 0x00000001)
			{
				MaxMemoryMap = *(DWORD*)((BYTE*)&Temp + 16);
				MemoryMapStride = 0;
				if(20 + MaxMemoryMap * 16 == TempSize)
					MemoryMapStride = 16;
				else if(20 + MaxMemoryMap * 20 == TempSize)
					MemoryMapStride = 20;
				if(MemoryMapStride > 0)
				{
					for(MemoryMap = 0; MemoryMap < MaxMemoryMap; MemoryMap++)
					{
						if((*(DWORD*)((BYTE*)&Temp + 20 + MemoryMap * MemoryMapStride) & ~0x0e000004) == 0x00000103)
						{
							MemoryAddress = 0;
							memcpy(&MemoryAddress, (BYTE*)&Temp + 20 + MemoryMap * MemoryMapStride + 4, sizeof(DWORD_PTR));
							MemorySize = 0;
							memcpy(&MemorySize, (BYTE*)&Temp + 20 + MemoryMap * MemoryMapStride + 12, sizeof(DWORD));
							if(*(DWORD*)((BYTE*)&Temp + 20 + MemoryMap * MemoryMapStride) & 0x00000004)
							{
								if(*(DWORD*)((BYTE*)&Temp + 20 + MemoryMap * MemoryMapStride) & 0x02000000)
									MemorySize = MemorySize << 8;
								else if(*(DWORD*)((BYTE*)&Temp + 20 + MemoryMap * MemoryMapStride) & 0x04000000)
									MemorySize = MemorySize << 16;
								else if(*(DWORD*)((BYTE*)&Temp + 20 + MemoryMap * MemoryMapStride) & 0x08000000)
									MemorySize = MemorySize << 24;
							}
							Address = 0;
							while(Address < MemorySize)
							{
								MapSize = min(MemorySize - Address, 256 * PAGE_SIZE);
								hMap = NULL;
								pMap = MapPhysToLin((PBYTE)(MemoryAddress + Address), (DWORD)MapSize, &hMap);
								if(pMap)
								{
									MapAddress = 0;
									while(MapAddress < MapSize)
									{
										bMatch = FALSE;
										bHash = FALSE;
										FirstBits = *(DWORD*)((BYTE*)pMap + MapAddress);
										for(i = 0; i < Count; i++)
										{
											if(pPatch[i].FirstBits == FirstBits)
											{
												if(!bHash)
													GenerateHash(&Hash, (BYTE*)pMap + MapAddress, PAGE_SIZE);
												bHash = TRUE;
												if(memcmp(&Hash, &pPatch[i].Hash, 16) == 0)
												{
													memcpy(&dw, (BYTE*)pMap + MapAddress + pPatch[i].Offset, sizeof(DWORD));
													dw = (dw & ~pPatch[i].Mask) | (pPatch[i].Data & pPatch[i].Mask);
													memcpy((BYTE*)pMap + MapAddress + pPatch[i].Offset, &dw, sizeof(DWORD));
													bMatch = TRUE;
													bHash = FALSE;
													bResult = TRUE;
												}
											}
										}
										if(!bMatch)
											MapAddress += PAGE_SIZE;
									}
									UnmapPhysicalMemory(hMap, pMap);
								}
								Address += MapSize;
							}
						}
					}
				}
			}
		}
		RegCloseKey(hKey);
	}
	return bResult;
}

BOOL GenerateDisableAudioEngineLimiterPatch(PHYSICAL_MEMORY_PATCH* pPatch, DWORD* pCount, FLOAT Original, FLOAT Overriding, BOOL bRestore)
{
	BOOL bResult;
	DWORD Count;
	HANDLE hFile;
	DWORD Size;
	LPVOID pImage;
	BYTE* pBegin;
	IMAGE_DOS_HEADER* pidh;
	IMAGE_FILE_HEADER* pifh;
	IMAGE_SECTION_HEADER* pish;
	WORD i;
	LPVOID pSection;
	DWORD Page;
	DWORD Offset;
	bResult = FALSE;
	Count = 0;
	hFile = CreateFile(_T("C:\\Windows\\System32\\AudioEng.dll"), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile != INVALID_HANDLE_VALUE)
	{
		Size = GetFileSize(hFile, NULL);
		pImage = VirtualAlloc(NULL, Size, MEM_COMMIT, PAGE_READWRITE);
		if(pImage)
		{
			if(ReadFile(hFile, pImage, Size, &Size, NULL))
			{
				__try
				{
					pBegin = (BYTE*)pImage;
					pidh = (IMAGE_DOS_HEADER*)pBegin;
					if(pidh->e_magic == IMAGE_DOS_SIGNATURE)
					{
						if(*(DWORD*)(pBegin + pidh->e_lfanew) == IMAGE_NT_SIGNATURE)
						{
							pifh = (IMAGE_FILE_HEADER*)(pBegin + pidh->e_lfanew + sizeof(DWORD));
							pish = (IMAGE_SECTION_HEADER*)((BYTE*)pifh + IMAGE_SIZEOF_FILE_HEADER + pifh->SizeOfOptionalHeader);
							for(i = 0; i < pifh->NumberOfSections; i++)
							{
								if(memcmp(&pish[i].Name, ".text\0\0\0", IMAGE_SIZEOF_SHORT_NAME * sizeof(BYTE)) == 0)
								{
									pSection = VirtualAlloc(NULL, pish[i].SizeOfRawData, MEM_COMMIT, PAGE_READWRITE);
									if(pSection)
									{
										memcpy(pSection, pBegin + pish[i].PointerToRawData, pish[i].SizeOfRawData);
										for(Page = 0; Page < (pish[i].SizeOfRawData + (PAGE_SIZE - 1)) / PAGE_SIZE; Page++)
										{
											for(Offset = 0; Offset < PAGE_SIZE - (sizeof(DWORD) - 1); Offset++)
											{
												if(memcmp((BYTE*)pSection + Page * PAGE_SIZE + Offset, &Original, sizeof(DWORD)) == 0)
												{
													if(Count < *pCount)
													{
														if(bRestore)
															memcpy((BYTE*)pSection + Page * PAGE_SIZE + Offset, &Overriding, sizeof(DWORD));
														pPatch[Count].FirstBits = *(DWORD*)((BYTE*)pSection + Page * PAGE_SIZE);
														GenerateHash(pPatch[Count].Hash, (BYTE*)pSection + Page * PAGE_SIZE, PAGE_SIZE);
														pPatch[Count].Offset = Offset;
														pPatch[Count].Mask = ~0;
														if(bRestore)
															memcpy(&pPatch[Count].Data, &Original, sizeof(DWORD));
														else
															memcpy(&pPatch[Count].Data, &Overriding, sizeof(DWORD));
														memcpy((BYTE*)pSection + Page * PAGE_SIZE + Offset, &pPatch[Count].Data, sizeof(DWORD));
													}
													Count++;
													bResult = TRUE;
												}
											}
										}
										memset(pSection, 0, pish[i].SizeOfRawData);
										VirtualFree(pSection, pish[i].SizeOfRawData, MEM_DECOMMIT);
									}
								}
							}
						}
					}
				}
				__except(true)
				{
				}
			}
			memset(pImage, 0, Size);
			VirtualFree(pImage, Size, MEM_DECOMMIT);
		}
		CloseHandle(hFile);
	}
	*pCount = Count;
	return bResult;
}

BOOL RestartWindowsAudioService()
{
	BOOL bResult;
	SC_HANDLE hSC;
	SC_HANDLE hService;
	SERVICE_STATUS sc;
	bResult = FALSE;
	hSC = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT);
	if(hSC)
	{
		hService = OpenService(hSC, _T("AudioSrv"), SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);
		if(hService)
		{
			if(QueryServiceStatus(hService, &sc))
			{
				if(sc.dwCurrentState == SERVICE_RUNNING)
				{
					if(ControlService(hService, SERVICE_CONTROL_STOP, &sc))
					{
						do
						{
							Sleep(100);
							QueryServiceStatus(hService, &sc);
						}
						while(sc.dwCurrentState == SERVICE_STOP_PENDING);
					}
				}
			}
			if(StartService(hService, 0, NULL))
				bResult = TRUE;
			CloseServiceHandle(hService);
		}
		CloseServiceHandle(hSC);
	}
	return bResult;
}

BOOL DisableAudioEngineLimiter(FLOAT Original, FLOAT Overriding, BOOL bRestore)
{
	BOOL bResult;
	DWORD Count;
	PHYSICAL_MEMORY_PATCH Patch[256];
	bResult = FALSE;
	if(Load())
	{
		_putts(_T("Restarting Windows Audio Engine..."));
		RestartWindowsAudioService();
		Count = 256;
		_putts(_T("Generating system memory patch..."));
		if(GenerateDisableAudioEngineLimiterPatch(Patch, &Count, Original, Overriding, bRestore))
		{
			_putts(_T("Applying patch..."));
			if(ApplyPatch(Patch, Count))
			{
				_putts(_T("Restarting Windows Audio Engine..."));
				if(RestartWindowsAudioService())
					bResult = TRUE;
			}
		}
		Unload();
	}
	return bResult;
}

int _tmain(int argc, _TCHAR* argv[])
{
	FLOAT Original;
	FLOAT Overriding;
	BOOL bRestore;
	int i;
	TCHAR* p;
	DOUBLE d;
	_putts(_T("Disable Peak Limiter in Windows Audio Engine Ver 1.2"));
	_putts(_T("Copyright (C) 2013-2019 Suguru Kawamoto"));
	_putts(_T(""));
	_putts(_T("Usage"));
	_putts(_T("dpeaklim.exe [-from:<value>[dB]] [-to:<value>[dB]] [-restore]"));
	_putts(_T(""));
	_putts(_T("Options"));
	_putts(_T("-from:"));
	_putts(_T("    Changes the original threshold value of the peak limiter."));
	_putts(_T("    0.985 (-0.13dB, the system default) will be used if it is not specified."));
	_putts(_T("    This option normally does not need to be specified."));
	_putts(_T("-to:"));
	_putts(_T("    Changes the overriding threshold value of the peak limiter."));
	_putts(_T("    1.0 (0dB) will be used if it is not specified."));
	_putts(_T("value"));
	_putts(_T("    Sets the threshold value."));
	_putts(_T("    An infinity will be used if a negative value is specified."));
	_putts(_T("dB"));
	_putts(_T("    Indicates the threshold value is expressed in dB."));
	_putts(_T("-restore"));
	_putts(_T("    Restores the threshold value of the peak limiter."));
	_putts(_T("    Values must be identical to those which are specified last time."));
	_putts(_T(""));
	_putts(_T("Examples"));
	_putts(_T("dpeaklim.exe"));
	_putts(_T("    The peak limiter works only when the audio is clipped."));
	_putts(_T("dpeaklim.exe -to:-0.05dB"));
	_putts(_T("    The peak limiter works if the audio level exceeds -0.05dB."));
	_putts(_T("dpeaklim.exe -to:-1"));
	_putts(_T("    The peak limiter should never work."));
	_putts(_T("dpeaklim.exe -restore"));
	_putts(_T("    The peak limiter which was once disabled works as well as the original."));
	_putts(_T(""));
	Original = 0.985f;
	Overriding = 1.0f;
	bRestore = FALSE;
	for(i = 1; i < argc; i++)
	{
		p = argv[i];
		if(_tcsnicmp(p, _T("-from:"), _tcslen(_T("-from:"))) == 0)
		{
			p += _tcslen(_T("-from:"));
			d = _tcstod(p, &p);
			if(_tcsicmp(p, _T("dB")) == 0)
				d = pow(10.0, d / 20.0);
			if(d < 0.0)
				d = pow(0.0, -1.0);
			if(d == 0.0)
			{
				_putts(_T("Invalid parameter."));
				break;
			}
			Original = (FLOAT)d;
		}
		else if(_tcsnicmp(p, _T("-to:"), _tcslen(_T("-to:"))) == 0)
		{
			p += _tcslen(_T("-to:"));
			d = _tcstod(p, &p);
			if(_tcsicmp(p, _T("dB")) == 0)
				d = pow(10.0, d / 20.0);
			if(d < 0.0)
				d = pow(0.0, -1.0);
			if(d == 0.0)
			{
				_putts(_T("Invalid parameter."));
				break;
			}
			Overriding = (FLOAT)d;
		}
		else if(_tcsicmp(p, _T("-restore")) == 0)
			bRestore = TRUE;
		else
		{
			_putts(_T("Invalid parameter."));
			break;
		}
	}
	if(i == argc)
	{
		if(DisableAudioEngineLimiter(Original, Overriding, bRestore))
			_putts(_T("Finished successfully."));
		else
			_putts(_T("Failed."));
	}
	return 0;
}

