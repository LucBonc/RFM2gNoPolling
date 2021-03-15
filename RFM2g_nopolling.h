/**
 * @file RFM2g_nopolling.h
 * @brief Header file for class RFM2g
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

 * @details This header file contains the declaration of the class RFM2g
 * with all of its public, protected and private members. It may also include
 * definitions for inline methods which need to be visible to the compiler.
 *
 *
 *
 *
 *         +RFM = {
 Class = RFM2g
 ExecutionMode = RealTimeThread //Optional. If not set ExecutionMode = IndependentThread. If ExecutionMode == IndependentThread a thread is spawned to generate the time events. ExecutionMode == RealTimeThread the time is generated in the context of the real-time thread.
 CPUMask = 0x8//Optional and only relevant if ExecutionMode=IndependentThread
 Device = /dev/rfm2g0// Mandatory, the Linux device handling the RFM card installed on the system
 MasterStepMaxRetries=100//Optional and only relevant for Master. Default 100
 ReadOffset = 4096// Mandatory, the offset in bytes of the read starting point in the RF memory
 WriteOffset = 4096// Mandatory, the offset in bytes of the write starting point in the RF memory (Rmember: aways start after 4096)

 UseDMA = 1// Optional, if 1 data exchange will be performed using DMA, if 0 with programmed IO. Default = 0
 DMABufferAddress = 0x1f3600000//0x3aec00000 // Required if UseDMA=1, physical address (BEWARE, NOT VIRTUAL, i.e. coming from cat /proc/iomem) of the kernel reserved DMA memory buffer (see node (1))
 WaitDMA = 1// Required if UseDMA=1, if 0 the DataSource launches DMA read/write transactions without waiting for them to be completed. If 1 it waits for them. (see node (2))
 DMABufferSize = 4096// The DMA buffer size
 DMAThreshold = 32// The DMA threshold after which DMA must be used (bytes)

 //Synchronizing = 0 // Optional, if 1 the DataSource synchronizes the calling thread using SPC synchronization protocol, if 0 it doesn't synchronize and only exchanges data. Default = 0
 //BasePeriod = 1e-4 // Required if Synchronizing=1, the base period of the RFM synchronization clock (coming from the RFM master mode)
 DownSampleFactor = 1// Required if Synchronizing=1, the downsample factor for synchronization strobes
 StartTime = 0// Required if Synchronizing=1, the start time at which the DataSource will begin to synchronize

 Master = 1// Optional, if 1 the node is the RFM synchronizing node, i.e. sends the system time around the RFM ring. Default = 0. Note that one and only one master must be defined!
 InitRunTime =0// 1000000

 NumberOfHosts=3// Mandatory. Number of host on the RFM
 TimeOut=20// Optional.  Time out (in microseconds) to wait for hosts writing operations. Dafault is 1 second (i.e., 1000000)

 NodeIdNumber=0//Required. For the master always NodeIdNumber=0. For the slaves, a consecutive exclusive integer number, from 1 to ... NumberOfHosts-1



 Signals = {
 Counter = {Type = uint32}
 Time = {Type = uint32}
 InputBuffer = {Type = uint8 NumberOfElements = 1200}  //RR
 OutputBuffer = {Type = uint8 NumberOfElements =400}   //RR
 RealTime = {Type = float64}
 Counters = {Type = uint8 NumberOfElements = 12}
 Diagnostics = {Type = uint8 NumberOfElements = 12}
 }
 }
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */

#ifndef RFM2G_interrupts_H_
#define RFM2G_interrupts_H_

/*---------------------------------------------------------------------------*/
/*                        Standard header includes                           */
/*---------------------------------------------------------------------------*/
#include "DataSourceI.h"
#include "MessageI.h"
#include "EventSem.h"
#include "EmbeddedServiceMethodBinderI.h"
#include "SingleThreadService.h"
#include "RegisteredMethodsMessageFilter.h"
#include "rfm2g_drv/include/rfm2g_api.h"
#include "rfm2g_drv/include/rfm2g_osspec.h"
#include "rfm2g_drv/include/rfm2g_defs.h"

