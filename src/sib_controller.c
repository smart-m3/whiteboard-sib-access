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
 * 
 *
 * sib_controller.c
 *
 * Copyright 2007 Nokia Corporation
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>

#if AVAHI==1
#include <avahi-gobject/ga-client.h>
#include <avahi-gobject/ga-service-browser.h>
#endif

#include <whiteboard_log.h>

#include "sib_service.h"
#include "sib_controller.h"

#if AVAHI==0
//#define SIB_IP "192.168.0.156"// Vista
//#define SIB_IP "192.168.0.139"  // Jukka's laptop
//#define SIB_IP "192.168.0.103"  // Ubuntu-HLa
//#define SIB_IP "192.168.0.196"  // m3laptop
//#define SIB_IP "192.168.0.181"
#define SIB_IP "127.0.0.1" //local host
//#define SIB_IP "192.168.0.109"
//#define SIB_IP "212.213.221.65"
//define SIB_IP "192.168.0.155" 
#define SIB_PORT 10010

#define TEST_UDN "X"
#define TEST_NAME "TestServer"

#define LOCAL_UDN "Y"
#define LOCAL_NAME "LocalServer"
#endif
#define SIB_SERVICE_TYPE "_kspace._tcp"


/*****************************************************************************
 * Structure definitions
 *****************************************************************************/
struct _SIBController
{
#if AVAHI==1

  GaClient *client;
  GaServiceBrowser *service_browser;
#endif

  SIBControllerListener listener;
  gpointer listener_data;
  GHashTable *services;
  
  GMutex* mutex; /*locking*/
};



static SIBController* sib_controller_new( );
static void sib_controller_lock(SIBController *self);
static void sib_controller_unlock(SIBController *self);

#if AVAHI==0
static guint timeout_id = 0;
static gboolean sib_controller_search_timeout( gpointer user_data);
#endif

gint sib_controller_start( SIBController *self);

gint sib_controller_stop( SIBController *self);

static SIBController *controller_singleton = NULL;

#if AVAHI==1
// Callbacks for GaServiceBrowser signals
static void sib_controller_new_service_cb( GaServiceBrowser *context,
					   gint interface,
					   gint protocol,
					   gchar *name,
					   gchar *type,
					   gchar *domain,
					   gint flags,
					   gpointer userdata);

static void sib_controller_removed_service_cb( GaServiceBrowser *context,
					   gint interface,
					   gint protocol,
					   gchar *name,
					   gchar *type,
					   gchar *domain,
					   gint flags,
					   gpointer userdata);
static void sib_controller_all_for_now_cb( GaServiceBrowser *context,
					       gpointer userdata);


// Callbacks for GaServiceResolver signals

static void sib_controller_resolver_found( GaServiceResolver *context,
					   gint interface,
					   gint protocol,
					   gchar *name,
					   gchar *type,
					   gchar *domain,
					   gchar *host_name,
					   AvahiAddress *avahi_address,
					   gint port,
					   AvahiStringList *txt,
					   gint flags,
					   gpointer userdata);

static void sib_controller_resolver_failure( GaServiceResolver *context,
					     GError *error,
					     gpointer userdata);
// utilities for handling hash table
static gboolean sib_controller_add_service( SIBController *self,
					    SIBServerData *data);
static gboolean sib_controller_remove_service( SIBController *self,
					       guchar *uri);
static SIBServerData *sib_controller_get_service( SIBController *self,
						  guchar *uri);

#endif


static void sib_controller_free_sibserverdata(gpointer data);

/**
 * Create the SIBService singleton if it doesn't exist. Otherwise returns
 * the pointer to the service singleton.
 *
 * @return SIBService*
 */
static SIBController* sib_controller_new( )
{
  whiteboard_log_debug_fb();
  SIBController *self = NULL;
  self = g_new0(SIBController, 1);
  g_return_val_if_fail(self != NULL, NULL);

  self->mutex = g_mutex_new();
  self->listener = NULL;
  self->listener_data = NULL;

#if AVAHI==1
  self->client = ga_client_new(GA_CLIENT_FLAG_NO_FLAGS);
#endif
  self->services = g_hash_table_new_full(g_str_hash,
					 g_str_equal,
					 NULL, 
					 sib_controller_free_sibserverdata);
  

  whiteboard_log_debug_fe();

  return self;
}

