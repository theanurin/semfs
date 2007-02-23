#!/usr/bin/python
"""########################################################################
# 
# Semantic File Retrieval in File Systems using Virtual Directories
# 
# Created By 
#     Venkateswaran S (wenkat.s@gmail.com)
# 
# Role 
#    -  To create a socket, bind it and listen for the client 
#       connections from the file system driver
#    -  Parses the XML
#    -  Index using the query processor.
#    
# Assumption
#    The server gets a xml file from the socket.
#
#    Copyright (c) 2006, 
#    Prashanth Mohan <prashmohan@gmail.com>, 
#    Venkateswaran S <wenkat.s@gmail.com> and 
#    Raghuraman <raaghum2222@gmail.com> All rights reserved.
###########################################################################"""

import os
import sys
import logging
import Queue
import threading
import md5
from xml.dom.minidom import Document
from utils import *
import utils

def makeThisProcessAsDaemon():
    """The code, as it is, will create a new file in the root directory, when
    executed with superuser privileges.  The file will contain the following
    daemon related process parameters: return code, process ID, parent
    process group ID, session ID, user ID, effective user ID, real group ID,
    and the effective group ID."""
    try:
        retCode=0
        retCode = utils.daemonProcess.create()
        logging.basicConfig(level=logging.DEBUG,
                            format='%(asctime)s %(levelname)s %(message)s',
                            filename= utils.logDirectory + 'semfsDaemon.log',
                            filemode='w')
        
        ProcParams = \
        """ return code=%s \
        process ID=%s \
        parent process ID=%s \
        process group ID=%s \
        session ID=%s \
        user ID=%s \
        effective user ID=%s \
        real group ID=%s \
        effective group ID=%s """ \
        % (retCode, os.getpid(), os.getppid(), os.getpgrp(), os.getsid(0), \
           os.getuid(), os.geteuid(), os.getgid(), os.getegid())
        logging.debug(ProcParams)
        os.chdir(utils.semFSInstalledDirectory)
    except:
        logging.info("Error while making the process as Daemon")

def registerPlugins():
    """Read the plugins directory for plugins and import them"""
    
    try:
        pluginHandle = {}
        logging.info("starting the query processor")
        cursor = queryProcessor.start (utils.pluginDirectoryPath)
        logging.debug(queryProcessor.pluginHandler)
        for plugin in queryProcessor.pluginHandler.values():
            pluginHandle[plugin] = getattr( __import__('.'.join(utils.pluginDirectoryPath.split('/')) + ".%s"%(plugin)), plugin)
            logging.debug(pluginHandle)
            metaDataStructure = pluginHandle[plugin].getMetaDataList()
            logging.debug(metaDataStructure)
            if queryProcessor.checkTable(cursor, plugin):
                logging.info("Already  table for %s available"%(plugin))
            elif queryProcessor.createPluginTable(cursor, metaDataStructure, plugin):
                logging.info("Created a table for the plugin")
            else:
                logging.error("Unable to create the table")
    except:    
        logging.exception("Query Processor returned an exception")
        sys.exit(False)
    return cursor, pluginHandle

def writeRequest(event, mountPoint, mainTableIndex, fsDriver):
    """Get MetaData from the file and return parent Inode"""
    inoNode = xmlParser.get_node(event,'ino')
    ino = xmlParser.get_text(inoNode[0],'/')
    pno, file = queryProcessor.getpno(cursor, ino, mainTableIndex)
    fullFilePath = mountPoint + file
    type = getattr (__import__('.'.join(utils.pluginDirectoryPath.split('/')) + ".type"), 'type')
    extension = type.guess(file)
    logging.debug("Updating info of %s as an %s file"%(fullFilePath, extension))
    Existence = queryProcessor.checkExistence(cursor, extension, ino)
    try:
        #Should update main table and insert into plugin table.
        values = pluginHandle[extension].getMetaData(fullFilePath)
        logging.debug(values)
        properties = ""
        for data in os.stat(fullFilePath):
            properties += str(data) + ', '
        properties = properties[:-2]
        logging.debug(properties)
        vmain = queryProcessor.updateMainTable(cursor, mainTableIndex, file, ino, pno, properties, extension)
        logging.info("Main Table updated")
        if Existence:
            vplug = queryProcessor.updateIntoPluginTable(cursor, values, extension, file, ino)
        else:
            vplug = queryProcessor.insertIntoPluginTable(cursor, values, extension, file, ino)
        
        dataReturn = node.createElement("semFS")
        response = node.createElement('response')
        dataReturn.appendChild(response)
        pnoNode = node.createElement('pno')
        pnoValue = node.createTextNode(str(pno))
        pnoNode.appendChild(pnoValue)
        response.appendChild(pnoNode)
        
        if vmain and vplug:
            response.attributes['return']="0"
            utils.socketListener.write(fsDriver, dataReturn.toxml())
            logging.info("write successfull")
        else:
            response.attributes['return']="1"
            utils.socketListener.write(fsDriver, dataReturn.toxml())
        
    except:
        logging.exception("Error while updating the Index")

