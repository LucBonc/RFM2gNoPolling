# RFM2gNoPolling
To compile the DataSource  make a symbolic link, in this  folder, to the rfmg_drv install directory


To enable debug printf enable flag CPPFLAGS=-D_DEBUG in Makefile.inc and configure  with slow frequency, say  1 Hz!!!

 
The RFM Datasource configured for master uses SyncronisedOutputBroker for output and MemoryMappedInputBroker
for Input. This means that during the Syncronise method of the DataSource the input data from the rfm are stored locally and read
after the synch cycle that works as follow:

1. the master writes its own data starting from its writeoffset for the dimensions specified in the outputBuffer channel;
2. the master changes the counter via poke/peek operations (standard TCV approach). The counter and the timer arei located at the start of rfm memory (bytes 0-8);
3. the master polls sleep a configured time;
4. the master read the input from rfm starting from the readoffset and for the dimensions specified in the inputBuffer channel;

The RFM Datasource configured for slaves uses SyncronisedInputBroker for input and MemoryMappedOutputBroker
for output. This means that during the Syncronise method of the DataSource the ouput data in rfm are flushed to the network after 
the synch cycle that works as follow:

1. the slave polls via  peek operations (standard TCV approach) the counter;
2. the slave writes its own data starting from its writeoffset for the dimension specified in the outputBuffer channel;
3. the slave polls sleep a configured time;
4. the slave read the input from rfm starting from the readoffset and for the dimensions specified in the inputBuffer channel;

Additional Configuration options respect SPC version of DataSource:
* When the DataSource is master it can be configured only with ExecutionMode=RealTimeThread, while if it is slave it can be 
* also EmbeddedThread.
* The configurable parameter for poll spleep time operation between read and write is named  TimeOut, and espressed in usec.
* The NumberOfHost paramenter corresponds to the number of host in the rfm network.
* The NodeIdNumber shall be 0 for master and 1,2,3....for slaves. The nodeIdNumber must be consecutive
* The the rfm memory shall be mapped continuosly woth respect to all the hosts (0,1,2...) according to the NodeIdNumber. Each host has its own write piece of memory on the rfm devices, using appropriate writes offsets (>4096).
* The hosts readoffsets can start from any address in the range of the writes one, according to the total dimensions.

* The DataSource adds the following output signals:
  1. RealTime measured used tsc Counter, using the MARTe2 default frequency found in /proc/cpuinfo (not the tsc calibrated one), the tsc offset is taken at the first cycle
  2. Counters (NumberOfHost uint32) array, that contains the counter of each host (for those hosts read by this host, zero otherwise). Such counter is implicitly written by each host at any rfm writing operation and located at the end of the 
  3. Diagnostics  (NumberOfHost uint32) array that contains the age of the packet (for those hosts read by this host, zero otherwise), expressed  in packet counts, respect the local current time ( local_counter - remote_host1_counter*R where R is the ratio between hosts downsampling factors)
     When a diagnostic value of this host is negative, it means that this host lost cycles with respect to the other host (this host recognize it just when it stops being blocked).
     When the Diagnostic counter of the Master with itself is negative, this means that the master is not able to update its counter on the rfm, therefore it is not able to trigger the data exchanges on the rfm   


 
An example of configuration can be found at [this repo](https://github.com/LucBonc/RFM2gNoPollingConfigurations_Trees)
