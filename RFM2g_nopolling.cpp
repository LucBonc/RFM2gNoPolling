/**
 * @file RFM2g_nopolling.cpp
 * @.cpp file for class RFM2g
 * @date 08/03/2021
 * @authors Davide Liuzza, Luca Boncagni,  Cristian Galperti
 *
 *
 * @copyright Copyright 2021 FSN-ENEA | Nuclear and Fusion Energy Department, ENEA Frascati (Rome)
 * Italy.
 * @copyright Copyright 2019 SPC | Swiss Plasma Center, EPFL Lausanne
 * Switzerland.
 * @copyright Copyright 2015 F4E | European Joint Undertaking for ITER and
 * the Development of Fusion Energy ('Fusion for Energy').
 * Licensed under the EUPL, Version 1.1 or - as soon they will be approved
 * by the European Commission - subsequent versions of the EUPL (the "Licence")
 * You may not use this work except in compliance with the Licence.
 * You may obtain a copy of the Licence at: http://ec.europa.eu/idabc/eupl
 *
 * @warning Unless required by applicable law or agreed to in writing, 
 * software distributed under the Licence is distributed on an "AS IS"
 * basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the Licence permissions and limitations under the Licence.

 * @details This source file contains the definition of all the methods for
 * the class RFM2g (public, protected, and private). Be aware that some
 * methods, such as those inline could be defined on the header file, instead.
 */

#define DLL_API

/*---------------------------------------------------------------------------*/
/*                         Standard header includes                          */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/*                         Project header includes                           */
/*---------------------------------------------------------------------------*/
#include "AdvancedErrorManagement.h"
#include "RFM2g_nopolling.h"
#include "MemoryMapSynchronisedInputBroker.h"
#include "Threads.h"
#include "EmbeddedThreadI.h"
#include "CLASSMETHODREGISTER.h"

#include "stdio.h"
#include "signal.h"
#include "string.h"
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
/*                           Static definitions                              */
/*---------------------------------------------------------------------------*/
namespace MARTe {
/**
 * Execute in the context of the real-time thread.
 */
const uint32 RFM2G_EXEC_MODE_RTTHREAD = 1u;
/**
 * Execute in the context of a spawned thread.
 */
const uint32 RFM2G_EXEC_MODE_SPAWNED = 2u;

uint8 RFM2g::numberOfinstances = 0u;

}

