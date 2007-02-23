#!/usr/bin/python
import os
import sys
import time
import logging
import apsw
from xml.dom.minidom import Document

pluginHandler = {}

def checkTable(cursor, tableName):
    """ Check if the table already exists
    """
    query = "SELECT name FROM sqlite_master WHERE type = 'table'"
    for name in cursor.execute(query):
        if name[0] == tableName:
            logging.info("Table:%s already exist"%(tableName))
            return True
    logging.info("Table:%s does not exists"%(tableName))
    return False

def checkExistence(cursor, tableName, ino):
    """To check if an entry exists in the plugins table"""
    query = "select INODE from %s where INODE = %s;"%(tableName, ino)
    for INODE in cursor.execute(query):
        if INODE:
            logging.info("Something exists in table %s"%(ino))
            return True

    logging.info("Hoping that nothing is returned")
    return False

def getpno(cursor, ino, mainTableIndex):
    """Return the parent inode and filename  given the file inode"""
    query = "select PARENTINODE,FILENAME from %s where INODE = %s"%(mainTableIndex, ino)
    logging.info(query)
    for PARENTINODE, FILENAME in cursor.execute(query):
        logging.info('%s,%s are the ino and filename of %s'%(PARENTINODE, FILENAME, ino))
    
    return PARENTINODE, FILENAME

def getModuleName(path):
    """This function is used to read the module name from the given file
    and return it.
    
    INPUT: The file name along with its path
    OUTPUT: It returns the module name of that plugin."""
    
    file = open(path)
    logging.info("Reading the plugin contents to get the module name")
    lines = file.readlines()
    for line in lines:
        decl = line.split('=')[0].strip().lower()
        if decl == 'module':
            return line.split('=')[1].strip()
    return False

def start (pluginDirectoryPath):
    """Starts the query Processor of the Daemon. It creates/opens the
    SemFSIndex (database), scans the plugin folder and registers the 
    plugins that are present in it.
    
    INPUT: The directory where the plugins are placed.
    OUTPUT: It returns the cursor for future querying the index table."""
    #Check we have the expected version of apsw and sqlite """    
    logging.debug(" Using APSW file " + apsw.__file__)             
    # from the extension module
    logging.debug(" APSW version " + apsw.apswversion())           
    # from the extension module
    logging.debug(" SQLite version" + apsw.sqlitelibversion())     
    # from the sqlite library code
        
    """Opening/creating database"""
    try:
        if os.path.exists("SemFSIndex"): 
            os.remove("SemFSIndex")
        logging.debug(os.getenv ('PWD'))
        connection = apsw.Connection("SemFSIndex")
        logging.info("Connecting to the SemFS Indexing Table")
        cursor=connection.cursor()            # Creating the connection.
        logging.info("Connected")
    except:
        logging.exception("Error while contecting to the SemFS database")

    """Registering the plugins are done here"""
    global pluginHandler
    dirList = os.listdir(pluginDirectoryPath)
    for fileName in dirList:
        if fileName.endswith('.semFS-plugin'):
            logging.debug("Found a plugin -> " + fileName)
            #pluginHandler.append(fileName)
            module = getModuleName(pluginDirectoryPath + "/" + fileName)
            if module:
                pluginHandler.__setitem__(fileName,module)
                logging.info("Plugin Registered...")
            else:
                logging.error("Module name not specified in the plugin -> " \
                              + fileName)
                return False
    return cursor

def findResult(cursor, query, id):
    """Returns the filenames that matches with the current 
    requirements from the index."""    
    try:
        node = Document()
        resultSet = node.createElement('semFS')
        response = node.createElement('response')
        response.attributes['id'] = str(id)
        resultSet.appendChild(response)
        logging.info("executing the query")
        for INODE in cursor.execute(query):
            ino = node.createElement('ino')
            inoValue = node.createTextNode(str(INODE[0]))
            ino.appendChild(inoValue)
            response.appendChild(ino)
        logging.info("query Result :%s"%(resultSet.toxml()))
        return resultSet.toxml()
    except:
        logging.exception("Unable to execute or return the query result")
        return 'Failed'

