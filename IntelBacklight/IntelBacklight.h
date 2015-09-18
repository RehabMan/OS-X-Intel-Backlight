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

#ifndef _INTELBACKLIGHT_H
#define _INTELBACKLIGHT_H

#include <IOKit/IOService.h>
#include <IOKit/graphics/IODisplay.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLocks.h>

#include "BacklightHandler.h"
#include "IntelBacklightHandler.h"

class EXPORT IntelBacklightPanel : public IODisplayParameterHandler
{
    OSDeclareDefaultStructors(IntelBacklightPanel)
    typedef IODisplayParameterHandler super;

public:
	// IOService
    virtual bool init();
    virtual IOService* probe(IOService* provider, SInt32* score);
	virtual bool start(IOService* provider);
    virtual void stop(IOService* provider);
    virtual IOReturn setProperties(OSObject* props);

    // IODisplayParameterHandler
    virtual bool setDisplay(IODisplay* display);
    virtual bool doIntegerSet(OSDictionary* params, const OSSymbol* paramName, UInt32 value);
    virtual bool doDataSet(const OSSymbol* paramName, OSData* value);
    virtual bool doUpdate();

    // IntelBacklightPanel
    virtual void setBacklightHandler(BacklightHandler2* handler, OSDictionary* config = NULL);
    
private:
    BacklightHandler2* m_handler;
    IODisplay* m_display;
    IOACPIPlatformDevice* m_provider;

    enum { kWorkSave = 0x01, kWorkSetBrightness = 0x02 };
    IOInterruptEventSource* m_workSource;
    unsigned m_workPending;
    PRIVATE void scheduleWork(unsigned newWork);
    
    IOTimerEventSource* m_smoothTimer;
    IOCommandGate* m_cmdGate;
    IORecursiveLock* m_lock;
    int m_smoothIndex;

    bool m_hasSaveMethod;
    PRIVATE void savePrebootBrightnessLevel(UInt32 level);
    
	PRIVATE void setRawBrightnessLevel(UInt32 level);
	PRIVATE UInt32 queryRawBrightnessLevel();
    PRIVATE void setBrightnessLevel(UInt32 level);
    PRIVATE void setBrightnessLevelSmooth(UInt32 level);
	PRIVATE UInt32 findIndexForLevel(UInt32 BCLvalue);
    
    enum { kDisableSmooth = 0x01, };
    BacklightConfig m_config;

    int m_value;  // osx value
    int m_from_value; // current value working towards _value
    int m_committed_value;
    int m_saved_value;
    
    PRIVATE void processWorkQueue(IOInterruptEventSource*, int);
    PRIVATE void onSmoothTimer();
    PRIVATE void saveBrightnessLevelNVRAM(UInt32 level);
    PRIVATE UInt32 loadFromNVRAM();
    PRIVATE NOINLINE UInt32 indexForLevel(UInt32 value, UInt32* rem = NULL);
    PRIVATE NOINLINE UInt32 levelForIndex(UInt32 level);
    PRIVATE UInt32 levelForValue(UInt32 value);

    PRIVATE IOReturn setPropertiesGated(OSObject* props);
    PRIVATE OSDictionary* getConfigurationOverride(const char* method);
    PRIVATE bool loadConfiguration(OSDictionary* config);
    PRIVATE OSObject* translateArray(OSArray* array);
    PRIVATE OSObject* translateEntry(OSObject* obj);
};

#endif // _INTELBACKLIGHT_H