/*---------------------------------------------------------------------------*/
/*                        Project header includes                            */
/*---------------------------------------------------------------------------*/

#define RFM_TRIG_OFFSET      3*sizeof(int)
#define RFM_ITERATION_OFFSET 0
#define RFM_TIME_OFFSET      1*sizeof(int)

//here the structure that packs a single host protocol information
struct HostCounterProcInfo {

    RFM2G_UINT32 hostWriteoffset;
    RFM2G_UINT32 hostOutputsize;
    MARTe::uint32 hostDownsamplefactor;
};

//here the structure that contains a single host read mapping info
struct HostReadMappingInfo {

    RFM2G_UINT32 hostToReadOffset;
    RFM2G_UINT32 hostToReadSize;
};

//here the start of the RFM reserved space for the diangostic counter protocol
#define RFM_START_PROTOCOL      64

#define SIZE_OF_HOST_PROTOCOL_DATA sizeof(HostCounterProcInfo) //in the order:  writeoffset, outputsize, downsamplefactor

#define RFM_SYSTEM_BUFFER RFM_START_PROTOCOL+256*SIZE_OF_HOST_PROTOCOL_DATA

/*---------------------------------------------------------------------------*/
/*                           Class declaration                               */
/*---------------------------------------------------------------------------*/
namespace MARTe {

/**
 * The default waiting period during polling operations
 */
const float64 SLEEP_WAITING_PERIOD = 10.F;
const float64 TIMEOUT_PERIOD = 1000000.F;
const int16 MASTERSTEP_MAX_RETRIES = 100;

/**
 * @brief GE/FANUC-Abaco Systems 5565 Reflective Memory series card  DataSource
 * @details The RFM2g is a fast and operating system independent network which
 * exchange data between nodes (cards) installed on different computers.
 * Data written on the memory of one card is seamlessly exchanged with the
 * other cards unless all the memories shares all the contents.
 * The configuration syntax is (names are only given as an example):
 * <pre>
 * +ReflectiveMemory = {
 *     Class = RFM2g
 *     ExecutionMode = IndependentThread //Optional. If not set ExecutionMode = IndependentThread. If ExecutionMode == IndependentThread a thread is spawned to generate the time events. ExecutionMode == RealTimeThread the time is generated in the context of the real-time thread.
 *     CPUMask = 0x8 //Optional and only relevant if ExecutionMode=IndependentThread
 *     Device = /dev/rfm2g0 // Mandatory, the Linux device handling the RFM card installed on the system (see note (3))
 *     ReadOffset = 800 // Mandatory, the offset in bytes of the read starting point in the RF memory
 *     WriteOffset = 800 // Mandatory, the offset in bytes of the write starting point in the RF memory
 *
 *     UseDMA = 0 // Optional, if 1 data exchange will be performed usig DMA, if 0 with programmed IO. Default = 0
 *     DMABufferAddres = 0x17fffffff // Required if UseDMA=1, physical address (BEWARE, NOT VIRTUAL, i.e. coming from cat /proc/iomem) of the kernel reserved DMA memory buffer (see node (1))
 *     WaitDMA = 1 // Required if UseDMA=1, if 0 the DataSource launches DMA read/write transactions without waiting for them to be completed. If 1 it waits for them. (see node (2))
 *     DMABufferSize = 1024 // Required if UseDMA=1, the size of the userspace DMA buffer, see node (4)
 *     DMAThreshold = 32 // Required, the transfer size above which DMA will be triggered
 *
 *     //Synchronizing = 0 // Optional, if 1 the DataSource synchronizes the calling thread using SPC synchronization protocol, if 0 it doesn't synchronize and only exchanges data. Default = 0
 *
 *     // If synchronizing (at least 1 signal with Frequency property set), then
 *     // SPC synchronization mechanism is used and the DataSource can synch the
 *     // calling thread on the system RFM master using SPC protocol
 *     DownSampleFactor = 10 // Required if Synchronizing=1, the downsample factor for synchronization strobes
 *     StartCycle = 10 // Required if Synchronizing=1, the start cycle at which the DataSource will begin to synchronize
 *
 *     Master = 1 // Optional, if 1 the node is the RFM synchronizing node, i.e. sends the system time around the RFM ring. Default = 0
 *                // in this case RFM is always synchronizing in its inputs
 *     //BasePeriod = 100 // Required if master mode, the base period in us of the strobe signal (aka us per counter step)
 *     //StartTime =  // Required if master mode, the base period in us of the strobe signal (aka us per counter step)
 *     InitRunTime = -10000000 // Required if master mode, time value to be set oin the RFM when mester enters Run
 *     MasterStepMaxRetries = 100 // Required if master mode, the number of retries of failed master steps before giving un (within a cycle)
 *
 *     //InputEnabled = 1  // To be implemented
 *     //Outputenabled = 1 // To be implemented
 *
 *     Cycles = 1000 // Number of cycles before exiting the LLC phase and sending the TermMessage1, see note (5)
 *
 *     Signals = {
 *         Counter = {
 *             Type = int32
 *             // MASTER MODE: output, the cycle counter to be written to RFM synch area
 *             // SLAVE MODE : input, the active real time cycle counter retrieved from RFM synch area
 *         }
 *         Time = {
 *             Type = int32
 *             Frequency = 1000
 *             // MASTER MODE: output, the system time to broadcast on the RFM synch area
 *             // SLAVE MODE : input, the system time in us retrieved from RFM synch area
 *         }
 *         InputBuffer = {
 *             Type = uint8
 *             NumberOfElements = 100
 *             // The input buffer (RFM -> system), the buffer is treated as a contiguous memory segment, size in bytes
 *         }
 *         OutputBuffer = {
 *             Type = uint8
 *             NumberOfElements = 100
 *             // The output buffer (system -> RFM), the buffer is treated as a contiguous memory segment, size in bytes*
 *         }
 *     }
 *
 *     +TermMessage1 = { Class=Message Destination=StateMachine Function=RUNCOMPLETE }
 * }
 * </pre>
 *
 * Brokers configurations:
 *
 * BROKERS CONFIGURATION
 * -------------------------------------------------------------
 * Master mode      INPUT:  MemoryMapInputBroker
 * (node02)         OUTPUT: MemoryMapSynchronisedOutputBroker
 *                  NOTES:  RFM synch info are updated in the Synchronize
 *                          method according to the running cycle
 *                          in the context of the same rt thread.
 *                          On even cycles data is read from RFM returned and
 *                          stored on local buffer for the write cycle
 *                          read call.
 *                          On odd cycles data is written to RFM, read returns
 *                          the buffered data.
 *                  THREAD MODEL: RTTHREAD
 *
 * Slave mode       INPUT:  MemoryMapSynchronisedInputBroker
 * synchronizing    OUTPUT: MemoryMapOutputBroker
 * (node03,06)      NOTES:  The calling thread is blocked until the next
 *                          right (even and downsampled) read RFM cycle, data is written
 *                          on the following RFM cycle and then the thread is released.
 *                          The output broker must execute before the input one, probably.
 *                  THREAD MODEL: RTTHREAD AND SPAWNED, see node (4)
 *
 * Slave mode       INPUT:  MemoryMapInputBroker
 * not synchroniz.  OUTPUT: MemoryMapAsyncOutputBroker
 * (node07,08)      NOTES:  Only meaningful in the context of a separate thread
 *                          (as indeed done for node 07 and node08 RFM synch already now)
 *                          The thread notify the asynch thread of new data available
 *                          (once per cycle), the detached thread performs synchronized
 *                          data exchange with the RFM. Fresh read data are stored
 *                          in a buffer for next read calls.
 *                  THREAD MODEL: SPAWNED
 *
 *
 * NOTES:
 * (1) In order to use DMA, the RFM device driver must use a physical memory buffer reserved for it by the kernel.
 *     Usually the procedure to set it up involves reserving an upper machine memory buffer via the 'mem' option of Linux start command line (via GRUB cfg file)
 *     reading its start physical address via cat /proc/iomem and reporting it in the DMABufferAddres
 *     Usually the kernel free RAM buffer is named 'RAM buffer' in /proc/iomem outout.
 *     When testing on the same board pay attention at separating the buffers between different processes
 *     otherwise DMA transfers will insisting in the same buffer from different processes!
 * (2) For very fast operations, the DataSource can launch DMA transfers without waiting for them to be completed
 *     In this case, is up to the user to check whether they complete in time or not. Usually, if the paylod is fixed
 *     this is performed via fiducial data fields check or time benchmarking via CPU TSC (Time Stamp Counter)
 *     It is however much more safe to use the waited DMA approach, if it fits into cycle times.
 * (3) This DataSource relies on the SPC patched RFM Linux device driver currently available
 *     on spcsvn SVN server
 * (4) Currently (on tcvrt02b.epfl.ch machine) modes RTTHREAD and SPAWNED on the same RTTHread block
 *     the console log output, while MARTe is running correctly. HTTP message server is alive
 *     so messages are passed and a shot cycle could be done without problems. Anyway SPAWNED
 *     thread on a dedicated CPU is working w/o problems and should be preferred.
 * (5) Only valid in slave - synchronizing mode
 *
 */

/**
 * Semaphore to handle multiple RFM instances
 */
FastPollingMutexSem fastMuxRFM;

class RFM2g: public DataSourceI, public MessageI, public EmbeddedServiceMethodBinderI {
public:CLASS_REGISTER_DECLARATION()
    /**
     * @brief Default constructor
     * @post
     *   Counter = 0
     *   Time = 0
     */
    RFM2g();

