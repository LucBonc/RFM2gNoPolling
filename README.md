# RFM2gNoPolling
To compile the DataSource  make a symbolic link, in this  folder, to the rfmg_drv install directory


To enable debug printf enable flag CPPFLAGS=-D_DEBUG in Makefile.inc and configure  with slow frequency, say  1 Hz!!!

 
The RFM Datasource configured for master uses SyncronisedOutputBroker for output and MemoryMappedInputBroker
for Input. This means that during the Syncronise method of the DataSource the input data from the rfm are stored locallyread
after the synch cycle that works as follow:

1. the master writes its own data starting from its writeoffset for the dimension s specified in the outputBuffer channel;
2. the master change the counter via poke/peek operations (standard TCV approach) the counter. The counter is located at the start of rfm memory (bytes 0-8??);
3. the master polls sleep a configured time;
4. the master read the input from rfm starting from the readoffset and for the dimensions specified in the inputBuffer channel;

The RFM Datasource configured for slaves uses SyncronisedInputBroker for input and MemoryMappedOutputBroker
for output. This means that during the Syncronise method of the DataSource the ouput data in rfm are flushed to the network after 
after the synch cycle that works as follow:

1. the slave polls via  peek operations (standard TCV approach) the counter;
2. the slave writes its own data starting from its writeoffset for the dimension specified in the outputBuffer channel;
3. the slave polls sleep a configured time;
4. the slave read the input from rfm starting from the readoffset and for the dimensions specified in the inputBuffer channel;

Additional Configuration options respect SPC version of DataSource:
* When the DataSource is master it can be configured just with ExecutionMode=RealTimeThread, while if it is slave it can be 
* also EmbeddedThread.
* The configurable parameter for poll spleep time operation between read and write is named  TimeOut, and espressed in usec.
* The NumberOfHost paramenter corresponds to the number of host in the rfm network.
* The NodeIdNumber shall be 0 for master and 1,2,3....for slaves.
* The the rfm memory shall be mapped continuos between all the hosts. Each host has its own write piece of memory on the rfm devices, using appropriate writes offsets (>4096).
* The hosts readoffsets can start from any address in the range of the writes one, according with total dimensions.

* The DataSource adds the following the output signals:
  1. RealTime measured used tsc Counter, using the MARTe2 default frequency found in /proc/cpuinfo (not the tsc calibrated one), the tsc offset is taken at the first cycle
  2. Counters (NumberOfHost uint32) array, that contains the counter placed ath the end of OutputBuffer and popolated by each host
  3. Diagnostics  (NumberOfHost uint32) array that contains the age of the packet, expressed  in packet counts, respect the local current time ( local_counter - remote_host1_counter*R where R is the ratio between hosts downsampling factors)
  


