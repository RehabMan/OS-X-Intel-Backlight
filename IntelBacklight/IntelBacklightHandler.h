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

#ifndef _INTEL_BACKLIGHT_HANDLER_H
#define _INTEL_BACKLIGHT_HANDLER_H

#include <IOKit/pci/IOPCIDevice.h>

#include "Common.h"
#include "BacklightHandler.h"

class IntelBacklightPanel;

class EXPORT IntelBacklightHandler2 : public BacklightHandler2
{
    OSDeclareDefaultStructors(IntelBacklightHandler2)
    typedef BacklightHandler2 super;

private:
    IOPCIDevice* m_provider;
    IOMemoryMap* m_baseMap;
    volatile void* m_baseAddr;
    IntelBacklightPanel* m_panel;
    UInt32 m_fbtype;

    // saved register values from startup...
    //REVIEW: do we need all of these?
    UInt32 m_lev2, m_levl, m_levw, m_levx, m_pchl;

    enum { kFBTypeIvySandy = 1, kFBTypeHaswellBroadwell = 2, };

public:
    // IOService
    virtual bool init();
    virtual IOService* probe(IOService* provider, SInt32* score);
    virtual bool start(IOService* provider);
    virtual void stop(IOService* provider);

    // BacklightHandler
    virtual void initBacklight(BacklightConfig* config);
    virtual void setBacklightLevel(UInt32 level);
    virtual UInt32 getBacklightLevel();
};


#endif // _INTEL_BACKLIGHT_HANDLER_H
