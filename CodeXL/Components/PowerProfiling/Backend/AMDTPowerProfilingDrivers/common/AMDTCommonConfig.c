//===============================================================================
//
// Copyright(c) 2015 Advanced Micro Devices, Inc.All Rights Reserved
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
//=================================================================================

#include <AMDTPwrProfInternal.h>
#include <AMDTRawDataFileHeader.h>
#include <AMDTHelpers.h>
#include <AMDTSmu8Interface.h>
#include <AMDTSmu7Interface.h>
#include <AMDTAccessPmcData.h>
#ifdef AMDT_INTERNAL_COUNTERS
#include <AMDTSmu9Interface.h>
#endif
static bool g_smuAccessOk = true;

#define RETIRED_PERFORMACE_CNT 0xC00000E9
#define ACTUAL_PERFORMANCE_FREQ_CLOCK_CNT 0xC00000E8

// FillSmuAccessData: Fill the smu structure based on SMU type
bool FillSmuAccessData(SmuList* srcList, SmuList* destList)
{
    uint32 smuCnt = 0;
    SmuAccess* pAccess = NULL;
    bool ret = false;
    SmuAccessCb* pSmuFn;

    if ((NULL != srcList) && (NULL != destList) && (0 < srcList->m_count))
    {
        destList->m_count = srcList->m_count;

        for (smuCnt = 0; smuCnt < srcList->m_count; smuCnt++)
        {
            if (0 == srcList->m_info[smuCnt].m_counterMask)
            {
                DRVPRINT("No counters for smu %d", smuCnt);
                continue;
            }

            destList->m_info[smuCnt].m_packageId = srcList->m_info[smuCnt].m_packageId;
            destList->m_info[smuCnt].m_smuIpVersion = srcList->m_info[smuCnt].m_smuIpVersion;
            destList->m_info[smuCnt].m_gpuBaseAddress = srcList->m_info[smuCnt].m_gpuBaseAddress;
            destList->m_info[smuCnt].m_counterMask = srcList->m_info[smuCnt].m_counterMask;
#ifdef LINUX
            pSmuFn = (SmuAccessCb*)GetMemoryPoolBuffer(&g_sessionPool, sizeof(SmuAccessCb));
#else
            pSmuFn = (SmuAccessCb*)GetMemoryPoolBuffer(sizeof(SmuAccessCb), true);
#endif
            pAccess = &destList->m_info[smuCnt].m_access;
            memcpy(pAccess, &srcList->m_info[smuCnt].m_access, sizeof(SmuAccess));

            if (NULL != pAccess)
            {
                switch (destList->m_info[smuCnt].m_smuIpVersion)
                {
                    case SMU_IPVERSION_9_0:
                    {
#ifdef AMDT_INTERNAL_COUNTERS
                        pSmuFn->fnSmuInit = PwrSmu9SessionInitialize;
                        pSmuFn->fnSmuClose = PwrSmu9SessionClose;
                        pSmuFn->fnSmuReadCb = PwrSmu9CollectRegisterValues;
#endif
                        ret = true;
                        break;
                    }

                    case SMU_IPVERSION_8_0:
                    case SMU_IPVERSION_8_1:
                    {
                        pSmuFn->fnSmuInit = Smu8SessionInitialize;
                        pSmuFn->fnSmuClose = Smu8SessionClose;
                        pSmuFn->fnSmuReadCb = CollectSMU8RegisterValues;
                        ret = true;
                        break;
                    }

                    case SMU_IPVERSION_7_0:
                    case SMU_IPVERSION_7_1:
                    case SMU_IPVERSION_7_2:
                    case SMU_IPVERSION_7_5:
                    {
                        pSmuFn->fnSmuInit = Smu7SessionInitialize;
                        pSmuFn->fnSmuClose = Smu7SessionClose;
                        pSmuFn->fnSmuReadCb = CollectSMU7RegisterValues;
                        ret = true;
                        break;
                    }

                    default:
                        ret = false;
                        break;
                }

                if (false == ret)
                {
                    break;
                }

                pAccess->m_accessCb = (uint64)pSmuFn;
            }
        }
    }

    return ret;
}

// SetSmuAccessState: Se this flag is there is any failure while accessing smu
// it and any of the SMU configured for the profiling.
void SetSmuAccessState(bool state)
{
    g_smuAccessOk = state;
}

// GetSmuAccessState: If all configured SMU are accessible this will send true.
bool GetSmuAccessState(void)
{
    return g_smuAccessOk;
}

// ConfigureSourceProfiling: Configuration for source code profiling
void ConfigureSourceProfiling(CoreData* pCoreCfg)
{
    if (PLATFORM_ZEPPELIN != HelpPwrGetTargetPlatformId())
    {
        InitializePMCCounters(pCoreCfg->m_pmc);
    }
}

// CloseSourceProfiling: Close the configuration for source code profiling
void CloseSourceProfiling(CoreData* pCoreCfg)
{
    if (PLATFORM_ZEPPELIN != HelpPwrGetTargetPlatformId())
    {
        ResetPMCControl(pCoreCfg->m_pmc);
    }
}

// ConfigureSmu: This should be called before profiling start/stop/pause/ resume
void ConfigureSmu(SmuList* pList, bool isOn)
{
    uint32 smuCnt = 0;
    SmuInfo* pSmu = NULL;
    SmuAccessCb* pAccess = NULL;

    SetSmuAccessState(true);

    if (NULL != pList)
    {
        for (smuCnt = 0; smuCnt < pList->m_count; smuCnt++)
        {
            pSmu = &pList->m_info[smuCnt];
            pAccess = (SmuAccessCb*)pSmu->m_access.m_accessCb;

            if (NULL == pAccess)
            {
                continue;
            }

            if (true == isOn)
            {
                pAccess->fnSmuInit(pSmu);
            }
            else
            {
                pAccess->fnSmuClose(pSmu);
            }
        }
    }

}

// PwrReadZpIpcData: Get MSR based Zepplin IPC data
void PwrReadZpIpcData(uint64* pData)
{
    pData[PMC_EVENT_RETIRED_MICRO_OPS] = HelpReadMsr64(RETIRED_PERFORMACE_CNT);
    pData[PMC_EVENT_CPU_CYCLE_NOT_HALTED] = HelpReadMsr64(ACTUAL_PERFORMANCE_FREQ_CLOCK_CNT);
}

// PwrGetIpcData: Get Ipc data based on platform
void PwrGetIpcData(PmcCounters* pSrc, uint64* pData)
{
    if (PLATFORM_ZEPPELIN == HelpPwrGetTargetPlatformId())
    {
        PwrReadZpIpcData(pData);
    }
    else
    {
        ReadPmcCounterData(pSrc, pData);
        ResetPMCCounters(pSrc);
    }
}
