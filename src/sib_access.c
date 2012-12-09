/*

  Copyright (c) 2009, Nokia Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.  
    * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.  
    * Neither the name of Nokia nor the names of its contributors 
    may be used to endorse or promote products derived from this 
    software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */
/*
 * WhiteBoard SIBAccess component
 *
 * sib_access.c
 *
 * Copyright 2007 Nokia Corporation
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>
#include <whiteboard_log.h>

#include <sibmsg.h>

#include "sib_controller.h"

#define BUF_SIZE 8192
//gchar recvbuf[BUF_SIZE];

#define ENDTAG "</SSAP_message>"
#define ENDTAGLEN 15

typedef struct _SubData
{
  int s;    // socket
  gchar *recvbuf;
  gint len; // lenth of received message
  gint remaining_len; // unhandled bytes
} SubData;

struct _SIBAccess
{
  SIBController* cp;

  ssElement_ct uri;
  
  gchar* ip_address;
  gint port;

  //  int sockfd;
  
  /* subscription id -> SubData */
  GHashTable *subs_sockfd_map;

  
  gint refcount;
};

/*****************************************************************************
 * Private utilities
 *****************************************************************************/

static gint sib_access_command(int s, gchar *msg, gint len, NodeMsgContent_t *response);
static gint sib_access_subscribe_command(int s, gchar *msg, gint len, NodeMsgContent_t *response);

static gint sib_access_send_command(int s, gchar *msg, gint len);

static gint sib_access_send_message(int s, gchar *buf, gint len);
static gint sib_access_receive_message(SubData *sdata, NodeMsgContent_t *msgContent);

static int sib_access_get_and_connect_socket(SIBAccess *sa);
static ssBufDesc_t *sib_access_create_join_message(ssElement_ct ssId,
					     //apr09obsolete const gchar *username,
					     ssElement_ct nodeName,
					     gint msgnumber
					     //apr09obsolete , const guchar *node_pk,
					     //apr09obsolete ssElement_ct accessGroup_req
					     );
static ssBufDesc_t *sib_access_create_leave_message(ssElement_ct sibid, ssElement_ct nodeid,
					      gint msgnumber);
static ssBufDesc_t *sib_access_create_insert_message(ssElement_ct sibid,
					       ssElement_ct nodeid,
					       gint msgnumber,
					       EncodingType encoding,
					       guchar *request);
static ssBufDesc_t *sib_access_create_update_message(ssElement_ct sibid, ssElement_ct nodeid, gint msgnumber, EncodingType encoding, guchar *insert_request, guchar *remove_request);
static ssBufDesc_t *sib_access_create_remove_message(ssElement_ct sibid, ssElement_ct nodeid,
					       gint msgnumber, EncodingType encoding, guchar *request);
static ssBufDesc_t *sib_access_create_query_message(ssElement_ct sibid, ssElement_ct nodeid,
					      gint msgnumber, gint type, guchar *request);
static ssBufDesc_t *sib_access_create_subscribe_message(ssElement_ct sibid, ssElement_ct nodeid,
					      gint msgnumber, gint type, guchar *request);
static ssBufDesc_t *sib_access_create_unsubscribe_message(ssElement_ct sibid, ssElement_ct nodeid,
					      gint msgnumber, guchar *request);

static gboolean sib_access_add_subscription_socket(SIBAccess *sa, guchar *subscription_id, SubData *sdata);
static SubData *sib_access_get_subscription_socket(SIBAccess *sa, guchar *subscription_id);
static gboolean sib_access_remove_subscription_socket(SIBAccess *sa, guchar *subscription_id);
/**
 * frees SubData structure (does not close the socket);
 *
 **/
static void sub_data_free(gpointer data);

/**
 * frees SubData structure (and closes the socket);
 *
 **/
static void sub_data_free_close(gpointer data);


static SubData *sub_data_new(int s);
/*****************************************************************************
 * Construction/destruction
 *****************************************************************************/

SIBAccess* sib_access_new(SIBController* cp, guchar *uri, gchar *ip, gint port)
{
  SIBAccess* self = NULL;

  whiteboard_log_debug_fb();

  g_return_val_if_fail(cp != NULL, NULL);
  g_return_val_if_fail(ip != NULL, NULL);

  self = g_new0(SIBAccess, 1);
  g_return_val_if_fail(self != NULL, NULL);

  self->cp = cp;
  self->uri=(ssElement_ct)g_strdup( (gchar *)uri);
  self->ip_address = g_strdup(ip);
  self->port = port;
  self->refcount=1;

  //  self->sockfd = -1;
  self->subs_sockfd_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, sub_data_free_close);
  whiteboard_log_debug_fe();
  return self;
}


/**
 * Destroy a SIBAccess struct
 *
 * @param sa The SIBAccess struct to destroy
 * @return FALSE for error, TRUE for success.
 */
gboolean sib_access_destroy(SIBAccess* sa)
{
  whiteboard_log_debug_fb();
  
  g_return_val_if_fail(sa != NULL, FALSE);
  /* 	g_return_val_if_fail(server->mutex != NULL, FALSE); */
  

    /* Free the URI string */
  if (sa->uri)
    g_free((char *)sa->uri);
  sa->uri = NULL;
  

  /* Free the IP string */
  if (sa->ip_address)
    g_free(sa->ip_address);
  sa->ip_address = NULL;

  g_hash_table_destroy(sa->subs_sockfd_map);
  
  /*Finally destroy self */
  g_free(sa);
  whiteboard_log_debug_fe();
  
  return TRUE;
}

/**
 * Increase the server's reference count
 *
 * @param server The server, whose reference count to increase
 */