/*---------------------------------------------------------------------------*/
/*                           Method definitions                              */
/*---------------------------------------------------------------------------*/
namespace MARTe {
RFM2g::RFM2g() :
        DataSourceI(),
        MessageI(),
        EmbeddedServiceMethodBinderI(),
        executor(*this) {
    synchronisingFunctionIdx = 0u;
    counterAndTimer[0] = 0u;
    counterAndTimer[1] = 0u;
    synchronising = false;
    executionMode = 0u;
    device = "/dev/null";
    readoffset = 0u;
    writeoffset = 0u;
    usedma = 0;
    dmabufferaddr = 0u;
    waitdma = true;
    downsamplefactor = 1u;
    startcycle = 0u;
    master = false;
    rfmdevice[0] = '\0';
    rfmhandle = static_cast<RFM2GHANDLE>(NULL);
    rfmhandlevalid = false;
    dmathreshold = 32u;
    dmamapped = false;
    pDmaBuffer = static_cast<volatile void*>(NULL);
    //pDmaInputBuffer=static_cast<volatile void *>(NULL);
    //pDmaOutputBuffer=static_cast<volatile void *>(NULL);
    pInputBuffer = static_cast<void*>(NULL);
    pOutputBuffer = static_cast<void*>(NULL);
    pInputBufferInternal = static_cast<void*>(NULL);
    pOutputBufferInternal = static_cast<void*>(NULL);
    dmabuffersize = 0u;
    inputsize = 0u;
    outputsize = 0u;

    currentcycle = 0u;
    oktorun = true;
    nOfHosts = 0u;

    hostsProtocolInfo = static_cast<HostCounterProcInfo*>(NULL);
    hostsToReadInfo = static_cast<HostReadMappingInfo*>(NULL);
    initialHostToRead = -1;
    finalHostToRead = 0;
    inputsizeRemapped = 0u;
    counterRead = static_cast<int32*>(NULL);
    diagnosticData = static_cast<float32*>(NULL);
    diagnosticRatio = static_cast<float32*>(NULL);

    timeOutTicks = 0u;
    nodeIdNumber = 0u;

    localCounter = 0u;
    counter = 0;
    counterEmbedded = 0;

    localcurrentcycle = 0u;
    period = 0;

    realTimeOffset = 0;
    realTime = 0.0;

    if (!synchSem.Create()) {
        REPORT_ERROR(ErrorManagement::FatalError, "Could not create EventSem.");
    }

    filter = ReferenceT < RegisteredMethodsMessageFilter > (GlobalObjectsDatabase::Instance()->GetStandardHeap());
    filter->SetDestination(this);
    MessageI::InstallMessageFilter(filter);

    cycles = 0;
    termmsgsent = false;
    initruntime = -10000000;
    masterstepmaxretries = MASTERSTEP_MAX_RETRIES;
}

/*lint -e{1551} the destructor must guarantee that the Timer SingleThreadService is stopped.*/
RFM2g::~RFM2g() {
    if (!synchSem.Post()) {
        REPORT_ERROR(ErrorManagement::FatalError, "Could not post EventSem.");
    }
    if (!executor.Stop()) {
        if (!executor.Stop()) {
            REPORT_ERROR(ErrorManagement::FatalError, "Could not stop SingleThreadService.");
        }
    }
    if (rfmhandlevalid) {
        if (dmamapped) {
            if (RFM2gUnMapUserMemoryBytes(rfmhandle, (volatile void**) pDmaBuffer, dmabuffersize) != RFM2G_SUCCESS) {
                REPORT_ERROR(ErrorManagement::Information, "Could not unmap DMA of RFM2g device %s", rfmdevice);
            }
            else {
                REPORT_ERROR(ErrorManagement::Information, "RFM2g device %s DMA unmap successfully", rfmdevice);
            }
        }

        // Closing device
        if (RFM2gClose(&(rfmhandle)) != RFM2G_SUCCESS) {
            REPORT_ERROR(ErrorManagement::FatalError, "Could not close RFM2g device %s", rfmdevice);
        }
        else {
            REPORT_ERROR(ErrorManagement::Information, "RFM2g device %s closed successfully", rfmdevice);
        }

    }

    if (hostsProtocolInfo != NULL) {
        delete[] hostsProtocolInfo;
    }

    if (hostsToReadInfo != NULL) {
        delete[] hostsToReadInfo;
    }

    if (counterRead != NULL) {
        delete[] counterRead;
    }

    if (diagnosticData != NULL) {
        delete[] diagnosticData;
    }

    if (diagnosticRatio != NULL) {
        delete[] diagnosticRatio;
    }

}

bool RFM2g::AllocateMemory() {

    REPORT_ERROR(ErrorManagement::Information, "Allocate memory is starting");

    bool ok = true;

    if (usedma) {
        if (dmamapped) {
            pInputBufferInternal = (void*) pDmaBuffer;
            pOutputBufferInternal = (void*) ((uint8*) pDmaBuffer + inputsize + 256 * sizeof(int32)); // Here we assume that inputsize is in bytes (checked in SetConfiguredDatabase)

            pInputBuffer = (void*) (new uint8[inputsize]);
            pOutputBuffer = (void*) (new uint8[outputsize]);

        }
        else {
            REPORT_ERROR(ErrorManagement::FatalError, "AllocateMemory called on not mapped DMA buffer");
            ok = false;
        }

    }
    else {
        pInputBufferInternal = (void*) malloc(inputsize + 256 * sizeof(int32));

        pInputBuffer = (void*) (new uint8[inputsize]);

        if (pInputBufferInternal == NULL) {
            REPORT_ERROR(ErrorManagement::FatalError, "Failed to allocate input buffer");
            ok = false;
        }
        pOutputBufferInternal = (void*) malloc(outputsize + 256 * sizeof(int32));

        pOutputBuffer = (void*) (new uint8[outputsize]);

        if (pOutputBufferInternal == NULL) {
            REPORT_ERROR(ErrorManagement::FatalError, "Failed to allocate output buffer");
            ok = false;
        }
        if (ok)
            REPORT_ERROR(ErrorManagement::Information, "RFM input/output buffer allocated successfully");
    }

    return ok;
}

bool RFM2g::Initialise(StructuredDataI &data) {
    bool ok = DataSourceI::Initialise(data);

    StreamString executionModeStr;
    if (!data.Read("ExecutionMode", executionModeStr)) {
        executionModeStr = "IndependentThread";
        REPORT_ERROR(ErrorManagement::Warning, "ExecutionMode not specified using: %s", executionModeStr.Buffer());
    }
    if (executionModeStr == "IndependentThread") {
        executionMode = RFM2G_EXEC_MODE_SPAWNED;
    }
    else if (executionModeStr == "RealTimeThread") {
        executionMode = RFM2G_EXEC_MODE_RTTHREAD;
    }
    else {
        ok = false;
        REPORT_ERROR(ErrorManagement::InitialisationError, "The Execution mode must be \"IndependentThread\" or \"RealTimeThread\"");
    }

    if (executionMode == RFM2G_EXEC_MODE_SPAWNED) {
        if (ok) {
            uint32 cpuMaskIn;
            if (!data.Read("CPUMask", cpuMaskIn)) {
                cpuMaskIn = 0xFFu;
                REPORT_ERROR(ErrorManagement::Warning, "CPUMask not specified using: %d", cpuMaskIn);
            }
            cpuMask = cpuMaskIn;
        }
        if (ok) {
            if (!data.Read("StackSize", stackSize)) {
                stackSize = THREADS_DEFAULT_STACKSIZE;
                REPORT_ERROR(ErrorManagement::Warning, "StackSize not specified using: %d", stackSize);
            }
        }
        if (ok) {
            ok = (stackSize > 0u);

            if (!ok) {
                REPORT_ERROR(ErrorManagement::ParametersError, "StackSize shall be > 0u");
            }
        }
        if (ok) {
            executor.SetCPUMask(cpuMask);
            executor.SetStackSize(stackSize);
            executor.SetPriorityClass(Threads::RealTimePriorityClass);
            executor.SetPriorityLevel(-1);
            Threads::PriorityClassType tmp = executor.GetPriorityClass();
            REPORT_ERROR(ErrorManagement::Information, "executor prio class id %d", tmp);
        }
    }

    if (ok) {
        device = "";
        if (!data.Read("Device", device)) {
            REPORT_ERROR(ErrorManagement::ParametersError, "Device not set");
            ok = false;
        }
    }
    if (ok) {
        if (!data.Read("ReadOffset", readoffset)) {
            REPORT_ERROR(ErrorManagement::ParametersError, "ReadOffset not set");
            ok = false;
        }
        if (readoffset < RFM_SYSTEM_BUFFER) {
            uint32 tmp = RFM_SYSTEM_BUFFER;
            REPORT_ERROR(ErrorManagement::ParametersError, "ReadOffset <%d increase please", tmp);
            ok = false;
        }

    }
    if (ok) {
        if (!data.Read("WriteOffset", writeoffset)) {
            REPORT_ERROR(ErrorManagement::ParametersError, "WriteOffset not set");
            ok = false;
        }
        if (writeoffset < RFM_SYSTEM_BUFFER) {
            //REPORT_ERROR(ErrorManagement::ParametersError, "WriteOffset wrong, first");
            uint32 tmp = RFM_SYSTEM_BUFFER;
            REPORT_ERROR(ErrorManagement::ParametersError, "WriteOffset <%d increase please ", tmp);

            //  REPORT_ERROR(ErrorManagement::ParametersError, "WriteOffset wrong, first %d bytes are reserved", RFM_SYSTEM_BUFFER);
            ok = false;
        }
    }
    uint8 tmp;
    if (ok) {
        if (data.Read("UseDMA", tmp)) {
            usedma = (tmp == 1u);
        }
        if (!usedma) {
            REPORT_ERROR(ErrorManagement::Information, "DMA not selected, using programmed I/O");
        }
        else {
            REPORT_ERROR(ErrorManagement::Information, "DMA selected");
            // TODO: check hex conversion and type conversion
            if (!data.Read("DMABufferAddress", dmabufferstr)) {
                REPORT_ERROR(ErrorManagement::ParametersError, "DMABufferAddress must be given when in DMA mode");
                ok = false;
            }
            else {
                char *end;
                dmabufferaddr = strtoull(dmabufferstr.Buffer(), &end, 16);
            }
            if (!data.Read("WaitDMA", tmp)) {
                REPORT_ERROR(ErrorManagement::ParametersError, "WaitDMA must be given when in DMA mode");
                ok = false;
            }
            if (!data.Read("DMABufferSize", dmabuffersize)) {
                REPORT_ERROR(ErrorManagement::ParametersError, "DMABufferSize must be given when in DMA mode");
                ok = false;
            }
            if (!data.Read("DMAThreshold", dmathreshold)) {
                REPORT_ERROR(ErrorManagement::ParametersError, "DMAThreshold must be given when in DMA mode");
                ok = false;
            }
            if (ok) {
                waitdma = (tmp == 1u);
                if (!waitdma) {
                    REPORT_ERROR(ErrorManagement::Warning, "WaitDMA is false, carefully check RFM data transfers");
                }
                else {
                    REPORT_ERROR(ErrorManagement::Warning, "WaitDMA is true, DMA transfers will be waited for");
                }
            }
        }
    }
    if (ok) {
        /* Synchronizing is set by the Frequency property of signals,
         * see GetBrokerName
         if(!data.Read("Synchronizing", tmp))
         {
         synchronising=false;
         }
         else
         {
         synchronising=(tmp==1u);

         if(synchronising)
         {
         */

        if (!data.Read("DownSampleFactor", downsamplefactor)) {
            REPORT_ERROR(ErrorManagement::ParametersError, "DownSampleFactor must be given when in Synchronizing mode");
            REPORT_ERROR(ErrorManagement::Warning, "DownSampleFactor not set");
            ok = false;
        }
        else {
            if (downsamplefactor < 1) {
                REPORT_ERROR(ErrorManagement::ParametersError, "DownSampleFactor must be greater than 0");
                ok = false;
            }
        }

        REPORT_ERROR(ErrorManagement::Information, "downsamplefactor = %d", downsamplefactor);

        if (!data.Read("StartTime", startcycle)) {
            //REPORT_ERROR(ErrorManagement::ParametersError, "StartTime must be given when in Synchronizing mode");
            REPORT_ERROR(ErrorManagement::Warning, "StartTime not set");

            //ok=false;
        }

        //if(ok) REPORT_ERROR(ErrorManagement::Information, "RFM2g will synchronize the calling thread from t=%f s", starttime);

//            }
//        }
    }
    if (!data.Read("Master", tmp)) {
        master = false;
    }
    else {
        master = (tmp == 1u);
        if (master) {
            /*
             if(synchronising)
             {
             REPORT_ERROR(ErrorManagement::ParametersError, "RFM2g DataSource cannot be Synchronizing and Master at the same time.");
             ok=false;
             }
             */
            if (ok)
                REPORT_ERROR(ErrorManagement::Information, "RFM2g configured as RFM Master node");

            if (ok) {
                if (!data.Read("InitRunTime", initruntime)) {
                    REPORT_ERROR(ErrorManagement::ParametersError, "InitRunTime must be given in master mode");
                    ok = false;
                }
                if (!data.Read("MasterStepMaxRetries", masterstepmaxretries)) {
                    REPORT_ERROR(ErrorManagement::ParametersError, "MasterStepMaxRetries should be given in master mode. Default %d", masterstepmaxretries);
                }

            }

        }
    }

    if (ok) {
        if (!data.Read("NumberOfHosts", nOfHosts)) {
            REPORT_ERROR(ErrorManagement::ParametersError, "NumberOfHosts must be given");
            ok = false;
        }

    }

    if (ok) {
        float64 timeOut = TIMEOUT_PERIOD;
        ok = data.Read("TimeOut", timeOut);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "TimeOut not given. Default is %d microseconds", TIMEOUT_PERIOD);
        }
        else {
            REPORT_ERROR(ErrorManagement::ParametersError, "TimeOut is %d microseconds", timeOut);
        }

