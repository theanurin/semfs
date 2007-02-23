import logging
__all__ = ["daemonProcess", "socketListener", "xmlParser", "queryProcessor"]

host = 'localhost'           # can leave this blank on the server side
port =  40000                # check if this port is avaiable on the system.
maxMountPoints = 5           # Currently 5 partitions can be mounted
maXmlFileLength = 1000000    # Not Recommended to change this.
maxClientThreads = 5         # Not Recommended to change this.

logDirectory = '/tmp/'
pluginDirectoryPath = "plugins" # Relative to SemFS directory
semFSInstalledDirectory = "/home/venkat/workspace/SemFS/src"