void sib_access_ref(SIBAccess* self)
{
  whiteboard_log_debug_fb();

  g_return_if_fail(self != NULL);
  
  if (g_atomic_int_get(&self->refcount) > 0)
    {
      /* Refcount is something sensible */
      g_atomic_int_inc(&self->refcount);
      
      whiteboard_log_debug("SIBAccess refcount: %d\n",
			   self->refcount);
    }
  else
    {
      /* Refcount is already zero */
      whiteboard_log_debug("SIBAccess  refcount already %d!\n",
			   self->refcount);
    }
  
  whiteboard_log_debug_fe();
}

/**
 * Decrease the server's reference count. If the counter reaches zero,
 * the server is destroyed.
 *
 * @param server The server, whose reference count to increase
 */
void sib_access_unref(SIBAccess* self)
{
  whiteboard_log_debug_fb();
  
  g_return_if_fail(self != NULL);
  
  if (g_atomic_int_dec_and_test(&self->refcount) == FALSE)
    {
      whiteboard_log_debug("SIBAccess refcount dec: %d\n", 
			   self->refcount);
    }
  else
    {
      whiteboard_log_debug("SIBAccess refcount zeroed. Destroy.\n");
      sib_access_destroy(self);
    }
  
  whiteboard_log_debug_fe();
}

const gchar* sib_access_get_uri(SIBAccess* self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return (const gchar*) self->uri;
}

/**
 * Compare the given SIBServer pointer with the given UDN.
 *
 * @param server The SIBServer to compare with
 * @param udn The UDN to try and match
 * @return 0 if server and udn match, !0 if they don't match
 */
gint sib_access_compare_uri(gconstpointer sib, gconstpointer uri)
{
  SIBAccess* sa = (SIBAccess*) sib;
  
  g_return_val_if_fail(sa != NULL, 1);
  g_return_val_if_fail(uri != NULL, -1);
  
  return g_ascii_strcasecmp((char *)sa->uri, uri);
}

gint sib_access_join( SIBAccess *sa,
		      //apr09obsolete const gchar *username,
		      ssElement_ct nodeid,
		      gint msgnumber,
		      //apr09obsolete const guchar *node_pk,
		      //apr09obsolete ssUri_ct access_req,
		      NodeMsgContent_t *msgContent )
{
  gint retvalue = -1;
  gint rbytes=-1;
  int s;
  ssBufDesc_t *buf = NULL;
  gchar *sendmsg =NULL;
  gint sendmsgLen = -1;
  whiteboard_log_debug_fb();
  
  buf = sib_access_create_join_message( sa->uri,
					//apr09obsolete username,
					nodeid,
					msgnumber
					//apr09obsolete , node_pk,
					//apr09obsolete access_req
					);
  if( buf == NULL )
    {
      whiteboard_log_warning("Could not create Join message\n");
      whiteboard_log_debug_fe();
      return -1;
    }
  s = sib_access_get_and_connect_socket(sa);
  if(s<0)
    {
      whiteboard_log_warning("socket err\n");
      ssBufDesc_free(&buf);
      whiteboard_log_debug_fe();
      return -1;
    }
  sendmsg = ssBufDesc_GetMessage(buf);
  sendmsgLen = ssBufDesc_GetMessageLen(buf);
  rbytes = sib_access_command(s, sendmsg, sendmsgLen, msgContent);
  if(rbytes > 0)
    {
      if( g_ascii_strcasecmp((char *)nodeid, parseSSAPmsg_get_nodeid( msgContent) ) ||
	  g_ascii_strcasecmp((char *)sa->uri, parseSSAPmsg_get_spaceid( msgContent) ) ||
	  ( parseSSAPmsg_get_name(msgContent) != MSG_N_JOIN ) ||
	  ( parseSSAPmsg_get_type(msgContent) != MSG_T_CNF ) )
	{
	  whiteboard_log_debug("Not proper join conf. Receiver: %s(%s), Sender: %s(%s), Name: %d, Type: %d\n",
			       parseSSAPmsg_get_nodeid( msgContent),
			       nodeid,
			       parseSSAPmsg_get_spaceid( msgContent),
			       sa->uri,
			       parseSSAPmsg_get_name(msgContent),
			       parseSSAPmsg_get_type(msgContent) );
	  retvalue = -1;
	}
      else
	{
	  retvalue = 1;
	}
    }
  else
    {
      whiteboard_log_debug("Sending join command failed\n");
      retvalue = -1;
    }
  
  ssBufDesc_free(&buf);

  whiteboard_log_debug_fe();
  return retvalue;
}

gint sib_access_leave( SIBAccess *sa, ssElement_ct nodeid, gint msgnumber,
		       NodeMsgContent_t *msgContent )
{
  
  whiteboard_log_debug_fb();
  gint rbytes;
  gint retvalue = -1;
  int s;
  ssBufDesc_t *buf = NULL;
  gchar *sendmsg = NULL;
  gint sendmsgLen = -1;
  buf=sib_access_create_leave_message( sa->uri, nodeid, msgnumber );
  if(buf==NULL)
    {
      whiteboard_log_warning("Could not create Leave message\n");
      whiteboard_log_debug_fe();
      return -1;
    }
  s = sib_access_get_and_connect_socket(sa);
  if(s<0)
    {
      whiteboard_log_warning("socket err\n");
      ssBufDesc_free(&buf);
      whiteboard_log_debug_fe();
      return -1;
    }

  sendmsg = ssBufDesc_GetMessage(buf);
  sendmsgLen = ssBufDesc_GetMessageLen(buf);
  rbytes = sib_access_command(s, sendmsg, sendmsgLen, msgContent);
   if(rbytes > 0)
    {
      
      if( g_ascii_strcasecmp((char *)nodeid, parseSSAPmsg_get_nodeid( msgContent) ) ||
	  g_ascii_strcasecmp((char *)sa->uri, parseSSAPmsg_get_spaceid( msgContent) ) ||
	  ( parseSSAPmsg_get_name(msgContent) != MSG_N_LEAVE ) ||
	  ( parseSSAPmsg_get_type(msgContent) != MSG_T_CNF ) )
	{
	  whiteboard_log_debug("Not proper leave conf. Receiver: %s(%s), Sender: %s(%s), Name: %d, Type: %d\n",
			       parseSSAPmsg_get_nodeid( msgContent),
			       nodeid,
			       parseSSAPmsg_get_spaceid( msgContent),
			       sa->uri,
			       parseSSAPmsg_get_name(msgContent),
			       parseSSAPmsg_get_type(msgContent) );
	  retvalue = -1;
	}
      else
	{
	  retvalue = 1;
	}
    }
  else
    {
      whiteboard_log_debug("Sending leave command failed\n");
      retvalue = -1;
    }
   
   ssBufDesc_free(&buf);
  whiteboard_log_debug_fe();
  return retvalue;
}

