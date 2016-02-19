/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 *
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include "Debug.h"
#include "Common.h"
#include "IntelBacklight.h"
#include "IntelBacklightHandler.h"

OSDefineMetaClassAndStructors(IntelBacklightHandler2, BacklightHandler2)

// macros for backlight register access
#define REG32_READ(offset)          (*(volatile UInt32*)((UInt8*)m_baseAddr+(offset)))
#define REG32_WRITE(offset,value)   ((*(volatile UInt32*)((UInt8*)m_baseAddr+(offset))) = (value))

// register offsets, refer to Intel reference for details
#define LEV2 0x48250
#define LEVL 0x48254
#define P0BL 0x70040
#define LEVW 0xc8250
#define LEVX 0xc8254
#define PCHL 0xe1180

bool IntelBacklightHandler2::init()
{
    if (!super::init())
        return false;

    m_provider = NULL;
    m_baseMap = NULL;
    m_baseAddr = NULL;
    m_panel = NULL;
    m_fbtype = 0;

    return true;
}

IOService* IntelBacklightHandler2::probe(IOService* provider, SInt32* score)
{
    if (!super::probe(provider, score))
        return NULL;

    m_provider = OSDynamicCast(IOPCIDevice, provider);
    if (!m_provider)
    {
        AlwaysLog("provider is not an IOPCIDevice... aborting\n");
        return NULL;
    }

    // setup BAR1 address...
    m_baseMap = m_provider->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!m_baseMap)
    {
        AlwaysLog("unable to map BAR1... aborting\n");
        return NULL;
    }
    m_baseAddr = reinterpret_cast<volatile void *>(m_baseMap->getVirtualAddress());
    if (!m_baseAddr)
    {
        AlwaysLog("unable to get virtual address for BAR1... aborting\n");
        return NULL;
    }
    // save copy of registers at startup
    m_lev2 = REG32_READ(LEV2);
    m_levl = REG32_READ(LEVL);
    m_levw = REG32_READ(LEVW);
    m_levx = REG32_READ(LEVX);
    m_pchl = REG32_READ(PCHL);

    OSNumber* num = OSDynamicCast(OSNumber, getProperty("kFrameBufferType"));
    if (!num)
    {
        AlwaysLog("unable to get framebuffer type\n");
        return NULL;
    }
    m_fbtype = num->unsigned32BitValue();

    return this;
}

bool IntelBacklightHandler2::start(IOService* provider)
{
    if (!super::start(provider))
        return false;

    //REVIEW: 9 second wait here... probably more than needed...
    // the "pilot error" case here is that the person did not patch for PNLF

    // setup for direct access
    IOService* service = waitForMatchingService(serviceMatching("IntelBacklightPanel"), 9000UL*1000UL*1000UL);
    if (!service)
    {
        AlwaysLog("IntelBacklightPanel not found (PNLF patch missing?)... aborting\n");
        return false;
    }
    m_panel = OSDynamicCast(IntelBacklightPanel, service);
    if (!m_panel)
    {
        AlwaysLog("Backlight service was not IntelBacklightPanel\n");
        service->release();
        return false;
    }

    // now register with IntelBacklight
    m_panel->setBacklightHandler(this, OSDynamicCast(OSDictionary, getProperty("Configuration")));

    // register service so IntelBacklightPanel can proceed...
    registerService();

    return true;
}

void IntelBacklightHandler2::stop(IOService* provider)
{
    if (m_panel)
    {
        m_panel->setBacklightHandler(NULL);
        m_panel->release();
        m_panel = NULL;
    }
    OSSafeReleaseNULL(m_baseMap);
    m_baseAddr = NULL;
    m_provider = NULL;
    m_config = NULL;

    super::stop(provider);
}