        if (timeOut < 0.01) {

            timeOutTicks = 0.F;
        }

        else {
            float64 frequency_ = timeOut * 1.e-6;
            frequency_ = 1. / frequency_;

            float64 timeOutT = (static_cast<float64>(HighResolutionTimer::Frequency()) / frequency_);
            timeOutTicks = static_cast<uint64>(timeOutT);
        }

    }

    if (ok) {
        if (!data.Read("NodeIdNumber", nodeIdNumber)) {
            REPORT_ERROR(ErrorManagement::ParametersError, "NodeIdNumber must be given");
            ok = false;
        }

        if (ok && (nodeIdNumber != 0) && master) {
            REPORT_ERROR(ErrorManagement::ParametersError,
                         "When master, NodeIdNumber must be 0. For the slaves, only exclusive integer numbers are allowed with values: 1 ... NumberOfHosts-1");
            ok = false;
        }

        if (ok && (nodeIdNumber == 0) && !master) {
            REPORT_ERROR(ErrorManagement::ParametersError,
                         "When master, NodeIdNumber must be 0. For the slaves, only exclusive integer numbers are allowed with values: 1 ... NumberOfHosts-1");
            ok = false;
        }

        if (ok && (nodeIdNumber > nOfHosts) && !master) {
            REPORT_ERROR(ErrorManagement::ParametersError,
                         "NodeIdNumber out of range. For the slaves, only exclusive integer numbers are allowed with values: 1 ... NumberOfHosts-1");
            ok = false;
        }

    }

    if (ok) {
        if (!master) {
            if (!data.Read("Cycles", cycles)) {
                REPORT_ERROR(ErrorManagement::ParametersError, "Cycles must be given in slave mode");
                ok = false;
            }
            else {
                if (cycles < 1) {
                    REPORT_ERROR(ErrorManagement::ParametersError, "Cycles must be > 1");
                    ok = false;
                }
                else {
                    REPORT_ERROR(ErrorManagement::Information, "RFM2g number of RT cycles set to %d", cycles);
                }
            }

        }
    }

    /**
     * Opening the device
     */
    if (device.Size() > 40) {
        REPORT_ERROR(ErrorManagement::InitialisationError, "Device name too long (max 40 characters)");
        ok = false;
    }
    else {
        strncpy(rfmdevice, device.Buffer(), 39);
    }

    if (ok) {
        if (RFM2gOpen(rfmdevice, &rfmhandle) != RFM2G_SUCCESS) {
            REPORT_ERROR(ErrorManagement::InitialisationError, "Error opening RFM2g device %s", rfmdevice);
            ok = false;
        }
        else {
            REPORT_ERROR(ErrorManagement::Information, "Device %s opened successfully", rfmdevice);
            rfmhandlevalid = true;
        }
    }

    /**
     * Mapping DMA buffer and setting DMA threshold
     */
    if (ok && rfmhandlevalid && usedma) {

        dmabuffersize = dmabuffersize + 256 * sizeof(int32);  //here I reserve space for all the possible hosts counters

        if (RFM2gUserMemoryBytes(rfmhandle, (volatile void**) &pDmaBuffer, dmabufferaddr | RFM2G_DMA_MMAP_OFFSET, dmabuffersize) != RFM2G_SUCCESS) {
            REPORT_ERROR(ErrorManagement::InitialisationError, "Coudn't map the userspace DMA buffer");
            ok = false;
        }
        else {
            REPORT_ERROR(ErrorManagement::Information, "Userspace DMA buffer mapped successfully");
            REPORT_ERROR(ErrorManagement::Information, "pDmaBuffer= %d", pDmaBuffer);
            dmamapped = true;

        }

        if (dmamapped) {
            if (RFM2gSetDMAThreshold(rfmhandle, dmathreshold) != RFM2G_SUCCESS) {
                REPORT_ERROR(ErrorManagement::InitialisationError, "Coudn't set the DMA threshold to %d", dmathreshold);
                ok = false;
            }
            else {
                REPORT_ERROR(ErrorManagement::Information, "DMA threshold set to %d", dmathreshold);
            }
        }
    }

    if (ok) {

        bool ok1 = InitializeHostsToReadInfo();
        bool ok2 = InitializeCounterRead();
        bool ok3 = InitializeDiagnosticData();
        bool ok4 = InitializeDiagnosticRatio();

        ok = ok1 && ok2 && ok3 && ok4;

        if (!ok) {
            REPORT_ERROR(ErrorManagement::InitialisationError, "Failed to allocate the diagnostic protocol info arrays");
        }

    }

    if (ok) {

        RFM2G_STATUS result;
        result = RFM2gNodeID(rfmhandle, &NodeId);
        ok = (result == RFM2G_SUCCESS);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::InitialisationError, "Could not get the RFM host node ID");
        }
    }

    /*
     if (ok) {
     if (!master && (downsamplefactor > 1) && (executionMode != RFM2G_EXEC_MODE_SPAWNED)) {
     REPORT_ERROR(ErrorManagement::InitialisationError, "For the slave, when DownSampleFactor >1, only ExecutionMode = IndependentThread is allowed");
     ok = false;
     }
     }
     */

    if (ok && numberOfinstances == 0) {
        numberOfinstances++;
        fastMuxRFM.Create();
    }

    if (ok) {
        fastMux.Create();
    }

    return ok;

}