def createMainTable(cursor,name):
    """Creates the main table with common metadata
    ST_MODE
        Inode protection mode.
    ST_INO
        Inode number.
    ST_DEV
        Device inode resides on.
    ST_NLINK
        Number of links to the inode.
    ST_UID
        User id of the owner.
    ST_GID
        Group id of the owner.
    ST_SIZE
        Size in bytes of a plain file; amount of data waiting on some special files.
    ST_ATIME
        Time of last access.
    ST_MTIME
        Time of last modification.
    ST_CTIME
        Time of last status change (see manual pages for details). """
    try:
        cursor.execute("""create table %s (FILENAME STRING, \
                                           INODE INTEGER PRIMARY KEY, \
                                           PARENTINODE INTEGER, \
                                           MODE INTEGER, \
                                           USERID INTEGER, \
                                           GROUPID INTEGER, \
                                           SIZE INTEGER, \
                                           ATIME INTEGER, \
                                           MTIME INTEGER, \
                                           CTIME INTEGER, \
                                           TYPE STRING);"""%(name))
        logging.info("Main table Index created with name %s"%(name))
        return True
    except:
        logging.exception("Cannot create the Main table")
        return False

def insertIntoMainTable(cursor, mainTableIndex, file, ino, pno, properties):
    """Add and entry to main table with type None"""
    try:
        query = "insert into %s values('%s',%d,%d,%s,'None');"%(mainTableIndex,file,int(ino),int(pno),properties)
        logging.debug(query)
        cursor.execute(query)
        logging.info("Inserted into main table")
        return True
    except:
        logging.exception("While writing to main table")
        return False

def updateMainTable(cursor, mainTableIndex, file, ino, pno, properties, type):
    """Update the Main table based on inode value"""
    try:
        d = properties.split(',')
        query = "update %s set FILENAME = '%s', PARENTINODE = %s, MODE = %s, USERID = %s, GROUPID = %s, SIZE = %s, ATIME = %s, MTIME = %s, CTIME = %s, TYPE = '%s' where INODE = %s;"\
                %(mainTableIndex, file, pno, d[0], d[1], d[2], d[3], d[4], d[5], d[6], type, ino)
        logging.debug(query)
        cursor.execute(query)
        logging.info("Inserted into main table")
        return True
    except:
        logging.exception("While writing to main table")
        return False

def deleteFromMainTable(cursor, mainTableIndex, ino):
    """Delete an entry from main table"""
    query = "delete from %s where INODE = %s"%(mainTableIndex,ino)
    logging.debug(query)
    try:
        cursor.execute(query)
        logging.info("Delete Successfull")
        return True
    except:
        logging.exception("While removing an entry from the Index")
        
def createPluginTable(cursor, metaDataStructure, plugin):
    """ First creates the tables that contain the metadata. This should also 
    query for the required meta data for their file type.
  
    INPUT: The sqlite cursor, The meta data headers and their 
    datatype in a dict.
    Output: constructs sql query and return the resultset"""
    param = ""
    for i in metaDataStructure:
        param += i + " " + metaDataStructure[i] + ","
    query = """create table %s (INODE INTEGER PRIMARY KEY, %s);""" \
    %(plugin,param[:-1])
    logging.debug(query)
    try:
        resultSet = cursor.execute(query)
        return resultSet
    except:
        logging.exception("Unable to create the table")
        return False

def insertIntoPluginTable(cursor, values, extension, filename, ino):
    """ Inserts a row if a new file or folder has been created.
    INPUT: {'artist':'rahman','playtime':'5'}
    OUTPUT:
    insert into <extension> values (artist='rahman',playtime='5') 
    """
    logging.debug(values)
    logging.debug(len(values))
    syntax=""
    for key in values:
        syntax += "'" + values[key] + "'" + ','
    syntax = syntax[:-1]
    query = "insert into %s values ('%s',%s);"%(extension, ino, syntax)
    logging.debug(query)
    try:
        cursor.execute(query)
        logging.info("Insert successfull")
        return True
    except:
        logging.exception("While Inserting the Index")
        return False

def updateIntoPluginTable(cursor, values, extension, file, ino):
    """Updates the values in the plugin table"""   
    syntax=""
    for key in values:
        syntax += key + ' = ' + "'" + values[key] + "'" + ','
    syntax = syntax[:-1]
    query = "update %s set %s where INODE = '%s';"%(extension,syntax,ino)
    logging.debug(query)
    try:
        cursor.execute(query)
        logging.info("Update plugin table successfull")
        return True
    except:
        logging.exception("While updateing the Index")
        return False
    
def deleteFromPluginTable(cursor, extension, ino):
    """Delete an entry from plugins table"""
    query = "delete from %s where INODE = %s"%(extension,ino)
    logging.debug(query)
    try:
        cursor.execute(query)
        logging.info("Delete Successfull")
        return True
    except:
        logging.exception("While removing an entry from the Index")
