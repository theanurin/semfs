#!/usr/bin/python
import socket
import os,sys
import logging

def start(host, port, maxMountPoints):
    """ Creating the socket connection between daemon and driver. In 
    case the given port is not free, the function finds a free port 
    within the limit specifed by the maxPort.
    INPUT:
         -    The port in which the socket should be created. 
         -    maxPort till which it will search for available ports.
         -    maxMountPoints that this DaemonServer can support.
    OUTPUT:
         -    Creates a Server socket and returns the handle."""
    try: 
        # create a socket
        # associate the socket with a port.
        fsSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        fsSocket.bind((host, port))
        logging.info("Connection to socket successfull")
    except: 
        logging.exception("Server Cannot create a socket or it cant bind \
                        to the specified port")
        print "Stopping ..."
        sys.exit(False)
    
    try:
        # Keep listening on the port till a connection request is got.
        fsSocket.listen(maxMountPoints)
        logging.info("Listening")
    except:
        logging.exception('Unable to accept the clients request')
        print "Stopping ..."
        sys.exit(False)
    
    return fsSocket

def read(fsDriver,maXmlFileLength):
    """ Read a xml file from the socket. This is the Xml file that
    will be treated as the query that the driver sends the Server.
    INPUT: 
         -    Client handle to read data from that.
         -    maXmlfileLength denotes the maximum amount of data to read
              from the socket.
    OUTPUT: The data that is read form the socket."""
    try:
        """TODO: Read string from client (assumed here to be so short 
        that one call to recv() is enough), and make multiple copies 
        (to show the need for the "while" loop on the client side)"""
        data = fsDriver.recv(maXmlFileLength)
        logging.info("Obtained a query from the Driver")
    except:
        logging.exception('Unable to read from the socket')
        
    return data

def write(fsDriver, dataReturn):
    """ Writes the data to the socket that can be accessed by the driver. 
    Basically this is used for return the result to the driver 
    INPUT:
        -    The Driver socket handle to write.
        -    The data to be written
    OUTPUT: Writes the data in a xml format to the driver."""
    try:
        """The client will block until server sends). The xml should be 
        parsed and the corresponding action should be taken."""
        fsDriver.send(dataReturn)
        logging.info("Data written on the driver port")
    except:
        logging.exception('Unable to write the result to the driver')