def unlinkRequest(event, mountPoint, mainTableIndex, fsDriver):
    """Delete the Row from the tables"""
    inoNode = xmlParser.get_node(event,'ino')
    ino = xmlParser.get_text(inoNode[0],'/')
    fileNode = xmlParser.get_node(event,'file')
    file = xmlParser.get_text(fileNode[0],'/')
    fullFilePath = mountPoint + file
    type = getattr (__import__('.'.join(utils.pluginDirectoryPath.split('/')) + ".type"), 'type')
    extension = type.guess(file)
    logging.debug("Removing info for %s of type %s "%(fullFilePath, extension))
    try:
        #Should update main table and insert into plugin table.
        vmain = queryProcessor.deleteFromMainTable(cursor, mainTableIndex, ino)
        logging.info("Removed from Main Table")
        vplug = queryProcessor.deleteFromPluginTable(cursor, extension, ino)
        if vmain and vplug:
            utils.socketListener.write(fsDriver, success.toxml())
            logging.info("Remove successfull")
        else:
            utils.socketListener.write(fsDriver, failure.toxml())
    except:
        logging.exception("Error while removing from the Index")

def createRequest(event, mainTableIndex, fsDriver):
    """Get MetaData from the file and return Sucess"""
    fileNode = xmlParser.get_node(event,'file')
    file = xmlParser.get_text(fileNode[0],'/')
    inoNode = xmlParser.get_node(event,'ino')
    ino = xmlParser.get_text(inoNode[0],'/')
    pnoNode = xmlParser.get_node(event,'pno')
    pno = xmlParser.get_text(pnoNode[0],'/')
    logging.debug("Updating info of %s,%s,%s in Main Table"%(file,ino,pno))
    #The file will not have metadata while create is called.
    try:
        properties = "'','','','','','',''"
        if queryProcessor.insertIntoMainTable(cursor, mainTableIndex, file, ino, pno, properties):
            utils.socketListener.write(fsDriver, success.toxml())
            logging.info("create successfull")
        else:
            utils.socketListener.write(fsDriver, failure.toxml())
    except:
        logging.exception("Error while adding an entry to the Main Index")

def queryRequest(event, mainTableIndex, id, fsDriver):
 
    try:
        query = xmlParser.selectQuery(event, mainTableIndex)
        data = queryProcessor.findResult(cursor, query, id)
        utils.socketListener.write(fsDriver, data)
        logging.info("Response sent to driver")
    except:
        logging.info("Cannot parse or execute the query")

def startRequest(event, fsDriver):
 
    uuid = event.getAttribute('uid')
    mainTableIndex = md5.md5(uuid).hexdigest()
    mountPoint = event.getAttribute('mountPoint')
    logging.debug('%s mounted at %s'%(str(mainTableIndex), str(mountPoint)))
    try:
        if queryProcessor.checkTable(cursor, mainTableIndex):
            utils.socketListener.write(fsDriver, success.toxml())
        elif queryProcessor.createMainTable(cursor, mainTableIndex):
            utils.socketListener.write(fsDriver, success.toxml())
        else:
            utils.socketListener.write(fsDriver, failure.toxml())
    except:
        logging.exception("Unable to create the main table")
    logging.info("Connection Created")
    return mainTableIndex,mountPoint

def readAndProcessDriverRequest(fsDriver, shutdown, connection):
    """Handle Requests that come to the Daemon"""
    
    data = utils.socketListener.read(fsDriver, utils.maXmlFileLength)
    logging.debug(data)
    head = xmlParser.parse(data)
    events = xmlParser.get_node(head, 'event')
    global mainTableIndex, mountPoint, id
    for event in events:
        try:
            action = event.getAttribute('action')
            id = event.getAttribute('id')
            if id:
                trueResponse.attributes['id'] = id
                failedResponse.attributes['id'] = id
            logging.debug("action :%s, connection :%s" %(action, connection))
            if action == 'write' and connection:
                writeRequest(event, mountPoint, mainTableIndex, fsDriver)
            elif action == 'create' and connection:
                createRequest(event, mainTableIndex, fsDriver)
            elif action == 'unlink' and connection:
                unlinkRequest(event, mountPoint, mainTableIndex, fsDriver)
            elif action == 'query' and connection:
                queryRequest(event, mainTableIndex, id, fsDriver)
            elif action == 'start':
                mainTableIndex, mountPoint = startRequest(event, fsDriver)
                connection = True
            elif action == 'close' and connection:
                shutdown = True
                logging.info("Close signal recieved")
            else:
                logging.error("Unkown Action")
        except:
            logging.exception("Exception while parsing the XML")
        
        return shutdown, connection

def driverRoutine():

    while True:
        try:
            fsDriver, Address = fsSocket.accept()
            logging.debug('Connection accepted for %s at %s'%((Address, fsDriver)))                    
            if fsDriver != None:
                logging.debug('Received connection: %s'%(fsDriver))
                shutdown = False
                connection = False
                while True:
                    shutdown, connection = readAndProcessDriverRequest(fsDriver, shutdown, connection)
                    if shutdown:
                        break
        except:
            logging.debug('Closed connection for %s at %s'%(Address, fsDriver))
            fsDriver.close()


#Default Values
node = Document()
success = node.createElement("semFS")
trueResponse = node.createElement('response')
trueResponse.attributes['return']="0"
success.appendChild(trueResponse)
failure = node.createElement("semFS")
failedResponse = node.createElement('response')
failedResponse.attributes['return']="1"
failure.appendChild(failedResponse)

#Daemon Code

makeThisProcessAsDaemon()
cursor, pluginHandle = registerPlugins()
logging.info("Starting the socket Listener")
fsSocket = utils.socketListener.start(utils.host, utils.port, utils.maxMountPoints)
logging.info("Started the socket Listener")
driverRoutine()

logging.info("Closing")
logging.info("-------")
sys.exit(True)