gint sib_controller_set_listener(SIBController *self, SIBControllerListener listener, gpointer user_data)
{
whiteboard_log_debug_fb();
  g_return_val_if_fail( self != NULL, -1);

  self->listener = listener;
  self->listener_data = user_data;
  whiteboard_log_debug_fe();
  return 0;
}


/**
 * Get the SIBService singleton.
 *
 * @return SIBService*
 */
SIBController* sib_controller_get()
{
  whiteboard_log_debug_fb();
  if (controller_singleton == NULL)
    {
      controller_singleton = sib_controller_new();
    }
  whiteboard_log_debug_fe();
  return controller_singleton;
}

/**
 * Destroy a UPnP service
 *
 * @param service The service to destroy
 * @return -1 on errors, 0 on success
 */
gint sib_controller_destroy(SIBController* self)
{

  whiteboard_log_debug_fb();

  g_return_val_if_fail(self != NULL, -1);

  /* Ensure that the ctrl is stopped */
  sib_controller_stop(self);

  g_hash_table_destroy(self->services);
  
  sib_controller_lock(self);

  sib_controller_unlock(self);

  whiteboard_log_debug_fe();

  return 0;
}

/**
 * Lock a SIBController instance during a critical operation
 *
 * @param self The SIBController instance to lock
 */
static void sib_controller_lock(SIBController* self)
{
  g_return_if_fail(self != NULL);
  g_mutex_lock(self->mutex);
}

/**
 * Unlock a SIBController instance after a critical operation
 *
 * @param self The SIBController instance to unlock
 */ 
static void sib_controller_unlock(SIBController* self)
{
  g_return_if_fail(self != NULL);
  g_mutex_unlock(self->mutex);
}


/*****************************************************************************
 *
 *****************************************************************************/

/**
 * Start a controller
 * * @param self The controller to start
 * @return -1 on errors, 0 on success
 */
gint sib_controller_start(SIBController* self)
{
  //  struct timeval tv;
  //const char *version;
  gint retvalue = -1;
  whiteboard_log_debug_fb();
#if AVAHI==1  
  GError *err = NULL;
#endif  
  g_return_val_if_fail(self != NULL, -1);

#if AVAHI==1  

  if(ga_client_start( self->client, &err) == TRUE )
    {
      retvalue = 0;
    }
  else if(err != NULL)
    {
      whiteboard_log_debug("Could not start avahi client: %s\n", err->message);
      g_error_free(err);
    }


  self->service_browser = ga_service_browser_new("_kspace._tcp");
  if(self->service_browser)
    {
      if( ga_service_browser_attach(self->service_browser,
				    self->client, &err) == TRUE)
	{
	  retvalue = 0;
	}
      else if(err != NULL)
	{
	  whiteboard_log_debug("Could not attach avahi service_browser: %s\n", err->message);
	  g_error_free(err); 
	}
    }
  

  g_signal_connect( G_OBJECT(self->service_browser),
		    "new-service",
		    (GCallback) sib_controller_new_service_cb,
		    self);
  
  g_signal_connect( G_OBJECT(self->service_browser),
		    "removed-service",
		    (GCallback) sib_controller_removed_service_cb,
		    self);
  
  g_signal_connect( G_OBJECT(self->service_browser),
		    "all-for-now",
		    (GCallback) sib_controller_all_for_now_cb,
		    self);
#else
  retvalue = 0;
#endif  
  whiteboard_log_debug_fe();

  return retvalue;
}

/**
 * Stop a controller
 *
 * @param controller The controller to stop
 * @return -1 on errors, 0 on success
 */
gint sib_controller_stop(SIBController* controller)
{
  whiteboard_log_debug_fb();

  g_return_val_if_fail(controller != NULL, -1);

  
  
  whiteboard_log_debug_fe();

  return 0;
}

#if AVAHI==0
gint sib_controller_search(SIBController *self)
{
  
  whiteboard_log_debug_fb();
  g_return_val_if_fail(self != NULL, -1);

  if( timeout_id == 0)
    {
      timeout_id=g_timeout_add( 5000,   // 5 seconds
				sib_controller_search_timeout,
				self );
    }
  
  
  whiteboard_log_debug_fe();
  return 0;
}

