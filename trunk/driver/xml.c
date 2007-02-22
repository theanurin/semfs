#include "xml.h"

extern struct semfs_s semfs;
static int id_generator()
{
     static int rand = 0;
     if (rand == 0)
          rand = random();
     return rand++;
}

static void send_to_daemon(char *msg)
{
     int bytes = 0, i;
    char recv_msg[256];
     while (1) {
          bytes = send(semfs.sock, msg, strlen(msg), 0);
          if (bytes >= strlen(msg))
               break;
          msg += bytes;
     }
     bytes = recv(semfs.sock, recv_msg, 256, 0);
     if (bytes > 0) {
          printf ("Received XML document:\n");
          for (i=0; i<bytes; i++)
               printf ("%c", recv_msg[i]);
          printf ("\n");
     }
}

static xmlChar *ConvertInput(const char *in, const char *encoding)
{
     xmlChar *out;
     int ret;
     int size;
     int out_size;
     int temp;
     xmlCharEncodingHandlerPtr handler;
     LIBXML_TEST_VERSION;
     if (in == 0)
          return 0;

     handler = xmlFindCharEncodingHandler(encoding);

     if (!handler) {
          printf("ConvertInput: no encoding handler found for '%s'\n",
                 encoding ? encoding : "");
          return 0;
     }

     size = (int) strlen(in) + 1;
     out_size = size * 2 - 1;
     out = (unsigned char *) xmlMalloc((size_t) out_size);

     if (out != 0) {
          temp = size - 1;
          ret = handler->input(out, &out_size, (const xmlChar *) in, &temp);
          if ((ret < 0) || (temp - size + 1)) {
               if (ret < 0) {
                    printf("ConvertInput: conversion wasn't successful.\n");
               }
               else {
                    printf ("ConvertInput: conversion wasn't successful. converted: %i octets.\n",
                            temp);
               }

               xmlFree(out);
               out = 0;
          }
          else {
               out = (unsigned char *) xmlRealloc(out, out_size + 1);
               out[out_size] = 0;  /*null terminating out */
          }
     }
     else
          printf("ConvertInput: no mem\n");
     return out;
}

int start_xml_document(xmlTextWriterPtr *writer, xmlBufferPtr *buf)
{
     int rc;
     *buf = xmlBufferCreate();
     if (*buf == NULL) {
          fprintf(stderr, "start_xml_document: Error creating the xml buffer\n");
          return 0;
     }
     *writer = xmlNewTextWriterMemory(*buf, 0);
     if (*writer == NULL) {
          fprintf(stderr, "start_xml_document: Error creating the xml writer\n");
          return 0;
     }
     rc = xmlTextWriterStartDocument(*writer, NULL, MY_ENCODING, NULL);
     if (rc < 0) {
          fprintf (stderr, "start_xml_document: Error at xmlTextWriterStartDocument\n");
          return 0;
     }
     rc = xmlTextWriterStartElement(*writer, BAD_CAST "semfs");
     if (rc < 0) {
          fprintf (stderr, "start_xml_document: Error at xmlTextWriterStartElement\n");
          return 0;
     }     
     rc = xmlTextWriterStartElement(*writer, BAD_CAST "event");
     if (rc < 0) {
          fprintf (stderr, "start_xml_document: Error at xmlTextWriterStartElement\n");
          return 0;
     }
     return 1;
}

char *close_xml_document(xmlTextWriterPtr *writer, xmlBufferPtr *buf)
{
     char *ret;
     int rc;
     rc = xmlTextWriterEndElement(*writer);
     if (rc < 0) {
          fprintf (stderr, "close_xml_document: Error at xmlTextWriterEndElement\n");
          return NULL;
     }
     rc = xmlTextWriterEndDocument(*writer);
     if (rc < 0) {
          fprintf (stderr, "close_xml_document: Error at xmlTextWriterEndDocument\n");
          return NULL;
     }
     xmlFreeTextWriter(*writer);
     ret = strdup((*buf)->content);
     xmlBufferFree(*buf);
     return ret;
}

