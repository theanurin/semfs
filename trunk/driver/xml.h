
#ifndef   	XML_H_
# define   	XML_H_
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include "semfs.h"

int start_xml_document(xmlTextWriterPtr *writer, xmlBufferPtr *buf);
char *close_xml_document(xmlTextWriterPtr *writer, xmlBufferPtr *buf);
int connection_event(char *uuid, char *mnt_point, char *event);
int notify_event(char *event, semfs_ino_t parent, semfs_ino_t child);
int notify_inode_event(char *event, semfs_ino_t ino);

#define MY_ENCODING "ISO-8859-1"

#endif 	    /* !XML_H_ */