bool RFM2g::SetConfiguredDatabase(StructuredDataI &data) {
    bool ok = DataSourceI::SetConfiguredDatabase(data);
    /*if (ok) {
     ok = (GetNumberOfSignals() == 4u);
     }
     if (!ok) {
     REPORT_ERROR(ErrorManagement::ParametersError, "Exactly four signals shall be configured");
     }*/
    if (ok) {
        ok = (GetSignalType(0u).numberOfBits == 32u);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The first signal shall have 32 bits and %d were specified", uint16(GetSignalType(0u).numberOfBits));
        }
    }
    if (ok) {
        ok = (GetSignalType(0u).type == SignedInteger);
        if (!ok) {
            ok = (GetSignalType(0u).type == UnsignedInteger);
        }
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The first signal shall be SignedInteger type");
        }
    }
    if (ok) {
        ok = (GetSignalType(1u).numberOfBits == 32u);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The second signal shall have 32 bits and %d were specified",
                         uint16(GetSignalType(1u).numberOfBits));
        }
    }
    if (ok) {
        ok = (GetSignalType(1u).type == SignedInteger);
        if (!ok) {
            ok = (GetSignalType(1u).type == UnsignedInteger);
        }
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The second signal shall be SignedInteger type");
        }
    }
    if (ok) {
        ok = (GetSignalType(2u).numberOfBits == 8u);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The third signal shall have 8 bits and %d were specified", uint16(GetSignalType(1u).numberOfBits));
        }
    }
    if (ok) {
        ok = (GetSignalType(2u).type == UnsignedInteger);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The third signal shall UnsignedInteger type");
        }
    }
    if (ok) {
        ok = GetSignalNumberOfElements(2u, inputsize);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::InitialisationError, "Cannot get the number of elements of the third signal (input buffer)");
        }
    }
    if (ok) {
        ok = (GetSignalType(3u).numberOfBits == 8u);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The fourth signal shall have 8 bits and %d were specified", uint16(GetSignalType(1u).numberOfBits));
        }
    }
    if (ok) {
        ok = (GetSignalType(3u).type == UnsignedInteger);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The fourth signal shall UnsignedInteger type");
        }
    }
    if (ok) {
        ok = GetSignalNumberOfElements(3u, outputsize);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::InitialisationError, "Cannot get the number of elements of the fourth signal (output buffer)");
        }
    }

    if (ok) {
        ok = (GetSignalType(4u).numberOfBits == 64u);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The fifth signal shall have 64 bits and %d were specified", uint16(GetSignalType(4u).numberOfBits));
        }
    }
    if (ok) {
        ok = (GetSignalType(4u).type == Float);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The fifth signal shall be Float64 type");
        }
    }
    if (ok) {
        ok = (GetSignalType(5u).numberOfBits == 8u);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The sixth signal shall have 8 bits and %d were specified", uint16(GetSignalType(5u).numberOfBits));
        }
    }
    if (ok) {
        ok = (GetSignalType(5u).type == UnsignedInteger);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The sixth signal shall be UnsignedInteger type");
        }
    }

    if (ok) {
        uint32 numberOfCounters;
        ok = GetSignalNumberOfElements(5u, numberOfCounters);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::InitialisationError, "Cannot get the number of elements of the sixth signal (Counters)");

            if (ok) {
                ok = (numberOfCounters == (nOfHosts));
                if (!ok) {
                    REPORT_ERROR(ErrorManagement::ParametersError, "Number of elements of the sixth signal not corrected. You must have %d signals", nOfHosts);
                }

            }

        }
    }

    if (ok) {
        ok = (GetSignalType(6u).numberOfBits == 8u);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The seventh signal shall have 8 bits and %d were specified",
                         uint16(GetSignalType(6u).numberOfBits));
        }
    }
    if (ok) {
        ok = (GetSignalType(6u).type == UnsignedInteger);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::ParametersError, "The seventh signal shall be UnsignedInteger type");
        }
    }

    if (ok) {
        uint32 numberOfDiagnostics;
        ok = GetSignalNumberOfElements(6u, numberOfDiagnostics);
        if (!ok) {
            REPORT_ERROR(ErrorManagement::InitialisationError, "Cannot get the number of elements of the seventh signal (Diagnostics)");

            if (ok) {
                ok = (numberOfDiagnostics == (nOfHosts));
                if (!ok) {
                    REPORT_ERROR(ErrorManagement::ParametersError, "Number of elements of the seventh signal not corrected. You must have %d signals",
                                 nOfHosts);
                }

            }

        }
    }

    /*
     * If DMA is enabled, the size of inputbuffer+outputbuffer must be less that the allocated DMA buffer
     */
    if (usedma) {
        if (inputsize > (dmabuffersize / 2)) {
            REPORT_ERROR(ErrorManagement::ParametersError, "In DMA mode inputsize buffer size (%d) must not be greater that DMA buffer size / 2 (%d)",
                         inputsize, dmabuffersize / 2);
            ok = false;
        }
        if (outputsize > (dmabuffersize / 2)) {
            REPORT_ERROR(ErrorManagement::ParametersError, "In DMA mode output buffer size (%d) must not be greater that DMA buffer size / 2 (%d)", outputsize,
                         dmabuffersize / 2);
            ok = false;
        }

    }

    if (!master && !synchronising && executionMode != RFM2G_EXEC_MODE_SPAWNED) {
        REPORT_ERROR(ErrorManagement::ParametersError, "RFM2g in not master mode and not synchronizing mode must be placed on a separated thread");
        ok = false;
    }

    if (!master && !synchronising && executionMode == RFM2G_EXEC_MODE_SPAWNED) {
        REPORT_ERROR(ErrorManagement::ParametersError, "RFM2g in not master mode and not synchronizing spawned isn't implemented yet");
        ok = false;
    }

    if (master && executionMode == RFM2G_EXEC_MODE_SPAWNED) {
        REPORT_ERROR(ErrorManagement::ParametersError, "RFM2g in master mode must not be placed on a separated thread");
        ok = false;
    }

    if (ok) {


        (void) fastMuxRFM.FastLock(TTInfiniteWait, 0.);  //multithread
        ok = SetDiagnosticOwnData();
        fastMuxRFM.FastUnLock();
    }

    REPORT_ERROR(ErrorManagement::Information, "Input  buffer length %d, starting at %d", inputsize, readoffset);
    REPORT_ERROR(ErrorManagement::Information, "Output buffer length %d, starting at %d", outputsize, writeoffset);

    return ok;
}

uint32 RFM2g::GetNumberOfMemoryBuffers() {
    // TODO: check this

    return 1u;
}

/*lint -e{715}  [MISRA C++ Rule 0-1-11], [MISRA C++ Rule 0-1-12]. Justification: The memory buffer is independent of the bufferIdx.*/
bool RFM2g::GetSignalMemoryBuffer(const uint32 signalIdx,
                                  const uint32 bufferIdx,
                                  void *&signalAddress) {
    bool ok = true;
    if (signalIdx == 0u) {
        signalAddress = &counterAndTimer[0];
    }
    else if (signalIdx == 1u) {
        signalAddress = &counterAndTimer[1];
    }
    else if (signalIdx == 2u) {
        signalAddress = pInputBuffer;
        //signalAddress = pInputBufferCopy;
    }
    else if (signalIdx == 3u) {
        signalAddress = pOutputBuffer;
    }
    else if (signalIdx == 4u) {
        signalAddress = &realTime;
    }
    else if (signalIdx == 5u) {
        void *counterReadout = static_cast<void*>(counterRead);
        signalAddress = counterReadout;
    }
    else if (signalIdx == 6u) {
        void *diagnosticDataout = static_cast<void*>(diagnosticData);
        signalAddress = diagnosticDataout;
    }

    return ok;
}

const char8* RFM2g::GetBrokerName(StructuredDataI &data,
                                  const SignalDirection direction) {
    const char8 *brokerName = NULL_PTR(const char8 *);
    float32 frequency = 0.F;

    /* If there is a signals with the Frequency attribute set,
     the DataSource becomes synchronizing
     This has to be retained since the following broker
     add functions (GetInputBrokers, GetOutputBrokers)
     will put the Frequency defined broker in first position */

    if (!data.Read("Frequency", frequency)) {
        frequency = -1.F;
    }
    else
        period = (1.e6 / frequency);

    if (frequency > 0.F) {
        synchronising = true;
    }

    // See the broker configuration table in RFM2g.h
    if (master) {
        if (direction == InputSignals) {
            brokerName = "MemoryMapInputBroker";
        }
        else {
            brokerName = "MemoryMapSynchronisedOutputBroker";
        }
    }
    else {

        if (synchronising) {
            if (direction == InputSignals) {
                brokerName = "MemoryMapSynchronisedInputBroker";
            }
            else {
                brokerName = "MemoryMapOutputBroker";
            }
        }
        else {
            if (direction == InputSignals) {
                brokerName = "MemoryMapInputBroker";
            }
            else {
                brokerName = "MemoryMapAsyncOutputBroker";
            }
        }
    }

    return brokerName;
}