gint sib_access_insert(SIBAccess *sa, ssElement_ct nodeid, gint msgnumber, EncodingType encoding, 
		       guchar *request, NodeMsgContent_t *msgContent)
{
  gint rbytes=0;
  gint retvalue = -1;
  ssBufDesc_t *buf = NULL;
  gchar *sendmsg = NULL;
  gint sendmsgLen = -1;
  int s;
  whiteboard_log_debug_fb();

  g_return_val_if_fail( NULL != sa, -1);
  g_return_val_if_fail( NULL != nodeid, -1 );
  g_return_val_if_fail( NULL != request, -1 );
  
  buf=sib_access_create_insert_message(sa->uri, nodeid, msgnumber, encoding, request);
  if(buf==NULL)
    {
      whiteboard_log_warning("Could not create INSERT message\n");
      whiteboard_log_debug_fe();
      return -1;
    }

  s = sib_access_get_and_connect_socket(sa);
  if(s<0)
    {
      whiteboard_log_warning("socket err\n");
      whiteboard_log_debug_fe();
      ssBufDesc_free(&buf);
      return -1;
    }
  sendmsg = ssBufDesc_GetMessage(buf);
  sendmsgLen = ssBufDesc_GetMessageLen(buf);
  rbytes  = sib_access_command(s, sendmsg, sendmsgLen, msgContent);
   
  if(rbytes > 0)
    {
      
      if( g_ascii_strcasecmp((char *)nodeid, parseSSAPmsg_get_nodeid( msgContent) ) ||
	  g_ascii_strcasecmp((char *)sa->uri, parseSSAPmsg_get_spaceid( msgContent) ) ||
	  ( parseSSAPmsg_get_name(msgContent) != MSG_N_INSERT ) ||
	  ( parseSSAPmsg_get_type(msgContent) != MSG_T_CNF ) )
	{
	  whiteboard_log_debug("Not proper insert conf. Receiver: %s, Sender: %s, Name: %d, Type: %d\n",
			       parseSSAPmsg_get_nodeid( msgContent),
			       parseSSAPmsg_get_spaceid( msgContent),
			       parseSSAPmsg_get_name(msgContent),
			       parseSSAPmsg_get_type(msgContent) );
	  retvalue = -1;
	}
      else
	{
	  retvalue = 1;
	}
    }
  else
    {
      whiteboard_log_debug("Sending insert command failed\n");
      retvalue = -1;
    }
  ssBufDesc_free(&buf);
  whiteboard_log_debug_fe();
  return retvalue;
}

gint sib_access_update(SIBAccess *sa, ssElement_ct nodeid,
		       gint msgnumber, EncodingType encoding, guchar *insert_request, 
		       guchar *remove_request, NodeMsgContent_t *msgContent)
{
  gint rbytes=0;
  gint retvalue = -1;
  ssBufDesc_t *buf = NULL;
  gchar *sendmsg = NULL;
  gint sendmsgLen = -1;
  int s;
  whiteboard_log_debug_fb();

  g_return_val_if_fail( NULL != sa, -1);
  g_return_val_if_fail( NULL != nodeid, -1 );
  g_return_val_if_fail( NULL != insert_request, -1 );
  g_return_val_if_fail( NULL != remove_request, -1 );
  
  buf=sib_access_create_update_message(sa->uri, nodeid, msgnumber, encoding, insert_request, remove_request);
  if(buf==NULL)
    {
      whiteboard_log_warning("Could not create UPDATE message\n");
      whiteboard_log_debug_fe();
      return -1;
    }

  s = sib_access_get_and_connect_socket(sa);
  if(s<0)
    {
      whiteboard_log_warning("socket err\n");
      ssBufDesc_free(&buf);      
      whiteboard_log_debug_fe();
      return -1;
    }
  
  sendmsg = ssBufDesc_GetMessage(buf);
  sendmsgLen = ssBufDesc_GetMessageLen(buf);
  rbytes  = sib_access_command(s, sendmsg, sendmsgLen, msgContent);
  
  if(rbytes > 0)
    {
      
      if( g_ascii_strcasecmp((char *)nodeid, parseSSAPmsg_get_nodeid( msgContent) ) ||
	  g_ascii_strcasecmp((char *)sa->uri, parseSSAPmsg_get_spaceid( msgContent) ) ||
	  ( parseSSAPmsg_get_name(msgContent) != MSG_N_UPDATE ) ||
	  ( parseSSAPmsg_get_type(msgContent) != MSG_T_CNF ) )
	{
	  whiteboard_log_debug("Not proper update conf. Receiver: %s, Sender: %s, Name: %d, Type: %d\n",
			       parseSSAPmsg_get_nodeid( msgContent),
			       parseSSAPmsg_get_spaceid( msgContent),
			       parseSSAPmsg_get_name(msgContent),
			       parseSSAPmsg_get_type(msgContent) );
	  retvalue = -1;
	}
      else
	{
	  retvalue = 1;
	}
    }
  else
    {
      whiteboard_log_debug("Sending update command failed\n");
      retvalue = -1;
    }
  ssBufDesc_free(&buf);      
  whiteboard_log_debug_fe();
  return retvalue;
}