void IntelBacklightHandler2::initBacklight(BacklightConfig* config)
{
    if (!m_baseAddr)
        return;

    m_config = config;

    switch (m_fbtype)
    {
        case kFBTypeIvySandy:
        {
            // gather current settings from PWM hardware
            if (!m_config->m_pchlInit)
                m_config->m_pchlInit = m_pchl;
            if (!m_config->m_pwmMax)
                m_config->m_pwmMax = m_levx>>16;
            if (!m_config->m_pwmMax)
                m_config->m_pwmMax = m_config->m_backlightLevelsScale;

            // adjust settings of PWM hardware depending on configuration
            UInt16 pwmMax = REG32_READ(LEVX)>>16;
            if (pwmMax != m_config->m_pwmMax)
            {
                UInt32 newLevel = REG32_READ(LEVL);
                if (!pwmMax || !newLevel)
                    newLevel = pwmMax = m_config->m_pwmMax;
                newLevel *= m_config->m_pwmMax;
                newLevel /= pwmMax;
                //REVIEW: wait for vblank before setting new PWM config
                ////for (UInt32 p0bl = REG32_READ(P0BL); REG32_READ(P0BL) == p0bl; );
                if (REG32_READ(LEVL) > m_config->m_pwmMax)
                {
                    REG32_WRITE(LEVX, m_config->m_pwmMax<<16);
                    REG32_WRITE(LEVL, newLevel);
                }
                else
                {
                    REG32_WRITE(LEVL, newLevel);
                    REG32_WRITE(LEVX, m_config->m_pwmMax<<16);
                }
            }
            break;
        }

        case kFBTypeHaswellBroadwell:
        {
            // Default value for m_pchlInit is 0xC0000000...
            // This 0xC value comes from looking what OS X initializes this
            // register to after display sleep (using ACPIDebug/ACPIPoller)
            if (m_config->m_levwInit)
                REG32_WRITE(LEVW, m_config->m_levwInit);
            if (!m_config->m_pwmMax)
                m_config->m_pwmMax = m_levx>>16;
            if (!m_config->m_pwmMax)
                m_config->m_pwmMax = m_config->m_backlightLevelsScale;
            // adjust settings of PWM hardware depending on configuration
            UInt16 pwmMax = REG32_READ(LEVX)>>16;
            if (pwmMax != m_config->m_pwmMax)
            {
                DebugLog("pwmMax!=config.pwmMax, adjusting: pwmMax=%x, config.pwmMax=%x, m_levx=%x\n", pwmMax, config->m_pwmMax, m_levx>>16);
                UInt32 newLevel = REG32_READ(LEVX) & 0xFFFF;
                if (!pwmMax || !newLevel)
                    newLevel = pwmMax = m_config->m_pwmMax;
                newLevel *= m_config->m_pwmMax;
                newLevel /= pwmMax;
                //REVIEW: wait for vblank before setting new PWM config
                ////for (UInt32 p0bl = REG32_READ(P0BL); REG32_READ(P0BL) == p0bl; );
                REG32_WRITE(LEVX, (m_config->m_pwmMax<<16) | newLevel);
            }
            break;
        }
    }

    // scale levels if needed
    if (m_config->m_pwmMax != m_config->m_backlightLevelsScale)
    {
        UInt32 newLevel;
        for (int i = 0; i < m_config->m_nLevels; i++)
        {
            newLevel = m_config->m_backlightLevels[i];
            newLevel *= m_config->m_pwmMax;
            newLevel /= m_config->m_backlightLevelsScale;
            m_config->m_backlightLevels[i] = newLevel;
        }
        // scale backightMin
        newLevel = m_config->m_backlightMin;
        newLevel *= m_config->m_pwmMax;
        newLevel /= m_config->m_backlightLevelsScale;
        m_config->m_backlightMin = newLevel;
        // scale backlight Max
        newLevel = m_config->m_backlightMax;
        newLevel *= m_config->m_pwmMax;
        newLevel /= m_config->m_backlightLevelsScale;
        m_config->m_backlightMax = newLevel;
    }
}

void IntelBacklightHandler2::setBacklightLevel(UInt32 level)
{
    if (!m_baseAddr || !m_config)
        return;

    // write backlight level
    switch (m_fbtype)
    {
        case kFBTypeHaswellBroadwell:
        {
            if ((m_config->m_options & kWriteLEVWOnSet) && m_config->m_levwInit)
                REG32_WRITE(LEVW, m_config->m_levwInit);
            // store new backlight level and restore max
            REG32_WRITE(LEVX, (m_config->m_pwmMax<<16) | level);
            break;
        }

        case kFBTypeIvySandy:
        {
            // initialize for consistent backlight level before/after sleep\n
            if (m_config->m_pchlInit != -1 && REG32_READ(PCHL) != m_config->m_pchlInit)
                REG32_WRITE(PCHL, m_config->m_pchlInit);
            if (REG32_READ(LEVW) != 0x80000000)
                REG32_WRITE(LEVW, 0x80000000);
            if (REG32_READ(LEVX) != m_config->m_pwmMax<<16)
                REG32_WRITE(LEVX, m_config->m_pwmMax<<16);
            // store new backlight level
            if (REG32_READ(LEV2) != 0x80000000)
                REG32_WRITE(LEV2, 0x80000000);
            REG32_WRITE(LEVL, level);
            break;
        }
    }
}

UInt32 IntelBacklightHandler2::getBacklightLevel()
{
    if (!m_baseAddr || !m_config)
        return -1;

    // read backlight level
    UInt32 result = -1;
    switch (m_fbtype)
    {
        case kFBTypeHaswellBroadwell:
        {
            result = REG32_READ(LEVX) & 0xFFFF;
            break;
        }

        case kFBTypeIvySandy:
        {
            result = REG32_READ(LEVL);
            break;
        }
    }
    return result;
}