bool RFM2g::Synchronise() {
    ErrorManagement::ErrorType err = ErrorManagement::NoError;

    if (!master) {
        (void) fastMux.FastLock(TTInfiniteWait, 0.);
        counterAndTimer[0]++;

        fastMux.FastUnLock();

    }

    /*
     ///////////////// //sleep to induce jitter
     if(counterAndTimer[0]==5000){
     Sleep::NoMore(0.003);
     }
     ////////////////////
     */

    if (executionMode == RFM2G_EXEC_MODE_SPAWNED) {
        //	bool notRunning = ( EmbeddedThreadI::RunningState);
//	if(!notRunning)
        if (executor.GetStatus() == EmbeddedThreadI::RunningState)
            err = synchSem.ResetWait(TTInfiniteWait);

        else
            err = synchSem.ResetWait(1000);

#ifdef _DEBUG

                REPORT_ERROR(ErrorManagement::Information, "cycle Triggered");


#endif

    }
    else {
        if (master) {
            ExecutionInfo info;
            info.SetStage(ExecutionInfo::MainStage);
            err = Execute(info);
        }
        else {
            do {
                ExecutionInfo info;
                info.SetStage(ExecutionInfo::MainStage);
                err = Execute(info);

            }
            while (err != ErrorManagement::NoError);

        }
    }

    if (!master) {
        if (counterAndTimer[0] != counterEmbedded) {
            counterAndTimer[0] = counterEmbedded;
        }
    }

    /*
     Sleep::Sec(1);
     counterAndTimer[0]++;
     */

    return err.ErrorsCleared();
}

/*lint -e{715}  [MISRA C++ Rule 0-1-11], [MISRA C++ Rule 0-1-12]. Justification: the counter and the timer are always reset irrespectively of the states being changed.*/
bool RFM2g::PrepareNextState(const char8 *const currentStateName,
                             const char8 *const nextStateName) {
    bool ok = true;

    REPORT_ERROR(ErrorManagement::Warning, "Prepare state %s ", nextStateName);
    if (!strcmp(nextStateName, "Idle")) {
        if (executionMode == RFM2G_EXEC_MODE_SPAWNED) {
            if (executor.GetStatus() == EmbeddedThreadI::RunningState) {
                //Sleep::Sec(0.5);
                ok = executor.Stop();
                REPORT_ERROR(ErrorManagement::Warning, "Independent Thread Stop %d", ok);
                synchSem.Post();

            }
        }
    }

    if (!strcmp(nextStateName, "Run")) {
        if (executionMode == RFM2G_EXEC_MODE_SPAWNED) {
            if (executor.GetStatus() == EmbeddedThreadI::OffState) {

                REPORT_ERROR(ErrorManagement::Warning, "Independent Thread Starting ");
                ok = executor.Start();
            }
        }

        counterAndTimer[0] = 0u;
        counterAndTimer[1] = 0u;
        realTimeOffset = 0;
        realTime = 0.0;

        memset(pInputBufferInternal, 0, inputsizeRemapped);
        memset(pOutputBufferInternal, 0, outputsize + sizeof(int32));
        if (usedma)
            RFM2gWriteDMAwaitfinish(rfmhandle, writeoffset + nodeIdNumber * sizeof(int32), pOutputBufferInternal, outputsize + sizeof(int32));
        else
            RFM2gWriteDMA(rfmhandle, writeoffset + nodeIdNumber * sizeof(int32), pOutputBufferInternal, outputsize + sizeof(int32));

        localCounter = 0u;
        counterEmbedded = 0;
        termmsgsent = false;
        counter = 0u;

        if (!rfm_master_step(0, initruntime)) {
            REPORT_ERROR(ErrorManagement::Warning, "Could not zero the RFM cycle counter");
        }
        else {
            REPORT_ERROR(ErrorManagement::Warning, "RFM cycle counter zeroed, RFM time set to %d", initruntime);
        }
    }

    return ok;
}

/*lint -e{715}  [MISRA C++ Rule 0-1-11], [MISRA C++ Rule 0-1-12]. Justification: the method sleeps for the given period irrespectively of the input info.*/
ErrorManagement::ErrorType RFM2g::Execute(ExecutionInfo &info) {

    ErrorManagement::ErrorType err = ErrorManagement::NoError;

    //while(1) {};

    if (!oktorun) {
        Sleep::NoMore(1);
    }

    if (master) {
        /* when called, update the RFM counter with the value
         provided from the rt application
         (usually a cycle counter coming from the main timing system)
         */

#ifdef _DEBUG
                REPORT_ERROR(ErrorManagement::Information, "The master (nodeID= %d) is preparing itself for the writing", NodeId);
#endif

        Write(info);

#ifdef _DEBUG
                REPORT_ERROR(ErrorManagement::Information, "The master has written");
#endif

        if (counterAndTimer[0] == 1) {
            realTimeOffset = HighResolutionTimer::Counter();
        }

        realTime = HighResolutionTimer::Period() * (HighResolutionTimer::Counter() - realTimeOffset);

        uint16 stepretry = 0;
        while (!rfm_master_step(counterAndTimer[0], counterAndTimer[1]) && stepretry < masterstepmaxretries) {
            stepretry++;
        }

#ifdef _DEBUG
                REPORT_ERROR(ErrorManagement::Information, "Master counter= %d", counterAndTimer[0]);
#endif

        // waiting
        uint64 startTicksTimeOut = HighResolutionTimer::Counter();

        do {

            ;

        }
        while (((HighResolutionTimer::Counter() - startTicksTimeOut) < timeOutTicks));

        //start the reading operations
        Read(info);

        //In case the master is not able to write its counter, a negative value will appear on the diagnostic channel
        //Such negative value will be the difference counterAndTimer[0]-lastMasterIteration. It the lastMasterIteration cannot be get
        //i.e., the get_iteration fails, then a default negative value (-12345) will be provided
        if (stepretry >= masterstepmaxretries) {
            diagnosticData[0] = -12345;  //default value in case it is not possible to read the iteration

            int32 lastMasterIteration;

            if (get_iteration(rfmhandle, &lastMasterIteration)) {
                diagnosticData[0] = counterAndTimer[0] - lastMasterIteration;
            }

        }

#ifdef _DEBUG
                REPORT_ERROR(ErrorManagement::Information, "The master has red");
#endif
    }

    else {  //here starts the slave execute

        if (synchronising) {

            if (executionMode != RFM2G_EXEC_MODE_SPAWNED) {
                err = ErrorManagement::NotCompleted;
            }

            bool notRunning = false;

            //check if the counter is outside the maximum number of cycles
            (void) fastMux.FastLock(TTInfiniteWait, 0.);

            if (counterAndTimer[0] + 1 > cycles) {
                if (!termmsgsent) {
                    ReferenceT < Message > termMessage = Get(0);
                    if (termMessage.IsValid()) {
                        REPORT_ERROR(ErrorManagement::Information, "Sending termination message");
                        SendMessage(termMessage, this);
                    }
                    termmsgsent = true;
                }
                Sleep::Sec(1);
                REPORT_ERROR(ErrorManagement::Information, "Max Number of cycles Reached");
            }
            fastMux.FastUnLock();

//in case of spawned thread, check if a termination message has been received
            if (executionMode == RFM2G_EXEC_MODE_SPAWNED) {
                EmbeddedThreadI::States status;
                status = executor.GetStatus();
                notRunning = (status != EmbeddedThreadI::RunningState);
            }

#ifdef _DEBUG

            // This is a sleep in case the counter is still the old one (maybe better a normal sleep)
                 Sleep::Sec(0.001);  //If not inserted, no standard output visible at runtime. Change here for appropriate sleep period
#endif

// the slave gets the current iteration from the RFM

            uint64 elapsedTimeTicks = 0u;
            uint64 startTicksTimeOut = HighResolutionTimer::Counter();
            if (counter == 0)
                realTimeOffset = HighResolutionTimer::Counter();

            while (!get_iteration(rfmhandle, &localcurrentcycle) && elapsedTimeTicks < timeOutTicks && !notRunning) {

                elapsedTimeTicks = HighResolutionTimer::Counter() - startTicksTimeOut;
            }

#ifdef _DEBUG
            if (elapsedTimeTicks >= timeOutTicks && !notRunning) {
                REPORT_ERROR(ErrorManagement::Information, "Slave %d cannot get the current cycle", nodeIdNumber);
            }

#endif

            /*
             #ifdef _DEBUG

             REPORT_ERROR(ErrorManagement::Information, "Counter received from the master= %d",localcurrentcycle);
             REPORT_ERROR(ErrorManagement::Information, "Slave counter= %d",counterAndTimer[0]);
             #endif
             */

            if (localcurrentcycle > counter && !notRunning) {

                localCounter += localcurrentcycle - counter;
                counter = localcurrentcycle;

#ifdef _DEBUG

                REPORT_ERROR(ErrorManagement::Information, "Slave counter= %d",counterAndTimer[0]);
                REPORT_ERROR(ErrorManagement::Information, "Slave localCounter= %d",localCounter);
#endif

                //this is the case when the slave must write/read (according to the downsample factor)
                if (localCounter >= downsamplefactor) {
                    realTime = (HighResolutionTimer::Counter() - realTimeOffset) * HighResolutionTimer::Period();

                    (void) fastMuxRFM.FastLock(TTInfiniteWait, 0.);

                    Write(info);

                   fastMuxRFM.FastUnLock();

#ifdef _DEBUG

                REPORT_ERROR(ErrorManagement::Information, "the slave has written");


#endif

                    counter = (localcurrentcycle / downsamplefactor) * downsamplefactor;
                    localCounter = 0u;

                    counterEmbedded = counter / downsamplefactor;

                    startTicksTimeOut = HighResolutionTimer::Counter();

                    do {
                        ;
                    }
                    while (HighResolutionTimer::Counter() - startTicksTimeOut < timeOutTicks && !notRunning);

                    RFM2gPeek32(rfmhandle, RFM_TIME_OFFSET, (RFM2G_UINT32*) &(counterAndTimer[1]));

                   // (void) fastMuxRFM.FastLock(TTInfiniteWait, 0.); //commentare
                    Read(info);
                    //fastMuxRFM.FastUnLock(); //commentare
#ifdef _DEBUG

                REPORT_ERROR(ErrorManagement::Information, "the slave has red");


#endif

                    bool check_counter = (counter < startcycle);

#ifdef _DEBUG
 if (check_counter) {
 
                REPORT_ERROR(ErrorManagement::Information, "start time not reached yet");
}


#endif

                    if (!check_counter) {

                        if (executionMode == RFM2G_EXEC_MODE_SPAWNED) {
                            err = !(synchSem.Post());

                        }

                        err = ErrorManagement::NoError;

                    }

                }

            }

            else {

#ifdef _DEBUG

                Sleep::Sec(0.005);


#endif

            }

        }

        else {
            // TODO: implement a not synchronizing (calling thread side) (node07 and node08 example)
            // spawned data exchange with the RFM
        }
    }

    return err;
}