gint sib_access_remove(SIBAccess *sa, ssElement_ct nodeid, gint msgnumber, EncodingType encoding,
		       guchar *request, NodeMsgContent_t *msgContent)
{
  gint rbytes=0;
  gint retvalue = -1;
  ssBufDesc_t *buf = NULL;
  gchar *sendmsg = NULL;
  gint sendmsgLen = -1;
  int s;
  whiteboard_log_debug_fb();

  g_return_val_if_fail( NULL != sa, -1);
  g_return_val_if_fail( NULL != nodeid, -1 );
  g_return_val_if_fail( NULL != request, -1 );
  
  buf=sib_access_create_remove_message(sa->uri, nodeid, msgnumber, encoding, request );
  if(buf==NULL)
    {
      whiteboard_log_warning("Could not create REMOVE message\n");
      whiteboard_log_debug_fe();
      return -1;
    }
  s = sib_access_get_and_connect_socket(sa);
  if(s<0)
    {
      whiteboard_log_warning("socket err\n");
      ssBufDesc_free(&buf);      
      whiteboard_log_debug_fe();
      return -1;
    }
  sendmsg = ssBufDesc_GetMessage(buf);
  sendmsgLen = ssBufDesc_GetMessageLen(buf);
  

  rbytes  = sib_access_command(s, sendmsg, sendmsgLen, msgContent);
   
  if(rbytes > 0)
    {
      
      whiteboard_log_debug("Remove command sent, Parsing response... %d bytes, \n", rbytes);
      if( g_ascii_strcasecmp((char *)nodeid, parseSSAPmsg_get_nodeid( msgContent) ) ||
	  g_ascii_strcasecmp((char *)sa->uri, parseSSAPmsg_get_spaceid( msgContent) ) ||
	  ( parseSSAPmsg_get_name(msgContent) != MSG_N_REMOVE ) ||
	  ( parseSSAPmsg_get_type(msgContent) != MSG_T_CNF ) )
	{
	  whiteboard_log_debug("Not proper remove conf. Receiver: %s, Sender: %s, Name: %d, Type: %d\n",
			       parseSSAPmsg_get_nodeid( msgContent),
			       parseSSAPmsg_get_spaceid( msgContent),
			       parseSSAPmsg_get_name(msgContent),
			       parseSSAPmsg_get_type(msgContent) );
	  retvalue = -1;
	}
      else
	{
	  retvalue = 1;
	}
    }
  else
    {
      whiteboard_log_debug("Sending remove command failed\n");
      retvalue = -1;
    }
  ssBufDesc_free(&buf);      
  whiteboard_log_debug_fe();
  return retvalue;
}

gint sib_access_query(SIBAccess *sa, ssElement_ct nodeid, gint msgnumber, gint type, guchar *request, NodeMsgContent_t *msgContent) 

{
  gint rbytes=0;
  gint retvalue = -1;
  ssBufDesc_t *buf = NULL;
  gchar *sendmsg = NULL;
  gint sendmsgLen = -1;
  int s;
  whiteboard_log_debug_fb();
  
  g_return_val_if_fail( NULL != sa, -1);
  g_return_val_if_fail( NULL != nodeid, -1 );
  g_return_val_if_fail( NULL != request, -1 );
  
  buf=sib_access_create_query_message(sa->uri, nodeid, msgnumber, type, request );
  if(buf==NULL)
    {
      whiteboard_log_warning("Could not create QUERY message\n");
      whiteboard_log_debug_fe();
      return -1;
    }
  s = sib_access_get_and_connect_socket(sa);
  if(s<0)
    {
      whiteboard_log_warning("socket err\n");
      ssBufDesc_free(&buf);  
      whiteboard_log_debug_fe();
      return -1;
    }

  sendmsg = ssBufDesc_GetMessage(buf);
  sendmsgLen = ssBufDesc_GetMessageLen(buf);
  
  rbytes  = sib_access_command(s, sendmsg, sendmsgLen, msgContent);
   
  if(rbytes > 0)
    {
      if( g_ascii_strcasecmp((char *)nodeid, parseSSAPmsg_get_nodeid( msgContent) ) ||
	  g_ascii_strcasecmp((char *)sa->uri, parseSSAPmsg_get_spaceid( msgContent) ) ||
	  ( parseSSAPmsg_get_name(msgContent) != MSG_N_QUERY ) ||
	  ( parseSSAPmsg_get_type(msgContent) != MSG_T_CNF ) )
	{
	  whiteboard_log_debug("Not proper query conf. Receiver: %s, Sender: %s, Name: %d, Type: %d\n",
			       parseSSAPmsg_get_nodeid( msgContent),
			       parseSSAPmsg_get_spaceid( msgContent),
			       parseSSAPmsg_get_name(msgContent),
			       parseSSAPmsg_get_type(msgContent) );
	  retvalue = -1;
	}
      else
	{
	  retvalue = 1;
	}
    }
  else
    {
      whiteboard_log_debug("Sending query command failed\n");
      retvalue = -1;
    }
  ssBufDesc_free(&buf);  

  whiteboard_log_debug_fe();
  return retvalue;
}

