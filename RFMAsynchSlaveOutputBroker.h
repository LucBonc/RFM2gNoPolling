/**
 * @file RFMAsynchSlaveOutputBroker.h
 * @brief Header file for class RFMAsynchSlaveOutputBroker
 * @date 28/03/2021
 * @authors Davide Liuzza, Luca Boncagni,  Cristian Galperti
 *
 * @cCopyright 2021 FSN-ENEA | Nuclear and Fusion Energy Department, ENEA Frascati (Rome)
 * Italy
 * @Copyright 2019 SPC | Swiss Plasma Center, EPFL Lausanne
 * Switzerland
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
 * */



#ifndef RFM_ASYNCH_SLAVE_OUTPUTBROKER_H
#define RFM_ASYNCH_SLAVE_OUTPUTBROKER_H

/*---------------------------------------------------------------------------*/
/*                        Standard header includes                           */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/*                        Project header includes                            */
/*---------------------------------------------------------------------------*/
#include "MemoryMapOutputBroker.h"
#include "FastPollingMutexSem.h"
#include "AdvancedErrorManagement.h"


/*---------------------------------------------------------------------------*/
/*                           Class declaration                               */
/*---------------------------------------------------------------------------*/

namespace MARTe {

/**
 * @brief Asynchronous slave output MemoryMapBroker implementation for RFM data transfer.
 * @details This broker copies (locked operation) all
 * the signals declared on this MemoryMapBroker (from the GAM memory to the DataSourceI memory).
 * The copy is locked since the same memory region is asynchonously accessed by the slave independent thread.
 */
class DLL_API RFMAsynchSlaveOutputBroker: public MemoryMapOutputBroker {
public:
    CLASS_REGISTER_DECLARATION()
    /**
     * @brief Default constructor. NOOP.
     */
    RFMAsynchSlaveOutputBroker();

    /**
     * @brief Destructor. NOOP.
     */
    virtual ~RFMAsynchSlaveOutputBroker();




    /**
     * @brief initialize the broker with the pointer to the RFM R/W lock and then call the inherited init.
     * @return true if the init is correctly performed
     */
    virtual bool Init(const SignalDirection direction, DataSourceI &dataSourceOut, const char8 * const functionName, void * const gamMemoryAddress,  FastPollingMutexSem* RFMfastMuxRWPointer);


    /**
     * @brief Sequentially copies all the
     * signals from the GAM  memory to the DataSourceI.
     * The copy is locked since the same memory region is asynchonously accessed by the slave independent thread
     * @return true if the synchronisation call is successful and if all copies are successfully performed.
     */
    virtual bool Execute();

private:

    FastPollingMutexSem* MuxRWPointer;


};

}

/*---------------------------------------------------------------------------*/
/*                        Inline method definitions                          */
/*---------------------------------------------------------------------------*/

#endif /* RFM_ASYNCH_SLAVE_OUTPUTBROKER_H */