ErrorManagement::ErrorType RFM2g::Read(ExecutionInfo &info) {

    if (!usedma) {
        RFM2gRead(rfmhandle, hostsToReadInfo[initialHostToRead].hostToReadOffset, pInputBufferInternal, inputsizeRemapped);

    }
    else {
        if (waitdma) {

            RFM2gReadDMAwaitfinish(rfmhandle, hostsToReadInfo[initialHostToRead].hostToReadOffset, pInputBufferInternal, inputsizeRemapped);

        }
        else {

            RFM2gReadDMA(rfmhandle, hostsToReadInfo[initialHostToRead].hostToReadOffset, pInputBufferInternal, inputsizeRemapped);

        }
    }
// TODO: how to handle an error here (RT phase) ?

    readRemapping();
    EvaluateDiagnostcData();

    return ErrorManagement::NoError;

}

ErrorManagement::ErrorType RFM2g::Write(ExecutionInfo &info) {

    MemoryOperationsHelper::Copy(pOutputBufferInternal, pOutputBuffer, outputsize);

    int32 *ptCounter = (int32*) ((uint8*) pOutputBufferInternal + outputsize);

    *ptCounter = counterAndTimer[0];

    if (!usedma) {
        RFM2gWrite(rfmhandle, writeoffset + nodeIdNumber * sizeof(int32), pOutputBufferInternal, outputsize + sizeof(int32));
    }
    else {
        if (waitdma) {
            RFM2gWriteDMAwaitfinish(rfmhandle, writeoffset + nodeIdNumber * sizeof(int32), pOutputBufferInternal, outputsize + sizeof(int32));
        }
        else {
            RFM2gWriteDMA(rfmhandle, writeoffset + nodeIdNumber * sizeof(int32), pOutputBufferInternal, outputsize + sizeof(int32));
        }
    }

// TODO: how to handle an error here (RT phase) ?

    return ErrorManagement::NoError;
}

const ProcessorType& RFM2g::GetCPUMask() const {
    return cpuMask;
}

uint32 RFM2g::GetStackSize() const {
    return stackSize;
}

inline bool RFM2g::get_iteration(RFM2GHANDLE handle,
                                 int32 *current_iteration) {
    unsigned char trig1 = 0;
    unsigned char trig2 = 0;

// first check ready to read  is OK
    RFM2gPeek8(handle, RFM_TRIG_OFFSET, &trig1);
    if (trig1 && 0x1) {
// OK to read
        RFM2gPeek32(handle, RFM_ITERATION_OFFSET, (RFM2G_UINT32*) current_iteration);

//RFM2gPeek32(handle, RFM_TIME_OFFSET,(int32 *)currenttime);

// verify that the flag did not change as we were reading
        RFM2gPeek8(handle, RFM_TRIG_OFFSET, &trig2);

        if (trig2 && 0x0) {
            return false;  //flag went low during read. return not ok.
        }
        else {
            return true;  // got it OK
        }

    }
    else {
        return false;  // flag indicates node is currently writing to RFM. wait.
    }

}

bool RFM2g::rfm_master_step(int32 rfm_iter,
                            int32 time) {
    unsigned char trig = 0;
    int result;

//
// use blocking flags to prevent collision in read/write with iteration number
// TODO: retry if trig booked ?
    result = RFM2gPoke8(rfmhandle, RFM_TRIG_OFFSET, trig);
    if (result != RFM2G_SUCCESS) {
        return false;
    }

// increment the counter.
    result = RFM2gWrite(rfmhandle, RFM_ITERATION_OFFSET, &rfm_iter, sizeof(int));
    if (result != RFM2G_SUCCESS) {
        return false;
    }

// increment the time.
    result = RFM2gWrite(rfmhandle, RFM_TIME_OFFSET, &time, sizeof(int));
    if (result != RFM2G_SUCCESS) {
        return false;
    }

    trig = 1;
    result = RFM2gPoke8(rfmhandle, RFM_TRIG_OFFSET, trig);
    if (result != RFM2G_SUCCESS) {
        return false;
    }

// so we are clear to read the iteration flag when the trig flag is at 1
// this should be tested BEFORE AND AFTER the iteration read to check it did not
// change during the read.

    return true;
}