    /**
     * @brief Destructor. Stops the EmbeddedThread.
     */
    virtual ~RFM2g();

    /**
     * @brief See DataSourceI::AllocateMemory.
     */
    virtual bool AllocateMemory();

    /**
     * @brief See DataSourceI::GetNumberOfMemoryBuffers.
     * @return 1.
     */
    virtual uint32 GetNumberOfMemoryBuffers();

    /**
     * @brief See DataSourceI::GetNumberOfMemoryBuffers.
     */
    virtual bool GetSignalMemoryBuffer(const uint32 signalIdx,
                                       const uint32 bufferIdx,
                                       void *&signalAddress);

    /**
     * @brief See DataSourceI::GetBrokerName.
     * @details
     * @return MemoryMapSynchronisedInputBroker if frequency > 0, MemoryMapInputBroker otherwise.
     */
    virtual const char8* GetBrokerName(StructuredDataI &data,
                                       const SignalDirection direction);

    /**
     * @brief Waits on an EventSem for the right RFM cycle
     * @return true if the semaphore is successfully posted.
     */
    virtual bool Synchronise();

    /**
     * @brief Callback function for an EmbeddedThread.
     * @details
     * @param[in] info not used.
     * @return NoError if the EventSem can be successfully posted.
     */
    virtual ErrorManagement::ErrorType Execute(ExecutionInfo &info);