gint sib_access_subscribe(SIBAccess *sa, ssElement_ct nodeid, gint msgnumber, gint type,
			  guchar *request, NodeMsgContent_t *msgContent)
{
  gint rbytes=0;
  gint retvalue = -1;
  ssBufDesc_t *buf = NULL;
  gchar *sendmsg = NULL;
  gint sendmsgLen = -1;
  int s;
  whiteboard_log_debug_fb();
  
  g_return_val_if_fail( NULL != sa, -1);
  g_return_val_if_fail( NULL != nodeid, -1 );
  g_return_val_if_fail( NULL != request, -1 );
  
  buf=sib_access_create_subscribe_message(sa->uri, nodeid, msgnumber,type, request );
  if(buf==NULL)
    {
      whiteboard_log_warning("Could not create SUBSCRIBE message\n");
      whiteboard_log_debug_fe();
      return -1;
    }
  s =  sib_access_get_and_connect_socket(sa);
  if (s<0)
    {
      whiteboard_log_warning("socket err\n");
      ssBufDesc_free(&buf);  

      whiteboard_log_debug_fe();
      return -1;
    }
  sendmsg = ssBufDesc_GetMessage(buf);
  sendmsgLen = ssBufDesc_GetMessageLen(buf);

  rbytes  = sib_access_subscribe_command(s, sendmsg, sendmsgLen, msgContent);
   
  if(rbytes > 0)
    {
      if( g_ascii_strcasecmp((char *)nodeid, parseSSAPmsg_get_nodeid( msgContent) ) ||
	  g_ascii_strcasecmp((char *)sa->uri, parseSSAPmsg_get_spaceid( msgContent) ) ||
	  ( parseSSAPmsg_get_name(msgContent) != MSG_N_SUBSCRIBE ) ||
	  ( parseSSAPmsg_get_type(msgContent) != MSG_T_CNF ) )
	{
	  whiteboard_log_debug("Not proper subscribe conf. Receiver: %s, Sender: %s, Name: %d, Type: %d\n",
			       parseSSAPmsg_get_nodeid( msgContent),
			       parseSSAPmsg_get_spaceid( msgContent),
			       parseSSAPmsg_get_name(msgContent),
			       parseSSAPmsg_get_type(msgContent) );
	  retvalue = -1;
	  close(s);
	}
      else
	{
	  SubData *sdata = sub_data_new(s);
	  if( sib_access_add_subscription_socket(sa, (guchar *)g_strdup(parseSSAPmsg_get_subscriptionid(msgContent)), sdata) )
	    retvalue = 1;
	  else
	    {
	      whiteboard_log_debug("Could not add socket w/ subscription_id: %s\n", parseSSAPmsg_get_subscriptionid(msgContent));
	      sub_data_free_close(sdata);
	      retvalue = -1;
	    }
	}
    }
  else
    {
      whiteboard_log_debug("Sending Subscribe command failed\n");
      retvalue = -1;
    }
  ssBufDesc_free(&buf);  
  whiteboard_log_debug_fe();
  return retvalue;
}


gint sib_access_unsubscribe(SIBAccess *sa, ssElement_ct nodeid, gint msgnumber, guchar *request)
{
  gint success=-1;

  ssBufDesc_t *buf = NULL;
  gchar *sendmsg = NULL;
  gint sendmsgLen = -1;
  int s;
  whiteboard_log_debug_fb();
  
  g_return_val_if_fail( NULL != sa, -1);
  g_return_val_if_fail( NULL != nodeid, -1 );
  g_return_val_if_fail( NULL != request, -1 );
  
  buf=sib_access_create_unsubscribe_message(sa->uri, nodeid, msgnumber, request );
  if(buf==NULL)
    {
      whiteboard_log_warning("Could not create UNSUBSCRIBE message\n");
      whiteboard_log_debug_fe();
      return -1;
    }
  s =  sib_access_get_and_connect_socket(sa);
  if (s<0)
    {
      whiteboard_log_warning("socket err\n");
      ssBufDesc_free(&buf);  
      whiteboard_log_debug_fe();
      return -1;
    }
  
  sendmsg = ssBufDesc_GetMessage(buf);
  sendmsgLen = ssBufDesc_GetMessageLen(buf);
  success  = sib_access_send_command(s, sendmsg, sendmsgLen);
   
  if(success < 0)
    {
      whiteboard_log_debug("Cound not send unsubscribe command\n");
    }
  ssBufDesc_free(&buf);  
  whiteboard_log_debug_fe();
  return success;
}

static gint sib_access_command(int s, gchar *msg, gint len, NodeMsgContent_t *response)
{
  gint rbytes = 0;
  //apr09unused gint rtmp;
  //apr09unused gboolean finished = FALSE;
  //apr09unused gint rpos = 0;
  SubData *sdata = NULL;
  whiteboard_log_debug_fb();

  sdata = sub_data_new(s);

  if( sib_access_send_message(s, msg, len) < 0)
    {
      whiteboard_log_warning("Could not send message\n");
      sub_data_free_close(sdata);
      whiteboard_log_debug_fe();
      return -1;
    }

  shutdown(s, SHUT_WR); // shutdown write direction.
  
  rbytes = sib_access_receive_message(sdata, response);
  
  sub_data_free_close(sdata);// closes socket also
  
  whiteboard_log_debug_fe();
  return rbytes;
}

static gint sib_access_send_command(int s, gchar *msg, gint len)
{
  gint rbytes = 0;
  whiteboard_log_debug_fb();
  if( sib_access_send_message(s, msg, len) < 0)
    {
      whiteboard_log_warning("Could not send message\n");
      close (s);
      whiteboard_log_debug_fe();
      return -1;
    }

  close(s);
  whiteboard_log_debug_fe();
  return rbytes;
}

static gint sib_access_subscribe_command(int s, gchar *msg, gint len, NodeMsgContent_t *msgContent)
{
  gint rbytes = 0;
  //apr09unused gint rtmp;
  //apr09unused gboolean finished = FALSE;
  //apr09unused gint rpos = 0;
  //apr09unused gboolean close_socket = FALSE;
  SubData *sdata=NULL;
  whiteboard_log_debug_fb();
  sdata = sub_data_new(s);
  if( sib_access_send_message(s, msg, len) < 0)
    {
      whiteboard_log_warning("Could not send message\n");
      close (s);
      whiteboard_log_debug_fe();
      return -1;
    }
  shutdown(s, SHUT_WR); // shutdown write direction.
  rbytes = sib_access_receive_message(sdata, msgContent);
  if(rbytes < 0)
    sub_data_free_close(sdata); // closes socket also
  else
    sub_data_free(sdata);
  
#if 0  
  while(!finished )
    {
      rtmp = recv(s, recvbuf+rpos, recvlen-rpos, 0);
      if(rtmp > 0)
	{
	  rpos+= rtmp;
	}
      else if(rpos < 0)
	{
	  whiteboard_log_warning("receive error\n");
	  close(s);
	  rbytes = -1;
	  finished = TRUE;
	}
      else
	{
	  whiteboard_log_warning("received zero bytes\n");
	  finished = TRUE;
	}

      if(sib_access_check_msg_end_tag(recvbuf,rpos) || (rpos >= recvlen))  
	{
	  rbytes = rpos;
	  finished = TRUE;
	  whiteboard_log_debug("Received %d bytes\n", rbytes);
	}
    }
#endif 
  // note: socket not closed!

  whiteboard_log_debug_fe();
  return rbytes;
}