ErrorManagement::ErrorType RFM2g::StopLLC() {
    oktorun = false;
    return ErrorManagement::NoError;
}

bool RFM2g::SetDiagnosticOwnData() {

    //here the host put its data on the RFM so as to implement the counter diagnostic protocol

    bool ok, ok1, ok2, ok3;
    ok = false;

    RFM2G_STATUS result;

    result = RFM2gPoke32(rfmhandle, RFM_START_PROTOCOL + nodeIdNumber * SIZE_OF_HOST_PROTOCOL_DATA, writeoffset);
    ok1 = (result == RFM2G_SUCCESS);

    result = RFM2gPoke32(rfmhandle, RFM_START_PROTOCOL + nodeIdNumber * SIZE_OF_HOST_PROTOCOL_DATA + sizeof(uint32), outputsize);
    ok2 = (result == RFM2G_SUCCESS);

    result = RFM2gPoke32(rfmhandle, RFM_START_PROTOCOL + nodeIdNumber * SIZE_OF_HOST_PROTOCOL_DATA + 2 * sizeof(uint32), downsamplefactor);
    ok3 = (result == RFM2G_SUCCESS);

    ok = ok1 && ok2 && ok3;

    if (!ok) {
        REPORT_ERROR(ErrorManagement::InitialisationError, "Could not write on the RFM the host data: writeoffset, outputsize, downsamplefactor");
    }

    return ok;

}

ErrorManagement::ErrorType RFM2g::SetInitialInfo() {

    ErrorManagement::ErrorType err;

    REPORT_ERROR(ErrorManagement::Information, "The diagnostic counter protocol setting is starting");

    //here starts collecting the hosts information

    int result;

    if (!usedma) {
        result = RFM2gRead(rfmhandle, RFM_START_PROTOCOL, pInputBuffer, nOfHosts * SIZE_OF_HOST_PROTOCOL_DATA);
    }
    else {
        if (waitdma) {
            result = RFM2gReadDMAwaitfinish(rfmhandle, RFM_START_PROTOCOL, pInputBuffer, nOfHosts * SIZE_OF_HOST_PROTOCOL_DATA);
        }
        else {
            result = RFM2gReadDMA(rfmhandle, RFM_START_PROTOCOL, pInputBuffer, nOfHosts * SIZE_OF_HOST_PROTOCOL_DATA);

        }
    }

    bool ok = (result == RFM2G_SUCCESS);

    if (ok) {
        // copy the info from the DMA.

        hostsProtocolInfo = new HostCounterProcInfo[nOfHosts];

        ok = (hostsProtocolInfo != NULL);
    }

    if (ok) {

        uint32 i = 0u;

        HostCounterProcInfo *reintPinput = reinterpret_cast<HostCounterProcInfo*>(pInputBuffer);

        for (i = 0u; i < nOfHosts; i++) {

            hostsProtocolInfo[i] = *(reintPinput + i);

            REPORT_ERROR(ErrorManagement::Information, "*** Host number %d: *** ", i);
            REPORT_ERROR(ErrorManagement::Information, "writeoffset %d: ", hostsProtocolInfo[i].hostWriteoffset);
            REPORT_ERROR(ErrorManagement::Information, "outputsize %d:  ", hostsProtocolInfo[i].hostOutputsize);
            REPORT_ERROR(ErrorManagement::Information, "downsamplefactor %d: ", hostsProtocolInfo[i].hostDownsamplefactor);
            REPORT_ERROR(ErrorManagement::Information, "\n");

        }
    }

    //OPTIONAL: si potrebbe fare il check della topologia, per vedere la coerenza (ciascuno scrive in maniera esclusiva nel proprio pezzo e non si scrivono addosso, o indentificare un buco)

    // note: is it also possible to direcly copy the memory with the following line
    // MemoryOperationsHelper::Copy(OutBufferPtr,DACBufferPtr,numberOfSamples*sizeof(uint16));

    if (ok) {
        err.fatalError = false;
    }
    else {
        err.fatalError = true;
        REPORT_ERROR(ErrorManagement::FatalError, "Failed to set the initial info");
    }

    return err;

}

bool RFM2g::CheckMemoryContiguity() {

    bool ok = true;

    uint32 i = 0;

    for (i = 0; i < nOfHosts - 1 && ok; i++) {

        ok = (hostsProtocolInfo[i].hostWriteoffset + hostsProtocolInfo[i].hostOutputsize == hostsProtocolInfo[i + 1].hostWriteoffset);
    }

    if (!ok) {
        REPORT_ERROR(ErrorManagement::FatalError, "The hosts do not write in a contiguous way. First host not writing correctly has NodeIdNumber: %d", i);
    }

    return ok;

}

bool RFM2g::InitializeHostsToReadInfo() {

    hostsToReadInfo = new HostReadMappingInfo[nOfHosts];

    bool ok = (hostsToReadInfo != NULL);

    if (ok) {
        uint32 i = 0u;

        for (i = 0u; i < nOfHosts; i++) {
            hostsToReadInfo[i].hostToReadOffset = 0u;
            hostsToReadInfo[i].hostToReadSize = 0u;
        }
    }
    else {
        REPORT_ERROR(ErrorManagement::FatalError, "Failed to allocate hostsToReadInfo");
    }

    return ok;

}

ErrorManagement::ErrorType RFM2g::InternalRFMRemapping() {

    ErrorManagement::ErrorType err;

    bool exitRemapping = false;
    bool exitFindFinal = false;

    uint32 i = 0u;

    //this cycle browse for all the hosts within the range [readoffset, readoffset+inputsize]
    for (i = 0u; i < nOfHosts && !exitRemapping; i++) {

        if (hostsProtocolInfo[i].hostWriteoffset >= readoffset && hostsProtocolInfo[i].hostWriteoffset < readoffset + inputsize) {

            if (initialHostToRead == -1) {
                initialHostToRead = i;
                finalHostToRead = i;
            }
            else {
                finalHostToRead = i;
            }

            hostsToReadInfo[i].hostToReadOffset = hostsProtocolInfo[i].hostWriteoffset + i * sizeof(int32);
            hostsToReadInfo[i].hostToReadSize = hostsProtocolInfo[i].hostOutputsize;

        }
        else {

            exitRemapping = !(initialHostToRead == -1);

        }

    }

    //here we check if there exists an host before readoffset which is partially read
    if (initialHostToRead >= 1) {
        if (hostsProtocolInfo[initialHostToRead - 1].hostWriteoffset + hostsProtocolInfo[initialHostToRead - 1].hostOutputsize - 1 >= readoffset) {
            initialHostToRead--;

            hostsToReadInfo[initialHostToRead].hostToReadOffset = readoffset + initialHostToRead * sizeof(int32);
            hostsToReadInfo[initialHostToRead].hostToReadSize = hostsProtocolInfo[initialHostToRead].hostWriteoffset
                    + hostsProtocolInfo[initialHostToRead].hostOutputsize - readoffset;

        }
    }

    //here we check if host=0 (i.e., the master) is partially read and it is the unique host to be read
    if (initialHostToRead == -1) {
        if (hostsProtocolInfo[0].hostWriteoffset + hostsProtocolInfo[0].hostOutputsize - 1 >= readoffset) {
            initialHostToRead = 0;

            hostsToReadInfo[initialHostToRead].hostToReadOffset = readoffset;
            hostsToReadInfo[initialHostToRead].hostToReadSize = hostsProtocolInfo[initialHostToRead].hostWriteoffset
                    + hostsProtocolInfo[initialHostToRead].hostOutputsize - readoffset;

        }

    }

    //here we check if there exists a unique host that is read only partially
    if (initialHostToRead == -1) {
        for (i = 0u; i < nOfHosts && !exitFindFinal; i++) {
            REPORT_ERROR(ErrorManagement::Information, "*** PRINT  **** %d ", i);
            if (readoffset >= hostsProtocolInfo[i].hostWriteoffset && readoffset < hostsProtocolInfo[i].hostWriteoffset + hostsProtocolInfo[i].hostOutputsize) {
                initialHostToRead = i;
                finalHostToRead = i;
                hostsToReadInfo[i].hostToReadOffset = readoffset + i * sizeof(int32);
                exitFindFinal = true;
            }

        }

    }

    //here we check if the final host is read only partially
    if (initialHostToRead != -1) {
        if (hostsProtocolInfo[finalHostToRead].hostWriteoffset + hostsProtocolInfo[finalHostToRead].hostOutputsize >= readoffset + inputsize) {

            hostsToReadInfo[finalHostToRead].hostToReadSize = readoffset + inputsize - hostsToReadInfo[finalHostToRead].hostToReadOffset
                    + finalHostToRead * sizeof(int32);

        }
    }

    /////here the print of each host////

    REPORT_ERROR(ErrorManagement::Information, "*** Remapping of host %d **** ", nodeIdNumber);

    for (i = 0u; i < nOfHosts; i++) {

        REPORT_ERROR(ErrorManagement::Information, "host %d  remapped offset: %d ", i, hostsToReadInfo[i].hostToReadOffset);
        REPORT_ERROR(ErrorManagement::Information, "host %d  remapped size: %d ", i, hostsToReadInfo[i].hostToReadSize);

    }

    REPORT_ERROR(ErrorManagement::Information, "initial host to read: %d ", initialHostToRead);
    REPORT_ERROR(ErrorManagement::Information, "final host to read: %d ", finalHostToRead);

    //////////////////////////////////

    err.fatalError = false;

    return err;

}