    /**
     * @brief starts the EmbeddedThread.
     * @details See StatefulI::PrepareNextState. Starts the EmbeddedThread (if it was not already started) and loops
     * on the ExecuteMethod.
     * @return true if the EmbeddedThread can be successfully started.
     */
    virtual bool PrepareNextState(const char8 *const currentStateName,
                                  const char8 *const nextStateName);

    /**
     * @brief Initialises the RFM2g
     * @param[in] data configuration in the form given above
     * @return true if initialization successfull
     */
    virtual bool Initialise(StructuredDataI &data);

    /**
     * @brief Verifies that two, and only two, signal are set with the correct type.
     * @details Verifies that two, and only two, signal are set; that the signals are
     * 32 bits in size with a SignedInteger or UnsignedInteger type and that a Frequency > 0 was set in one of the two signals.
     * @param[in] data see DataSourceI::SetConfiguredDatabase
     * @return true if the rules above are met.
     */
    virtual bool SetConfiguredDatabase(StructuredDataI &data);

    /**
     * @brief Gets the affinity of the thread which is going to be used to asynchronously wait for the time to elapse.
     * @return the affinity of the thread which is going to be used to asynchronously wait for the time to elapse.
     */
    const ProcessorType& GetCPUMask() const;

    /**
     * @brief Gets the stack size of the thread which is going to be used to asynchronously wait for the time to elapse.
     * @return the stack size of the thread which is going to be used to asynchronously wait for the time to elapse.
     */
    uint32 GetStackSize() const;