static gint sib_access_send_message(int s, gchar *buf, gint len)
{
  gint total = 0;        // how many bytes we've sent
  gint bytesleft = len; // how many we have left to send
  gint n;
  whiteboard_log_debug_fb();
  whiteboard_log_debug("Sending request: %s", buf);
  while(total < len)
    {
      n = send(s, buf+total, bytesleft, 0);
      if(n == -1)
	{
	  whiteboard_log_debug_fe();
	  return -1;
	}
      total += n;
      bytesleft -= n;
    }
  whiteboard_log_debug_fe();
  return 0;
}

static int sib_access_get_and_connect_socket(SIBAccess *sa)
{
  int sockfd;
  struct sockaddr_in sib_addr;
  whiteboard_log_debug_fb();
  
  g_return_val_if_fail(sa!=NULL, -1);
  g_return_val_if_fail(sa->ip_address!=NULL, -1);
  
  sockfd = socket(PF_INET, SOCK_STREAM, 0); 
  
  if(sockfd < 0)
    {
      whiteboard_log_warning("Could not open socket\n");
      whiteboard_log_debug_fe();
      return -1;
    }
  
  sib_addr.sin_family = AF_INET;       
  sib_addr.sin_port = htons(sa->port);   
  sib_addr.sin_addr.s_addr = inet_addr(sa->ip_address);
  memset(sib_addr.sin_zero, '\0', sizeof sib_addr.sin_zero);
  
  if( connect( sockfd, (struct sockaddr *)&sib_addr, sizeof(sib_addr)) < 0)
    {
      whiteboard_log_warning("Could not connect socket\n");
      close(sockfd);
      whiteboard_log_debug_fe();
      return -1;
      
    }
  whiteboard_log_debug_fe();
  
  return sockfd;
}

static ssBufDesc_t *sib_access_create_join_message(ssElement_ct ssId,
					     //apr09obsolete const gchar *username,
					     ssElement_ct nodeid,
					     gint msgnumber
					     //apr09obsolete , const guchar *node_pk,
					     //apr09obsolete ssElement_ct accessGroup_req
					     )
{
  //gchar *msg = NULL;
  ssBufDesc_t *buf = NULL;
  whiteboard_log_debug_fb();
  g_return_val_if_fail( ssId != NULL, NULL);
  g_return_val_if_fail( nodeid != NULL, NULL);
  //apr09obsolete g_return_val_if_fail( node_pk != NULL, NULL);
    

  buf = ssBufDesc_new();
  if(ss_StatusOK != ssBufDesc_CreateJoinMessage(buf,
						ssId,
						//apr09obsolete username,
						nodeid,
						msgnumber
						//apr09obsolete , node_pk,
						//apr09obsolete accessGroup_req
						) )
    {
      ssBufDesc_free(&buf);
      return NULL;
    }
  whiteboard_log_debug_fe();
  //msg = ssBufDesc_GetMessage(buf);
  //  g_free(buf);
  return buf;
}

static ssBufDesc_t *sib_access_create_leave_message(ssElement_ct sibid, ssElement_ct nodeid, gint msgnumber )
{
  ssBufDesc_t *buf = NULL;
  whiteboard_log_debug_fb();
  g_return_val_if_fail( nodeid!= NULL, NULL);
  buf = ssBufDesc_new();
  if(ss_StatusOK != ssBufDesc_CreateLeaveMessage(buf, sibid, nodeid, msgnumber,TRUE) )
    {
      ssBufDesc_free(&buf);
      return NULL;
    }
  whiteboard_log_debug_fe();  
  return buf;
}

static ssBufDesc_t *sib_access_create_insert_message(ssElement_ct sibid,
						     ssElement_ct nodeid,
						     gint msgnumber,
						     EncodingType encoding,
						     guchar *request)
{
  ssBufDesc_t *buf = NULL;
  whiteboard_log_debug_fb();
  g_return_val_if_fail( nodeid != NULL, NULL);
  g_return_val_if_fail( request != NULL, NULL);

  buf = ssBufDesc_new();
  if(ss_StatusOK != ssBufDesc_CreateInsertMessage(buf, sibid, nodeid, msgnumber, encoding, request,TRUE) )
    {
      ssBufDesc_free(&buf);
      return NULL;
    }

  whiteboard_log_debug_fe();  
  return buf;
}

static ssBufDesc_t *sib_access_create_update_message(ssElement_ct sibid, ssElement_ct nodeid, gint msgnumber,
					       EncodingType encoding, guchar *insert_request, guchar *remove_request)
{
  ssBufDesc_t *buf = NULL;
  whiteboard_log_debug_fb();
  g_return_val_if_fail( nodeid != NULL, NULL);
  g_return_val_if_fail( insert_request != NULL, NULL);
  g_return_val_if_fail( remove_request != NULL, NULL);

  buf = ssBufDesc_new();
  if(ss_StatusOK != ssBufDesc_CreateUpdateMessage(buf, sibid, nodeid, msgnumber, encoding, insert_request, remove_request, TRUE) )
    {
      ssBufDesc_free(&buf);
      return NULL;
    }

  whiteboard_log_debug_fe();  
  return buf;
}