ErrorManagement::ErrorType RFM2g::inputSizeRemapping() {

    ErrorManagement::ErrorType err;

    inputsizeRemapped = 0u;

    int32 i = initialHostToRead;

    for (i = initialHostToRead; i < finalHostToRead; i++) {
        inputsizeRemapped = inputsizeRemapped + hostsToReadInfo[i].hostToReadSize + sizeof(int32);
    }

    //the last host has to be whole read since its counter is appended at the end
    inputsizeRemapped = inputsizeRemapped + hostsProtocolInfo[finalHostToRead].hostOutputsize + sizeof(int32);

    //
    REPORT_ERROR(ErrorManagement::Information, "inputsizeRemapped: %d ", inputsizeRemapped);
    //

    err.fatalError = false;

    return err;

}

bool RFM2g::InitializeCounterRead() {

    counterRead = new int32[nOfHosts];

    bool ok = (counterRead != NULL);

    if (ok) {
        uint32 i = 0u;

        for (i = 0u; i < nOfHosts; i++) {
            counterRead[i] = 0;
        }
    }
    else {
        REPORT_ERROR(ErrorManagement::FatalError, "Failed to allocate counterRead");
    }

    return ok;

}

bool RFM2g::InitializeDiagnosticData() {

    diagnosticData = new float32[nOfHosts];

    bool ok = (diagnosticData != NULL);

    if (ok) {
        uint32 i = 0u;

        for (i = 0u; i < nOfHosts; i++) {
            diagnosticData[i] = 0;
        }
    }
    else {
        REPORT_ERROR(ErrorManagement::FatalError, "Failed to allocate  diagnosticData");
    }

    return ok;

}

bool RFM2g::InitializeDiagnosticRatio() {

    diagnosticRatio = new float32[nOfHosts];

    bool ok = (diagnosticRatio != NULL);

    if (ok) {
        uint32 i = 0u;

        for (i = 0u; i < nOfHosts; i++) {
            diagnosticRatio[i] = 0;
        }
    }
    else {
        REPORT_ERROR(ErrorManagement::FatalError, "Failed to allocate  diagnosticRatio");
    }

    return ok;

}

void RFM2g::readRemapping() {
    void *indexpointerInternal = pInputBufferInternal;
    void *indexpointerExternal = pInputBuffer;

    int32 i = initialHostToRead;

    for (i = initialHostToRead; i <= finalHostToRead; i++) {

        //copy the data of host i from the DMA to the input buffer
        MemoryOperationsHelper::Copy(indexpointerExternal, indexpointerInternal, hostsToReadInfo[i].hostToReadSize);

        //shift the pointers to the next host (indexpointerExternal) and to the counter of host i (indexpointerInternal)
        indexpointerInternal = (void*) ((uint8*) indexpointerInternal + hostsToReadInfo[i].hostToReadSize);
        indexpointerExternal = (void*) ((uint8*) indexpointerExternal + hostsToReadInfo[i].hostToReadSize);

        int32 *counterPointer = (int32*) indexpointerInternal;
        counterRead[i] = *counterPointer;

#ifdef _DEBUG
        //here I print all the counters but not the last one, since it is computed later (the last host may be read only partially)
        if(i<finalHostToRead)
        REPORT_ERROR(ErrorManagement::Information, "counter of host  %d:  %d ", i,  counterRead[i]);
#endif

        //shift the pointer indexpointerInternal to the next host
        indexpointerInternal = (void*) ((uint8*) indexpointerInternal + sizeof(int32));

    }

    //here I grab the counter of the last host
    int32 *lastcounterPointer = (int32*) ((uint8*) indexpointerInternal - sizeof(int32) - hostsToReadInfo[finalHostToRead].hostToReadSize
            + hostsProtocolInfo[finalHostToRead].hostOutputsize);
    counterRead[finalHostToRead] = *lastcounterPointer;

#ifdef _DEBUG


        REPORT_ERROR(ErrorManagement::Information, "counter of host  %d:  %d ", finalHostToRead,  counterRead[finalHostToRead]);

#endif

}

ErrorManagement::ErrorType RFM2g::SettingDiagnosticProtocol() {

    ErrorManagement::ErrorType err;




    err = SetInitialInfo();

    if (!err.fatalError) {
        err.fatalError = !CheckMemoryContiguity();
    }

    if (!err.fatalError) {
        err = InternalRFMRemapping();
    }

    if (!err.fatalError) {
        err = inputSizeRemapping();
    }

    if (!err.fatalError) {
        EvaluateDiagnostcRatio();
    }

    else {
        REPORT_ERROR(ErrorManagement::FatalError, "Failed to set the diagnostic protocol");
    }




    return err;

}

void RFM2g::EvaluateDiagnostcRatio() {

    int32 i = initialHostToRead;

    for (i = initialHostToRead; i <= finalHostToRead; i++) {
        diagnosticRatio[i] = ((float32) hostsProtocolInfo[i].hostDownsamplefactor) / ((float32) downsamplefactor);

#ifdef _DEBUG


        REPORT_ERROR(ErrorManagement::Information, "ratio of the downsamplefactor of host  %d with this host:  %f ", i,  diagnosticRatio[i]);

#endif

    }

}

void RFM2g::EvaluateDiagnostcData() {

    int32 i = initialHostToRead;

    for (i = initialHostToRead; i <= finalHostToRead; i++) {
        diagnosticData[i] = counterRead[nodeIdNumber] - diagnosticRatio[i] * counterRead[i];

#ifdef _DEBUG


           REPORT_ERROR(ErrorManagement::Information, "diagnostic data of host  %d:  %f ", i,  diagnosticData[i]);

   #endif

    }

}

CLASS_REGISTER(RFM2g, "1.0")
CLASS_METHOD_REGISTER(RFM2g, StopLLC)
CLASS_METHOD_REGISTER(RFM2g, SettingDiagnosticProtocol)

}