    /**
     * @brief sets on the RFM the host own data for the diagnostic protocol, namely: writeoffset, outputsize, downsamplefactor
     * @return true if the writing operations of the data have been successful. Otherwise return false
     */
    bool SetDiagnosticOwnData();

    /**
     * @brief check if the RFM allocation of the master and all the hosts is contiguous, i.e, there are no holes in the memory segment
     * @return true if the allocation is contiguous and all the hosts writes according to the order given by their NodeIdNumber. Otherwise return false
     */
    bool CheckMemoryContiguity();

    /**
     * @brief set the information of the other hosts from RFM to implement the diagnostic counter protocol
     */
    ErrorManagement::ErrorType SetInitialInfo();

    /**
     * @brief do a re-mapping of the RFM writing regions taking into account the counter for each host
     */
    ErrorManagement::ErrorType InternalRFMRemapping();

    /**
     * @brief computes the remapped size to be read (the sum of the size to be read of all the hosts from initialHostToRead until finalHostToRead plus the counters)
     */
    ErrorManagement::ErrorType inputSizeRemapping();

    /**
     * @brief launch the functions: SetInitialInfo; InternalRFMRemapping; inputSizeRemapping so as to set the diagnostic counter protocol
     */
    ErrorManagement::ErrorType SettingDiagnosticProtocol();

    ErrorManagement::ErrorType StopLLC();

private:

    /**
     * Current counter and timer
     */
    int32 counterAndTimer[2];

    /**
     * The semaphore for the synchronisation between the EmbeddedThread and the Synchronise method.
     */
    EventSem synchSem;

    /**
     * The EmbeddedThread where the Execute method waits for the period to elapse.
     */
    SingleThreadService executor;

    /**
     * Index of the function which has the signal that synchronises on this DataSourceI.
     */
    uint32 synchronisingFunctionIdx;

    /**
     * True if this a synchronising data source
     */
    bool synchronising;

    /**
     * The affinity of the thread that asynchronously generates the time.
     */
    ProcessorType cpuMask;

    /**
     * The size of the stack of the thread that asynchronously generates the time.
     */
    uint32 stackSize;

    /**
     * The execution mode.
     */
    uint32 executionMode;

    /**
     * The driver device
     */
    StreamString device;

    /**
     * Read offset in bytes
     */
    RFM2G_UINT32 readoffset;

    /**
     * Write offset in bytes
     */
    RFM2G_UINT32 writeoffset;

    /**
     * Input size in bytes
     */
    RFM2G_UINT32 inputsize;

    /**
     * Output size in bytes
     */
    RFM2G_UINT32 outputsize;

    /**
     * DMA enabled
     */
    bool usedma;

    /**
     * DMA userspace buffer mapped
     */
    bool dmamapped;

    /**
     * DMA buffer, hex reading from cff
     */
    StreamString dmabufferstr;

    /**
     * physical start address of the DMA buffer
     */
    RFM2G_UINT64 dmabufferaddr;

    /**
     * Waited DMA or not
     */
    bool waitdma;

    /**
     * Base period of the synchronizing strobe
     */
    //uint32 baseperiod;
    /**
     * Downsample factor w.r.t. the synchronizing strobe
     */
    uint32 downsamplefactor;

    /**
     * Start time w.r.t. the synchronizing strobe
     */
    //float32 starttime;
    int32 startcycle;

    /**
     * Master RFM node ?
     */
    bool master;

    /**
     * RFM device name (in RFM types)
     */
    RFM2G_CHAR rfmdevice[40];

    /**
     * RFM handle
     */
    RFM2GHANDLE rfmhandle;

    /**
     * RFM handle valid
     */
    bool rfmhandlevalid;

    /**
     * transfer threshold to trigger DMA
     */
    RFM2G_UINT32 dmathreshold;

    /**
     * DMA userspace buffer
     */
    volatile void *pDmaBuffer;

