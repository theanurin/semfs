#!/usr/bin/python
import os
import sys

def getMetaData(filename):
    """ Return the metadata corresponding to the file.
    INPUT : Out put of mminfo execution
    OutPUT: Dictionary of Metadata of mp3 files
    """
    os.system('mminfo %s > plugininfo' %(filename))
    pl = open('plugininfo', 'r').readlines()
    values = {'artist':'', 'title':'',     
              'trackno':'','album':'', 
              'version':'', 'layer':'',
              'protection':'', 'bitrate':'',
              'samplerate':'', 'padding':'', 
              'private':'', 'mode':'', 
              'mode_extension':'', 'copyright':'', 
              'original':'', 'emphasis':'', 
              'length':''}
    for i in pl:
        if i != '\n':
             values["%s"%(i.split(':')[0].strip())]=i.split(':')[1][:-1].strip()
    return values

def getMetaDataList():
   """ Returns the Metadata along with its datatype in order to create the 
   plugin table.
   INPUT : Out put of mminfo execution
   OUTPUT: List of Metadata of mp3 files
   """
   mp3Metadata = {}
   mp3Metadata['artist'] = 'STRING'
   mp3Metadata['title'] = 'STRING'
   mp3Metadata['trackno'] = 'INTEGER'
   mp3Metadata['album'] = 'STRING'
   mp3Metadata['version'] = 'INTEGER'
   mp3Metadata['layer'] = 'STRING'
   mp3Metadata['protection'] = 'STRING'
   mp3Metadata['bitrate'] = 'INTEGER'
   mp3Metadata['samplerate'] = 'INTEGER'
   mp3Metadata['padding'] = 'STRING'
   mp3Metadata['private'] = 'STRING'
   mp3Metadata['mode'] = 'STRING'
   mp3Metadata['mode_extension'] = 'STRING'
   mp3Metadata['copyright'] = 'STRING'
   mp3Metadata['original'] = 'STRING'
   mp3Metadata['emphasis'] = 'STRING'
   mp3Metadata['length'] = 'INTEGER'
     
   return mp3Metadata