int connection_event(char *uuid, char *mnt_point, char *event)
{
     int rc;
     xmlTextWriterPtr writer;
     xmlBufferPtr buf;
     xmlChar *tmp;
     char *xml_doc;
     if (!start_xml_document(&writer, &buf))
          return 0;
    
     tmp = ConvertInput(uuid, MY_ENCODING);
     printf ("UUID: %s\n", tmp);
     rc = xmlTextWriterWriteAttribute(writer, BAD_CAST "uid",
                                      BAD_CAST tmp);
     if (rc < 0) {
          fprintf (stderr, "connection: Error at xmlTextWriterWriteAttribute\n");
          return 0;
     }
     if (tmp) xmlFree(tmp);
     tmp = ConvertInput(mnt_point, MY_ENCODING);
     rc = xmlTextWriterWriteAttribute(writer, BAD_CAST "mountpoint",
                                      BAD_CAST tmp);
     fprintf (stderr, "Mount-Point: %s\t%s\n", tmp, mnt_point);
     if (rc < 0) {
          printf ("connection: Error at xmlTextWriterWriteAttribute\n");
          return 0;
     }
     if (tmp) xmlFree(tmp);
     tmp = ConvertInput(event, MY_ENCODING);
     rc = xmlTextWriterWriteAttribute(writer, BAD_CAST "action",
                                      BAD_CAST tmp);
     if (rc < 0) {
          fprintf (stderr, "connection: Error at xmlTextWriterWriteAttribute\n");
          return 0;
     }
     if (tmp) xmlFree(tmp);
     xml_doc = close_xml_document(&writer, &buf);
     fprintf (stderr, "XML DOCUMENT:\n%s", xml_doc);
     send_to_daemon(xml_doc);
     free(xml_doc);
     return 1;
}

int notify_event(char *event, semfs_ino_t parent, semfs_ino_t child)
{
     int rc;
     xmlTextWriterPtr writer;
     xmlBufferPtr buf;
     xmlChar *tmp;
     char **path = (char **)malloc(255*sizeof(char));
     char *xml_doc;
     fprintf (stderr, "notify_event: %s\t%u\t%u\n", event, parent, child);
     if (!start_xml_document(&writer, &buf))
          return 0;
     if (xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "id",
                                           "%d", id_generator()) < 0) {
          fprintf (stderr, "Could not create attribute1\n");
          close_xml_document(&writer, &buf);
          return 0;
     }
     tmp = ConvertInput(event, MY_ENCODING);
     if (xmlTextWriterWriteAttribute(writer, BAD_CAST "action",
                                     BAD_CAST event) < 0) {
          fprintf (stderr, "Could not create attribute2\n");
          close_xml_document(&writer, &buf);
          return 0;
     }
     if (tmp) xmlFree(tmp);
     if (ext2fs_get_pathname (semfs.fs, parent, child, path)) {
          fprintf (stderr, "Could not get path name\n");
          close_xml_document(&writer, &buf);
          return 0;
     }
     printf("PATH: %s\n", *path);    
     tmp = ConvertInput(*path, MY_ENCODING);
     if (xmlTextWriterWriteElement(writer, BAD_CAST "file",
                                   BAD_CAST tmp) < 0 ||
         xmlTextWriterWriteFormatElement(writer, BAD_CAST "ino",
                                         "%u", child) < 0 ||
         xmlTextWriterWriteFormatElement(writer, BAD_CAST "pno",
                                         "%u", parent) < 0) {
          free(*path);
          if (tmp) xmlFree(tmp);
          close_xml_document(&writer, &buf);
          return 0;
     }
     xml_doc = close_xml_document(&writer, &buf);
     fprintf (stderr, "XML DOCUMENT:\n%s", xml_doc);
     send_to_daemon(xml_doc);
     free(xml_doc);
     free(*path);
     if (tmp) xmlFree(tmp);
     return 1;
}

int notify_inode_event(char *event, semfs_ino_t ino)
{
     int rc;
     xmlTextWriterPtr writer;
     xmlBufferPtr buf;
     xmlChar *tmp;
     char *xml_doc;
     if (!start_xml_document(&writer, &buf))
          return 0;
     if (xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "id",
                                           "%d", id_generator()) < 0) {
          fprintf (stderr, "Could not create attribute1\n");
          close_xml_document(&writer, &buf);
          return 0;
     }
     tmp = ConvertInput(event, MY_ENCODING);
     if (xmlTextWriterWriteAttribute(writer, BAD_CAST "action",
                                     BAD_CAST tmp) < 0) {
          fprintf (stderr, "Could not create attribute2\n");
          close_xml_document(&writer, &buf);
          return 0;
     }
     if (tmp) xmlFree(tmp);
     if (xmlTextWriterWriteFormatElement(writer, BAD_CAST "ino",
                                         "%u", ino) < 0) {
          close_xml_document(&writer, &buf);
          return 0;
     }
     xml_doc = close_xml_document(&writer, &buf);
     fprintf (stderr, "XML DOCUMENT:\n%s", xml_doc);
     send_to_daemon(xml_doc);
     free(xml_doc);
     return 1;
}
     
          
     