    /**
     * DMA userspace input buffer
     */
    //volatile void *pDmaInputBuffer;
    /**
     * DMA userspace output buffer
     */
    //volatile void *pDmaOutputBuffer;
    /**
     * Main userspace input buffer
     */
    //volatile void *pInputBuffer;
    void *pInputBuffer;
    //void *pInputBufferCopy;

    /**
     * Main userspace output buffer
     */
    //volatile void *pOutputBuffer;
    void *pOutputBuffer;

    /**
     * Input buffer for internal data exchange (decoupling buffer)
     */
    void *pInputBufferInternal;
    /**
     * Output buffer for internal data exchange (decoupling buffer)
     */
    void *pOutputBufferInternal;

    /**
     * total length of the DMA buffer size
     */
    RFM2G_UINT32 dmabuffersize;

    /**
     * number of hosts on the reflective memory
     */
    uint32 nOfHosts;

    /**
     * flags associated to the hosts
     */
    int32 *hostsFlags;

    /**
     * number of microseconds to wait for the timeout
     */
    uint32 timeOut;

    /**
     * number of ticks to wait for timeout
     */
    uint64 timeOutTicks;

    /**
     * A number which identifies the host to be assigned in the configuration file.
     * For the master always NodeIdNumber=0.
     * For the slaves, a consecutive exclusive integer number, from 1 to ... NumberOfHosts-1
     */
    uint32 nodeIdNumber;

    /**
     * node Id of the RFM host
     */
    RFM2G_NODE NodeId;

    /**
     * pointer to hosts info for diagnostic counter protocol
     */
    HostCounterProcInfo *hostsProtocolInfo;

    /**
     * pointer to reading info of the hosts to be read
     */
    HostReadMappingInfo *hostsToReadInfo;

    /**
     * host number of the first host to be read
     */
    int32 initialHostToRead;

    /**
     * host number of the last host to be read
     */
    int32 finalHostToRead;

    /**
     * inputsize taking into account the size of all the hosts to be read, from  initialHostToRead until finalHostToRead and all their the counters
     */
    RFM2G_UINT32 inputsizeRemapped;

    /**
     * vector containing the counters of the hosts read (from the RFM reading operation)
     * These counters will be used to compute the diagnostic data
     */
    int32 *counterRead;

    /**
     * vector containing the diagnostic information for checking if the data is outdated and/or cycles have been lost
     * The diagnosticData is an optional output of the RFM
     */
    float32 *diagnosticData;

    /**
     * vector containing the ratio of each of the downsamplefactor of the other hosts over the downsamplefactor of this host
     * The ratio is used in the function EvaluateDiagnostcData().
     * The diagnosticRatio is an optional output of the RFM
     */
    float32 *diagnosticRatio;

    /**
     * Semaphore to manage the buffer indexes.
     */
    FastPollingMutexSem fastMux;

    /**
     * Number of RFM instances. It is used to avoid to create the fastMuxRFM more than one time
     */
    static uint8 numberOfinstances;

    /**
     * RFM synchronization functions as per
     * SPC synch protocol until MARTe migration
     */

    /**
     * @brief Initialize the vector hostsToReadInfo at null values
     * @details First the vector hostsToReadInfo is dynamically allocated with the number of hosts and then it is initialized
     * @return true
     */
    bool InitializeHostsToReadInfo();

    /**
     * @brief Initialize the vector counterRead at null values
     * @details First the vector counterRead is dynamically allocated with the number of hosts and then it is initialized
     * @return true
     */
    bool InitializeCounterRead();

    /**
     * @brief Initialize the vector diagnosticData at null values
     * @details First the vector diagnosticData is dynamically allocated with the number of hosts and then it is initialized
     * @return true
     */
    bool InitializeDiagnosticData();

    /**
     * @brief Initialize the vector diagnosticRatio at null values
     * @details First the vector diagnosticRatio is dynamically allocated with the number of hosts and then it is initialized
     * @return true
     */
    bool InitializeDiagnosticRatio();

