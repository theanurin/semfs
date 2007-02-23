#!/usr/bin/python
import sys
import xml.dom.minidom
import urllib
import re
import time 
import logging

def get_text(xml_node, path):
    """Used to get the node value of an xml tag.
    INPUT: The xmlNode, the path. The path is given as /tag1/tag2
    OUTPUt: Gives back the list of nodes with the particular match."""
    nodes = get_node(xml_node, path)
    if type(nodes) != type([]):
        nodes = [nodes]
    ret = []
    for n in nodes:
        if type(n) in [type(u""), type("")]:
            ret.append(n)
        else:
            if not n.childNodes:
                ret.append(u"")
            elif n.childNodes[0].nodeType == n.TEXT_NODE:
                ret.append(n.childNodes[0].data)
    if len(ret) == 1:
        return ret[0]
    else:
        return ret

def get_node(xml_node, path):
    """Used to get the node of an xml tag.
    INPUT: The xmlNode, the path. The path is given as /tag1/tag2
    OUTPUt: Gives back the list of nodes with the particular match."""
    
    def get_from_node(n, path_elements):
        if len(path_elements) == 1:
            x = n.getElementsByTagName(path_elements[0])
            if x:
                return x
            else:
                x = n.getAttribute(path_elements[0])
                return x
        else:
            l = n.getElementsByTagName(path_elements[0])
            ret = []
            for x in l:
                ret.append(get_from_node(x, path_elements[1:]))
            if ret and len(ret) == 1:
                return ret[0]
            else:
                return ret

    if not path: return xml_node
    if path == "/": return xml_node
    
    parts = path.split("/")
    l = get_from_node(xml_node, parts)
    #if len(l) == 1:
    #    return l[0]
    #else:
    return l

def parse(str):
    return xml.dom.minidom.parseString(str)

def parseFile(path):
    return xml.dom.minidom.parse(path)

def selectQuery(event, mainTableIndex):
    """Returns the SemFs query after parsing the xml file

    TODO: Minimize the restrictions
          Also make sure the query searches in Main table also
          
    Defaults
    Type must!!! and (dont enclose it in quotes.
    Time in Seconds
    Size in Kb
    operators: and, or, (, ), =, <>, <=, >=
    '#' denotes its a special metadata
    '$' denotes is a normal property
       
    INPUT:
        <event action="query">
            <id>5000</id>
            <args>type = mp3, (#artist = 'rahman' or #playtime > 5) 
            and $size >=4 </args>
        </event>
    OUTPUT:"select filename from mp3,uuid where 
    (Main.filename = mp3.filename and mp3.artist = 'rahman' 
    or mp3.playtime > 5) and uuid.size >= 4;" 
    """
    try:
        args = get_node(event,'args')
        semFSQuery = get_text(args[0],'/')
        logging.debug(semFSQuery)
        type = semFSQuery.split(',')[0].split('=')[1].strip()
        whereClause = semFSQuery.split(',')[1]
        query = "select %s.INODE from %s,%s where %s.INODE = %s.INODE and %s;"%\
        (mainTableIndex, type, mainTableIndex, type, mainTableIndex, semFSQuery.split(',')[1].\
         replace('#',type + '.').replace('$',mainTableIndex))
        logging.debug(query)
        return query
    except:
        logging.exception("Syntax Error in the Query")
        return False