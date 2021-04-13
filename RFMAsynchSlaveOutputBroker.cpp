/**
 * @file RFMAsynchSlaveOutputBroker.cpp
 * @.cpp file for class RFMAsynchSlaveOutputBroker
 * @date 28/03/2021
 * @authors Davide Liuzza, Luca Boncagni,  Cristian Galperti
 *
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
 *
 * * */



/*---------------------------------------------------------------------------*/
/*                         Standard header includes                          */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/*                         Project header includes                           */
/*---------------------------------------------------------------------------*/
#include "RFMAsynchSlaveOutputBroker.h"

/*---------------------------------------------------------------------------*/
/*                           Static definitions                              */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/*                           Method definitions                              */
/*---------------------------------------------------------------------------*/

namespace MARTe {
RFMAsynchSlaveOutputBroker::RFMAsynchSlaveOutputBroker() :

        MemoryMapOutputBroker() {
        MuxRWPointer= static_cast<FastPollingMutexSem*>(NULL);

}

RFMAsynchSlaveOutputBroker::~RFMAsynchSlaveOutputBroker() {

}


bool RFMAsynchSlaveOutputBroker::Init(const SignalDirection direction, DataSourceI &dataSourceOut, const char8 * const functionName, void * const gamMemoryAddress, FastPollingMutexSem* RFMfastMuxRWPointer) {


    bool ret =true;


    ret =(RFMfastMuxRWPointer!=NULL);

    if(ret){
        MuxRWPointer=RFMfastMuxRWPointer;
        ret=MemoryMapOutputBroker::Init(direction, dataSourceOut,functionName, gamMemoryAddress);
    }

   return ret;

}


bool  RFMAsynchSlaveOutputBroker::Execute() {
    bool ret = true;
    if (dataSource != NULL_PTR(DataSourceI *)) {


       MuxRWPointer->FastLock(TTInfiniteWait, 0.);
        ret = MemoryMapOutputBroker::Execute();
       MuxRWPointer->FastUnLock();
    }

    return ret;
}







CLASS_REGISTER(RFMAsynchSlaveOutputBroker, "1.0")

}