    /**
     * @brief computes the vector of diagnosticRatio
     * @details The value is computed performing, for each host actually read, the ratio among its downsamplefactor and the
     *  downsamplefactor of this host.
     *  The vector diagnosticRatio is used in the function EvaluateDiagnostcData();
     */
    void EvaluateDiagnostcRatio();

    /**
     * @brief computes the vector of diagnosticData
     * @details The value is computed performing, for each host that is actually read, the comparison
     * by the own counter (the one of this host) and the host counter (scaled with the downsamplefactor ration) of the other hosts.
     * If the value is positive, the other host data is old (an it is older than the downsamplefactor ratio, than it is outdated).
     * Conversely, if the value is negative, than this host is delayed.
     */
    void EvaluateDiagnostcData();

    /**
     * @brief Get the current value of the iteration counter
     * @details Verifies that conditions for reading the iteration counter are met
     * and reads it
     * @param[out] the value of the iteration
     * @return true if the iteration could be safely read
     */
    inline bool get_iteration(RFM2GHANDLE handle,
                              int32 *current_iteration);
    //inline bool get_iteration(RFM2GHANDLE handle, int32 *current_iteration, int32 *current_time);

    /**
     * @brief Slave handshaking function (spawned case)
     * @details The slave waits for the interrupt from the master, then writes and waits
     * for the interrupts from the other slaves (after they have written)
     * @param[out] interruptsCounter: the number of slave interrupts received by slave
     * @return true if everything worked in the proper way: no timeouts in receiving any of the interrupts,
     * no errors in sending the interrupt
     */
    bool slave_rfm_write_spawned(uint32 &interruptsCounter,
                                 ExecutionInfo &info);

    /**
     * @brief Slave handshaking function (no spawned case)
     * @details The slave waits for the interrupt from the master, then writes and waits
     * for the interrupts from the other slaves (after they have written)
     * @param[out] interruptsCounter: the number of slave interrupts received by slave
     * @return true if everything worked in the proper way: no timeouts in receiving any of the interrupts,
     * no errors in sending the interrupt
     */
    bool slave_rfm_write(uint32 &interruptsCounter,
                         ExecutionInfo &info);

    /*
     * @brief Writes iter to the iteration offset on the RFM
     * @details writes iter taking care of its protecting semaphore
     * @param[in] rfm_iter the iteration number to be written
     * @return true if the iteration could be safely written
     */
    bool rfm_master_step(int32 rfm_iter,
                         int32 time);

    /**
     * @brief Reads the inputbuffer from the relfective memory local memory
     */
    ErrorManagement::ErrorType Read(ExecutionInfo &info);

    /**
     * @brief Writes the outputbuffer to the relfective memory local memory
     */
    ErrorManagement::ErrorType Write(ExecutionInfo &info);

    /**
     * @brief this function remap the read data into the memory pointed by pInputBuffer
     */
    void readRemapping();

    /**
     * First synchronization happened
     */
    bool firstsynchelapsed;

    /**
     * The current exec cycle from RFM synch area
     */
    int32 currentcycle;

    int32 counter;

    int32 localcurrentcycle;

    int32 period;

    uint64 realTimeOffset;

    float64 realTime;
    /**
     * Ok to run to the polling synch parts
     */
    bool oktorun;

    /**
     * The number of (downsampled) rt cycles
     */
    int32 cycles;

    /**
     * counter for the slave local cycles (to take into account the downsample factor)
     */
    uint32 localCounter;

    /**
     * counter of the Embedded thread
     */
    int32 counterEmbedded;

    int32 test;

    /**
     * the message filter for receiving messages
     */
    MARTe::ReferenceT<MARTe::RegisteredMethodsMessageFilter> filter;

    /**
     * Termination message sent
     */
    bool termmsgsent;

    /**
     * Init time to be broadcast by the master when entering the run state
     */
    int32 initruntime;

    /**
     * NUmner of maximum master step retries before giving up master step in the cycle
     */
    int16 masterstepmaxretries;

protected:

};

}

/*---------------------------------------------------------------------------*/
/*                        Inline method definitions                          */
/*---------------------------------------------------------------------------*/

#endif /* RFM2G_H_ */