static ssBufDesc_t *sib_access_create_remove_message(ssElement_ct sibid, ssElement_ct nodeid, 
						     gint msgnumber, EncodingType encoding, 
						     guchar *request)
{
  ssBufDesc_t *buf = NULL;
  whiteboard_log_debug_fb();
  g_return_val_if_fail( nodeid != NULL, NULL);
  g_return_val_if_fail( request != NULL, NULL);

  buf = ssBufDesc_new();
  if(ss_StatusOK != ssBufDesc_CreateRemoveMessage(buf, sibid, nodeid, msgnumber, encoding, request) )
    {
      ssBufDesc_free(&buf);
      return NULL;
    }

  whiteboard_log_debug_fe();  
  return buf;
}

static ssBufDesc_t *sib_access_create_query_message(ssElement_ct sibid, ssElement_ct nodeid,
						    gint msgnumber, gint type, guchar *request)
{
  ssBufDesc_t *buf = NULL;
  whiteboard_log_debug_fb();
  g_return_val_if_fail( nodeid != NULL, NULL);
  g_return_val_if_fail( request != NULL, NULL);

  buf = ssBufDesc_new();
  if(ss_StatusOK != ssBufDesc_CreateQueryMessage(buf, sibid, nodeid, msgnumber, type, request) )
    {
      ssBufDesc_free(&buf);
      return NULL;
    }

  whiteboard_log_debug_fe();  
  return buf;
}

ssBufDesc_t *sib_access_create_subscribe_message(ssElement_ct sibid, ssElement_ct nodeid, 
						 gint msgnumber, gint type, guchar *request)
{
  ssBufDesc_t *buf = NULL;
  whiteboard_log_debug_fb();
  g_return_val_if_fail( nodeid != NULL, NULL);
  g_return_val_if_fail( request != NULL, NULL);

  buf = ssBufDesc_new();
  if(ss_StatusOK != ssBufDesc_CreateSubscribeMessage(buf, sibid, nodeid, msgnumber, type, request) )
    {
      ssBufDesc_free(&buf);
      return NULL;
    }

  whiteboard_log_debug_fe();  
  return buf;
}

static ssBufDesc_t *sib_access_create_unsubscribe_message(ssElement_ct sibid, ssElement_ct nodeid,
							  gint msgnumber, guchar *request)
{
  ssBufDesc_t *buf = NULL;
  whiteboard_log_debug_fb();
  g_return_val_if_fail( nodeid != NULL, NULL);
  g_return_val_if_fail( request != NULL, NULL);

  buf = ssBufDesc_new();
  if(ss_StatusOK != ssBufDesc_CreateUnsubscribeMessage(buf, sibid, nodeid, msgnumber, request) )
    {
      ssBufDesc_free(&buf);
      return NULL;
    }

  whiteboard_log_debug_fe();  
  return buf;
}

gint sib_access_wait_for_subscription_ind(SIBAccess *sa, ssElement_ct nodeid,  guchar *id,
					  NodeMsgContent_t *msg)
{
  SubData *sdata;
  gint rbytes = 0;
  gint rtmp;
  //  gboolean finished = FALSE;
  //gint rpos = 0;
  whiteboard_log_debug_fb();
  g_return_val_if_fail( sa != NULL, -1);
  g_return_val_if_fail(msg != NULL, -1);
  g_return_val_if_fail(id != NULL, -1);
  //gchar *recvbuf=NULL;
  /*  g_return_val_if_fail(sa->subs_sockfd >= 0, -1);*/
  sdata = sib_access_get_subscription_socket(sa, id);
  
  rtmp =  sib_access_receive_message( sdata, msg);
  if( rtmp > 0 )
    {
      whiteboard_log_debug("Handled (%d bytes)\n", rtmp);
      
      if( !g_ascii_strcasecmp((char *)nodeid, parseSSAPmsg_get_nodeid( msg) ) &&
	  !g_ascii_strcasecmp((char *)sa->uri, parseSSAPmsg_get_spaceid( msg) ) &&
	  ( parseSSAPmsg_get_name(msg) == MSG_N_SUBSCRIBE ) &&
	  ( parseSSAPmsg_get_type(msg) == MSG_T_IND ) &&
	  !g_ascii_strcasecmp( (gchar *)id, parseSSAPmsg_get_subscriptionid(msg)) )
	{
	  whiteboard_log_debug("Received subscription indication\n");
	  rbytes = rtmp;
	}
      else if  ( !g_ascii_strcasecmp((char *)nodeid, parseSSAPmsg_get_nodeid( msg) ) &&
		 !g_ascii_strcasecmp((char *)sa->uri, parseSSAPmsg_get_spaceid( msg) ) &&
		 ( parseSSAPmsg_get_name(msg) == MSG_N_UNSUBSCRIBE ) &&
		 ( ( parseSSAPmsg_get_type(msg) == MSG_T_CNF) ||
		   ( parseSSAPmsg_get_type(msg) == MSG_T_IND) ) &&
		 !g_ascii_strcasecmp( (gchar *)id, parseSSAPmsg_get_subscriptionid(msg)) )
	{
	  whiteboard_log_debug("Received unsubscribe indication/confirmation\n");
	  sib_access_remove_subscription_socket(sa,id);
	  rbytes = rtmp;
	}
      else
	{
	  whiteboard_log_debug("Not proper subscription indication: To: %s from %s, Name: %d, Type: %d\n",
			       parseSSAPmsg_get_nodeid( msg),
			       parseSSAPmsg_get_spaceid(msg),
			       parseSSAPmsg_get_name(msg),
			       parseSSAPmsg_get_type(msg) );
	  sib_access_remove_subscription_socket(sa,id);
	  rbytes = -2;
	}
    }
  else
    {
      whiteboard_log_debug("Error (%d) while receiving message\n", rtmp);
      rbytes = rtmp;
      sib_access_remove_subscription_socket(sa,id);
    }
  
  whiteboard_log_debug_fe();

  return rbytes;
}