static gboolean sib_controller_search_timeout( gpointer user_data)
{
  SIBController *self = (SIBController *)(user_data);
  SIBService *service = (SIBService *)( self->listener_data);
  SIBServerData *ssdata = NULL;
  whiteboard_log_debug_fb();

  g_return_val_if_fail(self != NULL, FALSE);
  g_return_val_if_fail(service != NULL, FALSE);
  
  ssdata = g_new0(SIBServerData,1);    
  /*   if(servercount == 0) */
/*     { */
/*       ssdata->ip = g_strdup(SIB_IP); */
/*       ssdata->port = SIB_PORT; */
/*       name = g_strdup(TEST_NAME); */
/*       udn = g_strdup(TEST_UDN); */
/*       servercount++; */
/*     } */
/*   else  */
/*     { */
/*       ssdata->ip = g_strdup(SIB_LOCAL_IP); */
/*       ssdata->port = SIB_PORT; */
/*       name = g_strdup(LOCAL_NAME); */
/*       udn = g_strdup(LOCAL_UDN); */
/*       servercount++;  */
/*     } */
       ssdata->ip = g_strdup(SIB_IP); 
       ssdata->port = SIB_PORT; 
       ssdata->name = (guchar *)g_strdup(TEST_NAME); 
       ssdata->uri = (guchar *)g_strdup(TEST_UDN); 
  
       self->listener( SIBDeviceStatusAdded, ssdata, self->listener_data);
       
       sib_controller_free_sibserverdata(ssdata);
       whiteboard_log_debug_fe();
	 
  // do no call again
  return FALSE;
/*   if(servercount > 1) */
/*     return FALSE; */
/*   else */
/*     return TRUE; */
}
#endif /*AVAHI==0*/
	
					  
#if AVAHI==1
static void sib_controller_new_service_cb( GaServiceBrowser *context,
					   gint interface,
					   gint protocol,
					   gchar *name,
					   gchar *type,
					   gchar *domain,
					   gint flags,
					   gpointer userdata)
{
  whiteboard_log_debug_fb();
  SIBController *self = (SIBController *)userdata;
  GError *err = NULL;
  SIBServerData *ssdata = NULL;
  g_return_if_fail(self!=NULL);
  
  whiteboard_log_debug("New service: %s, type %s, domain: %s\n", name, type, domain);
  if( g_ascii_strcasecmp(type, SIB_SERVICE_TYPE) == 0)
    {
      ssdata = g_new0(SIBServerData,1);
      ssdata->uri = (guchar*)g_strdup(name);
      
      if(sib_controller_add_service(self,ssdata))
	{
	  ssdata->resolver = ga_service_resolver_new(interface,
						     protocol,
						     name,
						     type,
						     domain,
						     AVAHI_PROTO_INET,
						     GA_LOOKUP_NO_FLAGS);
	  if(ssdata->resolver)
	    {
	      if( ga_service_resolver_attach(ssdata->resolver,
					     self->client, &err) == TRUE)
		{
		  g_signal_connect( G_OBJECT(ssdata->resolver),
				    "found",
				    (GCallback) sib_controller_resolver_found,
				    self);
  
		  g_signal_connect( G_OBJECT(ssdata->resolver),
				    "failure",
				    (GCallback) sib_controller_resolver_failure,
				    self);
		}
	      else if(err != NULL)
		{
		  whiteboard_log_debug("Could not attach avahi service_browser w/ client: %s\n", err->message);
		  g_error_free(err);
		  sib_controller_remove_service(self, ssdata->uri);
		}
	    }
	  else
	    {
	      whiteboard_log_debug("Could not create avahi service_browser\n");
	      sib_controller_remove_service(self, ssdata->uri);
	    }
	}
    }
  whiteboard_log_debug_fe();
}


