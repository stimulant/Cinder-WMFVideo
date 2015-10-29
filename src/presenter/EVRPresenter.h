//////////////////////////////////////////////////////////////////////////
//
// EVRPresenter.h : Internal header for building the DLL.
// 
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//
//////////////////////////////////////////////////////////////////////////

#pragma once

#include <windows.h>
#include <intsafe.h>
#include <math.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <evr9.h>
#include <evcode.h> // EVR event codes (IMediaEventSink)

// Common helper code.
#define USE_LOGGING
#include "common/common.h"
//#include "common/registry.h"
using namespace MediaFoundationSamples;

#define CHECK_HR(hr) IF_FAILED_GOTO(hr, done)

typedef ComPtrList<IMFSample>           VideoSampleList;

// Custom Attributes

// MFSamplePresenter_SampleCounter
// Data type: UINT32
//
// Version number for the video samples. When the presenter increments the version
// number, all samples with the previous version number are stale and should be
// discarded.
static const GUID MFSamplePresenter_SampleCounter = 
{ 0xb0bb83cc, 0xf10f, 0x4e2e, { 0xaa, 0x2b, 0x29, 0xea, 0x5e, 0x92, 0xef, 0x85 } };

// MFSamplePresenter_SampleSwapChain
// Data type: IUNKNOWN
// 
// Pointer to a Direct3D swap chain.
static const GUID MFSamplePresenter_SampleSwapChain = 
{ 0xad885bd1, 0x7def, 0x414a, { 0xb5, 0xb0, 0xd3, 0xd2, 0x63, 0xd6, 0xe9, 0x6d } };


#ifndef __IMFTrackedSample_INTERFACE_DEFINED__
#define __IMFTrackedSample_INTERFACE_DEFINED__

MIDL_INTERFACE("245BF8E9-0755-40f7-88A5-AE0F18D55E17")
IMFTrackedSample : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetAllocator( 
        /* [annotation][in] */ 
        _In_  IMFAsyncCallback *pSampleAllocator,
        /* [unique][in] */ IUnknown *pUnkState) = 0;
        
};

#endif


// Project headers.
#include "PresenterHelpers.h"
#include "Scheduler.h"
#include "PresentEngine.h"
#include "Presenter.h"