gint sib_access_receive_message( SubData *sdata, NodeMsgContent_t *msg)
{
  gint rtmp;
  gint bytes_handled = 0;
  //apr09unused gint tmpbytes = 0;
  //apr09unused gint bytecountold = 0;
  gint bytecountnew = 0;
  ssStatus_t status;
  gint index = 0;
  gboolean finished = FALSE;
  while(!finished)
    {
      if( sdata->remaining_len )
	{
	  index = sdata->len - sdata->remaining_len;
	  //bytecountold = parseM3msg_parsedbytecount(msg);
	  // before first parsed section, parser responds with -1
	  //if(bytecountold < 0) 
	  //  bytecountold = 0;
	  
	  bytes_handled += sdata->remaining_len;
	  whiteboard_log_debug("Parsing section; index:%d, remaining_len: %d, bytes_handled: %d\n",
			       index, sdata->remaining_len, bytes_handled);
	  
	  status = parseSSAPmsg_section (msg,
					 sdata->recvbuf + index,
					 sdata->remaining_len,
					 0);
	  
	  if(status == ss_StatusOK)
	    {
	      gint not_handled;
	      bytecountnew = parseSSAPmsg_parsedbytecount(msg);
	      not_handled = bytes_handled - bytecountnew;
	      whiteboard_log_debug("Parse finished, bytecount (%d), not_handled (%d)\n", bytecountnew, not_handled);
	      sdata->remaining_len = not_handled;
	      bytes_handled = bytecountnew;
	      finished = TRUE;
	    }
	  else if(status == ss_ParsingInProgress)
	    {
	      sdata->len = 0;
	      sdata->remaining_len = 0;
	    }
	  else
	    {
	      whiteboard_log_debug("Parse error %d\n", status);
	      finished = TRUE;
	      bytes_handled = -1;
	    }
	}
      
      if (!finished)
	{
	  rtmp = recv(sdata->s, sdata->recvbuf, BUF_SIZE, 0);
	  if(rtmp < 0)
	    {
	      whiteboard_log_warning("receive error\n");
	      finished = TRUE;
	      bytes_handled = -1;
	    }
	  else if (rtmp > 0)
	    {
#if WHITEBOARD_DEBUG == 1
	      gchar *dbg = g_strndup( sdata->recvbuf, rtmp);
	      whiteboard_log_debug("Received (%d) bytes, len: %d, msg: %s\n", rtmp, sdata->len, dbg);
	      g_free(dbg);
#endif
	      sdata->len = rtmp;
	      sdata->remaining_len = rtmp;
	    }
	  else
	    {
	      whiteboard_log_warning("received zero bytes\n");
	      finished = TRUE;
	      bytes_handled = -1;
	    }
	}
      
    }
  
  return bytes_handled;
}


gboolean sib_access_add_subscription_socket(SIBAccess *sa, guchar *subscription_id, SubData *sdata)
{
  gboolean ret = FALSE;
  whiteboard_log_debug_fb();
  g_return_val_if_fail(sa != NULL, FALSE);
  g_return_val_if_fail(subscription_id!= NULL, FALSE);
  g_return_val_if_fail(socket != NULL,FALSE);

  whiteboard_log_debug("Trying to add subdata with id (%s)\n", subscription_id);
  if(sib_access_get_subscription_socket(sa,subscription_id) == NULL)
    {
      //      new = g_new0(gint, 1);
      //*new = socket;
      g_hash_table_insert( sa->subs_sockfd_map, g_strdup((gchar *)subscription_id), sdata);
      ret = TRUE;
    }
  whiteboard_log_debug_fe();
  return ret;
}

SubData *sib_access_get_subscription_socket(SIBAccess *sa, guchar *subscription_id)
{
  gpointer sdata;
  whiteboard_log_debug_fb();
  g_return_val_if_fail(sa != NULL, NULL);
  g_return_val_if_fail(subscription_id!= NULL, NULL);
  whiteboard_log_debug("Trying to get socket with subscription_id: %s, map size: %d\n",
		       subscription_id, g_hash_table_size(sa->subs_sockfd_map));

  sdata =  g_hash_table_lookup(sa->subs_sockfd_map, subscription_id) ;
  if(sdata == NULL)
    {
      whiteboard_log_debug("SData with subscription id: %s not found\n", subscription_id);
    }

  whiteboard_log_debug_fe();
  return ((SubData *)sdata);
}

gboolean sib_access_remove_subscription_socket(SIBAccess *sa, guchar *subscription_id)
{
  SubData *sdata;
  gboolean retval = FALSE;
   whiteboard_log_debug_fb();
   g_return_val_if_fail(sa != NULL, -1);
   g_return_val_if_fail(subscription_id!= NULL, -1);
   sdata = sib_access_get_subscription_socket(sa, subscription_id);
   if (sdata != NULL)
     {
       /* Remove the socket from the hash map */
       retval = g_hash_table_remove(sa->subs_sockfd_map, subscription_id);
     }
   whiteboard_log_debug_fe();
   return retval;
}


void sub_data_free(gpointer data)
{
  SubData *sdata = (SubData *)data;
  whiteboard_log_debug_fb();
  g_return_if_fail(sdata != NULL);

  if(sdata->recvbuf)
    g_free(sdata->recvbuf);
  
  g_free(sdata);
  
  whiteboard_log_debug_fe();
}

void sub_data_free_close(gpointer data)
{
  SubData *sdata = (SubData *)data;
  whiteboard_log_debug_fb();
  g_return_if_fail(sdata != NULL);
  
  close(sdata->s);
  sub_data_free(sdata);
  
  whiteboard_log_debug_fe();
}

static SubData *sub_data_new(int s)
{
  SubData *self = NULL;
  whiteboard_log_debug_fb();
  self = g_new0(SubData, 1);
  self->s = s;
  self->recvbuf = g_new0(gchar, BUF_SIZE);
  self->len = 0;
  self->remaining_len = 0;
  whiteboard_log_debug_fe();
  return self;
}
