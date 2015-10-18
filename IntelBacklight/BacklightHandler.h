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

#ifndef _BACKLIGHT_HANDLER_H
#define _BACKLIGHT_HANDLER_H

#include <IOKit/IOService.h>
#include "Common.h"

struct BacklightConfig
{
    UInt16 m_pwmMax;
    UInt32 m_pchlInit;
    UInt32 m_levwInit;
    UInt32 m_options;
    UInt16 m_backlightMin;
    UInt16 m_backlightMax;
    UInt16 m_backlightLevelsScale;
    UInt16 m_nLevels;
    UInt16* m_backlightLevels;
};

class EXPORT BacklightHandler2 : public IOService
{
    OSDeclareDefaultStructors(BacklightHandler2)
    typedef IOService super;

protected:
    BacklightConfig* m_config;

public:
    // BacklightHandler
    virtual void initBacklight(BacklightConfig* config);
    virtual void setBacklightLevel(UInt32 level);
    virtual UInt32 getBacklightLevel();
};

#endif // _BACKLIGHT_HANDLER_H