static void sib_controller_removed_service_cb( GaServiceBrowser *context,
					   gint interface,
					   gint protocol,
					   gchar *name,
					   gchar *type,
					   gchar *domain,
					   gint flags,
					   gpointer userdata)
{
  whiteboard_log_debug_fb();
  SIBController *self = (SIBController *)userdata;
  g_return_if_fail(self!=NULL);
  whiteboard_log_debug("Removed service: %s, type %s, domain: %s\n", name, type, domain);
  SIBServerData *ssdata = sib_controller_get_service(self, (guchar *)name);
  if( ssdata )
    {
      if( ssdata->ip ) // assume that if ip is filled, resolver has finished
	{
	  // notify listener
	  self->listener( SIBDeviceStatusRemoved, ssdata, self->listener_data );
	}
      sib_controller_remove_service(self, (guchar *)name);
    }
  whiteboard_log_debug_fe();
}

static void sib_controller_all_for_now_cb( GaServiceBrowser *context,
					   gpointer userdata)
{
  whiteboard_log_debug_fb();

  

  
  whiteboard_log_debug_fe();
}


static void sib_controller_resolver_found( GaServiceResolver *context,
					   gint interface,
					   gint protocol,
					   gchar *name,
					   gchar *type,
					   gchar *domain,
					   gchar *host_name,
					   AvahiAddress *avahi_address,
					   gint port,
					   AvahiStringList *txt,
					   gint flags,
					   gpointer userdata)
{
  whiteboard_log_debug_fb();
  SIBController *self = (SIBController *)userdata;
  g_return_if_fail( self!= NULL);
  SIBServerData *ssdata = NULL;

  ssdata = sib_controller_get_service(self, (guchar *)name);
  if(ssdata)
    {
      
      ssdata->ip = g_new0(gchar, AVAHI_ADDRESS_STR_MAX);
      avahi_address_snprint(ssdata->ip, AVAHI_ADDRESS_STR_MAX*sizeof(gchar), avahi_address);
      ssdata->name = (guchar *)g_strdup(host_name);
      ssdata->port = port;
      whiteboard_log_debug("Service %s resolved: %s, %d\n", name, ssdata->ip, port);
      self->listener( SIBDeviceStatusAdded, ssdata, self->listener_data );
    }
  whiteboard_log_debug_fe();
}


static void sib_controller_resolver_failure( GaServiceResolver *context,
					     GError *error,
					     gpointer userdata)
{
 whiteboard_log_debug_fb();

 if(error)
   {
     whiteboard_log_debug("Resolver failure: %s\n", error->message);
   }
  
 whiteboard_log_debug_fe(); 
}


static gboolean sib_controller_add_service( SIBController *self,
					    SIBServerData *data)
{
  gboolean ret = FALSE;
  whiteboard_log_debug_fb();
  // check that not existing previously
  if( sib_controller_get_service(self, data->uri) == NULL)
    {
      g_hash_table_insert(self->services, data->uri, data);
      ret = TRUE;
    }
  whiteboard_log_debug_fe();
  return ret;
}

static gboolean sib_controller_remove_service( SIBController *self,
					       guchar *uri)
{
  gboolean ret = FALSE;
  whiteboard_log_debug_fb();
  SIBServerData *ssdata=NULL;
  if( (ssdata = sib_controller_get_service(self, uri)) != NULL)
    {
      ret = g_hash_table_remove(self->services, uri);
    }
  whiteboard_log_debug_fe();
  return ret;
}

static SIBServerData *sib_controller_get_service( SIBController *self,
						  guchar *uri)
{
  SIBServerData *ssdata=NULL;
  
  whiteboard_log_debug_fb();
  // check that not existing previously
  
  ssdata = (SIBServerData *)g_hash_table_lookup(self->services,uri);
  
  whiteboard_log_debug_fe();
  return ssdata;
}
#endif  /*AVAHI==1*/

static void sib_controller_free_sibserverdata(gpointer data)
{
  SIBServerData *ssdata=(SIBServerData *)data;
  whiteboard_log_debug_fb();
  g_return_if_fail(data!=NULL);
  if(ssdata->uri)
    g_free(ssdata->uri);
  
  if(ssdata->ip)
    g_free(ssdata->ip);

  if(ssdata->name)
    g_free(ssdata->name);
#if AVAHI==1  
  if(ssdata->resolver)
    g_object_unref( ssdata->resolver);
#endif
  
  g_free(ssdata);
  whiteboard_log_debug_fe();
}
