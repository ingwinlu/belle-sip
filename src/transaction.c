/*
	belle-sip - SIP (RFC3261) library.
    Copyright (C) 2010  Belledonne Communications SARL

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "belle_sip_internal.h"

const char *belle_sip_transaction_state_to_string(belle_sip_transaction_state_t state){
	switch(state){
		case BELLE_SIP_TRANSACTION_INIT:
			return "INIT";
		case BELLE_SIP_TRANSACTION_TRYING:
			return "TRYING";
		case BELLE_SIP_TRANSACTION_CALLING:
			return "CALLING";
		case BELLE_SIP_TRANSACTION_COMPLETED:
			return "COMPLETED";
		case BELLE_SIP_TRANSACTION_CONFIRMED:
			return "CONFIRMED";
		case BELLE_SIP_TRANSACTION_ACCEPTED:
			return "ACCEPTED";
		case BELLE_SIP_TRANSACTION_PROCEEDING:
			return "PROCEEDING";
		case BELLE_SIP_TRANSACTION_TERMINATED:
			return "TERMINATED";
	}
	belle_sip_fatal("Invalid transaction state.");
	return "INVALID";
}

static void belle_sip_transaction_init(belle_sip_transaction_t *t, belle_sip_provider_t *prov, belle_sip_request_t *req){
	t->request=(belle_sip_request_t*)belle_sip_object_ref(req);
	t->provider=prov;
}

static void transaction_destroy(belle_sip_transaction_t *t){
	if (t->request) belle_sip_object_unref(t->request);
	if (t->last_response) belle_sip_object_unref(t->last_response);
	if (t->channel) belle_sip_object_unref(t->channel);
	if (t->branch_id) belle_sip_free(t->branch_id);
	if (t->dialog) belle_sip_object_unref(t->dialog);

}

BELLE_SIP_DECLARE_NO_IMPLEMENTED_INTERFACES(belle_sip_transaction_t);

BELLE_SIP_INSTANCIATE_CUSTOM_VPTR(belle_sip_transaction_t)={
	{
		BELLE_SIP_VPTR_INIT(belle_sip_transaction_t,belle_sip_object_t,FALSE),
		(belle_sip_object_destroy_t) transaction_destroy,
		NULL,/*no clone*/
		NULL,/*no marshall*/
	},
	NULL /*on_terminate*/
};

void *belle_sip_transaction_get_application_data(const belle_sip_transaction_t *t){
	return t->appdata;
}

void belle_sip_transaction_set_application_data(belle_sip_transaction_t *t, void *data){
	t->appdata=data;
}

const char *belle_sip_transaction_get_branch_id(const belle_sip_transaction_t *t){
	return t->branch_id;
}

belle_sip_transaction_state_t belle_sip_transaction_get_state(const belle_sip_transaction_t *t){
	return t->state;
}

void belle_sip_transaction_terminate(belle_sip_transaction_t *t){
	t->state=BELLE_SIP_TRANSACTION_TERMINATED;
	BELLE_SIP_OBJECT_VPTR(t,belle_sip_transaction_t)->on_terminate(t);
	belle_sip_provider_set_transaction_terminated(t->provider,t);
}

belle_sip_request_t *belle_sip_transaction_get_request(const belle_sip_transaction_t *t){
	return t->request;
}

void belle_sip_transaction_notify_timeout(belle_sip_transaction_t *t){
	belle_sip_timeout_event_t ev;
	ev.source=t->provider;
	ev.transaction=t;
	ev.is_server_transaction=BELLE_SIP_OBJECT_IS_INSTANCE_OF(t,belle_sip_server_transaction_t);
	BELLE_SIP_PROVIDER_INVOKE_LISTENERS_FOR_TRANSACTION(t,process_timeout,&ev);
}

belle_sip_dialog_t*  belle_sip_transaction_get_dialog(const belle_sip_transaction_t *t) {
	return t->dialog;
}

void belle_sip_transaction_set_dialog(belle_sip_transaction_t *t, belle_sip_dialog_t *dialog){
	if (dialog) belle_sip_object_ref(dialog);
	if (t->dialog) belle_sip_object_unref(t->dialog); /*to avoid keeping unexpected ref*/
	t->dialog=dialog;
}

/*
 * Server transaction
 */

static void server_transaction_destroy(belle_sip_server_transaction_t *t){
}


BELLE_SIP_DECLARE_NO_IMPLEMENTED_INTERFACES(belle_sip_server_transaction_t);
BELLE_SIP_INSTANCIATE_CUSTOM_VPTR(belle_sip_server_transaction_t)={
	{
		{
			BELLE_SIP_VPTR_INIT(belle_sip_server_transaction_t,belle_sip_transaction_t,FALSE),
			(belle_sip_object_destroy_t) server_transaction_destroy,
			NULL,
			NULL
		},
		NULL
	}
};

void belle_sip_server_transaction_init(belle_sip_server_transaction_t *t, belle_sip_provider_t *prov,belle_sip_request_t *req){
	belle_sip_header_via_t *via=BELLE_SIP_HEADER_VIA(belle_sip_message_get_header((belle_sip_message_t*)req,"via"));
	t->base.branch_id=belle_sip_strdup(belle_sip_header_via_get_branch(via));
	belle_sip_transaction_init((belle_sip_transaction_t*)t,prov,req);
	belle_sip_random_token(t->to_tag,sizeof(t->to_tag));
}

void belle_sip_server_transaction_send_response(belle_sip_server_transaction_t *t, belle_sip_response_t *resp){
	belle_sip_transaction_t *base=(belle_sip_transaction_t*)t;
	belle_sip_header_to_t *to=(belle_sip_header_to_t*)belle_sip_message_get_header((belle_sip_message_t*)resp,"to");
	belle_sip_dialog_t *dialog=base->dialog;
	int status_code;
	
	belle_sip_object_ref(resp);
	if (!base->last_response){
		belle_sip_hop_t hop;
		belle_sip_response_get_return_hop(resp,&hop);
		base->channel=belle_sip_provider_get_channel(base->provider,hop.host, hop.port, hop.transport);
		belle_sip_object_ref(base->channel);
		belle_sip_hop_free(&hop);
	}
	status_code=belle_sip_response_get_status_code(resp);
	if (status_code!=100){
		if (belle_sip_header_to_get_tag(to)==NULL){
			//add a random to tag
			belle_sip_header_to_set_tag(to,t->to_tag);
		}
		/*12.1 Creation of a Dialog

		   Dialogs are created through the generation of non-failure responses
		   to requests with specific methods.  Within this specification, only
		   2xx and 101-199 responses with a To tag, where the request was
		   INVITE, will establish a dialog.*/
		if (dialog && status_code>100 && status_code<300){

			belle_sip_response_fill_for_dialog(resp,base->request);
		}
	}
	if (BELLE_SIP_OBJECT_VPTR(t,belle_sip_server_transaction_t)->send_new_response(t,resp)==0){
		if (base->last_response)
			belle_sip_object_unref(base->last_response);
		base->last_response=resp;
	}
	if (dialog)
		belle_sip_dialog_update(dialog,base->request,resp,TRUE);
}

static void server_transaction_notify(belle_sip_server_transaction_t *t, belle_sip_request_t *req, belle_sip_dialog_t *dialog){
	belle_sip_request_event_t event;

	event.source=t->base.provider;
	event.server_transaction=t;
	event.dialog=dialog;
	event.request=req;
	BELLE_SIP_PROVIDER_INVOKE_LISTENERS_FOR_TRANSACTION(((belle_sip_transaction_t*) t),process_request_event,&event);
}

void belle_sip_server_transaction_on_request(belle_sip_server_transaction_t *t, belle_sip_request_t *req){
	const char *method=belle_sip_request_get_method(req);
	if (strcmp(method,"ACK")==0){
		/*this must be for an INVITE server transaction */
		if (BELLE_SIP_OBJECT_IS_INSTANCE_OF(t,belle_sip_ist_t)){
			belle_sip_ist_t *ist=(belle_sip_ist_t*)t;
			if (belle_sip_ist_process_ack(ist,(belle_sip_message_t*)req)==0){
				belle_sip_dialog_t *dialog=t->base.dialog;
				if (dialog && belle_sip_dialog_handle_ack(dialog,req)==-1)
					dialog=NULL;
				server_transaction_notify(t,req,dialog);
			}
		}else{
			belle_sip_warning("ACK received for non-invite server transaction ?");
		}
	}else if (strcmp(method,"CANCEL")==0){
		server_transaction_notify(t,req,t->base.dialog);
	}else
		BELLE_SIP_OBJECT_VPTR(t,belle_sip_server_transaction_t)->on_request_retransmission(t);
}

/*
 * client transaction
 */



belle_sip_request_t * belle_sip_client_transaction_create_cancel(belle_sip_client_transaction_t *t){
	belle_sip_message_t *orig=(belle_sip_message_t*)t->base.request;
	belle_sip_request_t *req;
	const char *orig_method=belle_sip_request_get_method((belle_sip_request_t*)orig);
	if (strcmp(orig_method,"ACK")==0 || strcmp(orig_method,"INVITE")!=0){
		belle_sip_error("belle_sip_client_transaction_create_cancel() cannot be used for ACK or non-INVITE transactions.");
		return NULL;
	}
	if (t->base.state!=BELLE_SIP_TRANSACTION_PROCEEDING){
		belle_sip_error("belle_sip_client_transaction_create_cancel() can only be used in state BELLE_SIP_TRANSACTION_PROCEEDING"
		               " but current transaction state is %s",belle_sip_transaction_state_to_string(t->base.state));
		return NULL;
	}
	req=belle_sip_request_new();
	belle_sip_request_set_method(req,"CANCEL");
	belle_sip_request_set_uri(req,(belle_sip_uri_t*)belle_sip_object_clone((belle_sip_object_t*)belle_sip_request_get_uri((belle_sip_request_t*)orig)));
	belle_sip_util_copy_headers(orig,(belle_sip_message_t*)req,"via",FALSE);
	belle_sip_util_copy_headers(orig,(belle_sip_message_t*)req,"call-id",FALSE);
	belle_sip_util_copy_headers(orig,(belle_sip_message_t*)req,"from",FALSE);
	belle_sip_util_copy_headers(orig,(belle_sip_message_t*)req,"to",FALSE);
	belle_sip_util_copy_headers(orig,(belle_sip_message_t*)req,"route",TRUE);
	belle_sip_message_add_header((belle_sip_message_t*)req,
		(belle_sip_header_t*)belle_sip_header_cseq_create(
			belle_sip_header_cseq_get_seq_number((belle_sip_header_cseq_t*)belle_sip_message_get_header(orig,"cseq")),
		    "CANCEL"));
	return req;
}


int belle_sip_client_transaction_send_request(belle_sip_client_transaction_t *t){
	return belle_sip_client_transaction_send_request_to(t,NULL);

}
int belle_sip_client_transaction_send_request_to(belle_sip_client_transaction_t *t,belle_sip_uri_t* outbound_proxy) {
	belle_sip_hop_t* hop;
	belle_sip_channel_t *chan;
	belle_sip_provider_t *prov=t->base.provider;
	int result=-1;
	
	if (t->base.state!=BELLE_SIP_TRANSACTION_INIT){
		belle_sip_error("belle_sip_client_transaction_send_request: bad state.");
		return -1;
	}
	/*store preset route for future use by refresher*/
	t->preset_route=outbound_proxy;
	if (t->preset_route) belle_sip_object_ref(t->preset_route);
	if (outbound_proxy) {
		hop=belle_sip_hop_create(	belle_sip_uri_get_transport_param(outbound_proxy)
									,belle_sip_uri_get_host(outbound_proxy)
									,belle_sip_uri_get_listening_port(outbound_proxy));
	} else {
		hop = belle_sip_stack_create_next_hop(prov->stack,t->base.request);
	}
	belle_sip_provider_add_client_transaction(t->base.provider,t); /*add it in any case*/
	chan=belle_sip_provider_get_channel(prov,hop->host, hop->port, hop->transport);
	if (chan){
		belle_sip_object_ref(chan);
		belle_sip_channel_add_listener(chan,BELLE_SIP_CHANNEL_LISTENER(t));
		t->base.channel=chan;
		if (belle_sip_channel_get_state(chan)==BELLE_SIP_CHANNEL_INIT){
			belle_sip_message("belle_sip_client_transaction_send_request(): waiting channel to be ready");
			belle_sip_channel_prepare(chan);
			/*the channel will notify us when it is ready*/
		} else {
			/*otherwise we can send immediately*/
			BELLE_SIP_OBJECT_VPTR(t,belle_sip_client_transaction_t)->send_request(t);
		}
		result=0;
	}else {
		belle_sip_error("belle_sip_client_transaction_send_request(): no channel available");
		belle_sip_transaction_terminate(BELLE_SIP_TRANSACTION(t));
		result=-1;
	}
	belle_sip_hop_free(hop);
	return result;
}

static unsigned int should_dialog_be_created(belle_sip_client_transaction_t *t, belle_sip_response_t *resp){
	belle_sip_request_t* req = belle_sip_transaction_get_request(BELLE_SIP_TRANSACTION(t));
	const char* method = belle_sip_request_get_method(req);
	int status_code = belle_sip_response_get_status_code(resp);
	return status_code>=180 && status_code<300 && (strcmp(method,"INVITE")==0 || strcmp(method,"SUBSCRIBE")==0);
}

void belle_sip_client_transaction_notify_response(belle_sip_client_transaction_t *t, belle_sip_response_t *resp){
	belle_sip_transaction_t *base=(belle_sip_transaction_t*)t;
	belle_sip_request_t* req = belle_sip_transaction_get_request(BELLE_SIP_TRANSACTION(t));
	const char* method = belle_sip_request_get_method(req);
	belle_sip_response_event_t event;
	belle_sip_dialog_t *dialog=base->dialog;
	int status_code =  belle_sip_response_get_status_code(resp);
	if (base->last_response)
		belle_sip_object_unref(base->last_response);
	base->last_response=(belle_sip_response_t*)belle_sip_object_ref(resp);

	if (dialog){
		if (status_code>=200 && status_code<300
			&& strcmp(method,"INVITE")==0
			&& (dialog->state==BELLE_SIP_DIALOG_EARLY || dialog->state==BELLE_SIP_DIALOG_CONFIRMED)){
			/*make sure this response matches the current dialog, or creates a new one*/
			if (!belle_sip_dialog_match(dialog,(belle_sip_message_t*)resp,FALSE)){
				dialog=belle_sip_provider_create_dialog_internal(t->base.provider,BELLE_SIP_TRANSACTION(t),FALSE);/*belle_sip_dialog_new(base);*/
				if (dialog){
					/*copy userdata to avoid application from being lost*/
					belle_sip_dialog_set_application_data(dialog,belle_sip_dialog_get_application_data(base->dialog));
					belle_sip_message("Handling response creating a new dialog !");
				}
			}
		}
	} else if (should_dialog_be_created(t,resp)) {
		dialog=belle_sip_provider_create_dialog_internal(t->base.provider,BELLE_SIP_TRANSACTION(t),FALSE);
	}

	if (dialog)
		belle_sip_dialog_update(dialog,base->request,resp,FALSE);

	event.source=base->provider;
	event.client_transaction=t;
	event.dialog=dialog;
	event.response=(belle_sip_response_t*)resp;
	BELLE_SIP_PROVIDER_INVOKE_LISTENERS_FOR_TRANSACTION(((belle_sip_transaction_t*)t),process_response_event,&event);
	/*check that 200Ok for INVITEs have been acknowledged by listener*/
	if (dialog && strcmp(method,"INVITE")==0)
		belle_sip_dialog_check_ack_sent(dialog);
}


void belle_sip_client_transaction_add_response(belle_sip_client_transaction_t *t, belle_sip_response_t *resp){
	BELLE_SIP_OBJECT_VPTR(t,belle_sip_client_transaction_t)->on_response(t,resp);
}

static void client_transaction_destroy(belle_sip_client_transaction_t *t ){
	if (t->preset_route) belle_sip_object_unref(t->preset_route);
}

static void on_channel_state_changed(belle_sip_channel_listener_t *l, belle_sip_channel_t *chan, belle_sip_channel_state_t state){
	belle_sip_client_transaction_t *t=(belle_sip_client_transaction_t*)l;
	belle_sip_io_error_event_t ev;
	belle_sip_message("transaction [%p] channel state changed to [%s]"
						,t
						,belle_sip_channel_state_to_string(state));
	switch(state){
		case BELLE_SIP_CHANNEL_READY:
			BELLE_SIP_OBJECT_VPTR(t,belle_sip_client_transaction_t)->send_request(t);
		break;
		case BELLE_SIP_CHANNEL_DISCONNECTED:
		case BELLE_SIP_CHANNEL_ERROR:

			ev.transport=belle_sip_channel_get_transport_name(chan);
			ev.source=BELLE_SIP_OBJECT(t);
			ev.port=chan->peer_port;
			ev.host=chan->peer_name;
			if (belle_sip_transaction_get_state(BELLE_SIP_TRANSACTION(t))!=BELLE_SIP_TRANSACTION_COMPLETED
				&& belle_sip_transaction_get_state(BELLE_SIP_TRANSACTION(t))!=BELLE_SIP_TRANSACTION_CONFIRMED
				&& belle_sip_transaction_get_state(BELLE_SIP_TRANSACTION(t))!=BELLE_SIP_TRANSACTION_ACCEPTED
				&& belle_sip_transaction_get_state(BELLE_SIP_TRANSACTION(t))!=BELLE_SIP_TRANSACTION_TERMINATED) {
				BELLE_SIP_PROVIDER_INVOKE_LISTENERS_FOR_TRANSACTION(((belle_sip_transaction_t*)t),process_io_error,&ev);
			}
			if (belle_sip_transaction_get_state(BELLE_SIP_TRANSACTION(t))!=BELLE_SIP_TRANSACTION_TERMINATED) /*avoid double notification*/
				belle_sip_transaction_terminate(BELLE_SIP_TRANSACTION(t));
		break;
		default:
			/*ignored*/
		break;
	}
}

BELLE_SIP_IMPLEMENT_INTERFACE_BEGIN(belle_sip_client_transaction_t,belle_sip_channel_listener_t)
on_channel_state_changed,
NULL,
NULL
BELLE_SIP_IMPLEMENT_INTERFACE_END

BELLE_SIP_DECLARE_IMPLEMENTED_INTERFACES_1(belle_sip_client_transaction_t, belle_sip_channel_listener_t);
BELLE_SIP_INSTANCIATE_CUSTOM_VPTR(belle_sip_client_transaction_t)={
	{
		{
			BELLE_SIP_VPTR_INIT(belle_sip_client_transaction_t,belle_sip_transaction_t,FALSE),
			(belle_sip_object_destroy_t)client_transaction_destroy,
			NULL,
			NULL
		},
		NULL
	},
	NULL,
	NULL
};

void belle_sip_client_transaction_init(belle_sip_client_transaction_t *obj, belle_sip_provider_t *prov, belle_sip_request_t *req){
	belle_sip_header_via_t *via=BELLE_SIP_HEADER_VIA(belle_sip_message_get_header((belle_sip_message_t*)req,"via"));
	char token[BELLE_SIP_BRANCH_ID_LENGTH];

	if (!via){
		belle_sip_fatal("belle_sip_client_transaction_init(): No via in request.");
	}
	
	if (strcmp(belle_sip_request_get_method(req),"CANCEL")!=0){
		obj->base.branch_id=belle_sip_strdup_printf(BELLE_SIP_BRANCH_MAGIC_COOKIE ".%s",belle_sip_random_token(token,sizeof(token)));
		belle_sip_header_via_set_branch(via,obj->base.branch_id);
	}else{
		obj->base.branch_id=belle_sip_strdup(belle_sip_header_via_get_branch(via));
	}
	belle_sip_transaction_init((belle_sip_transaction_t*)obj, prov,req);
}

belle_sip_refresher_t* belle_sip_client_transaction_create_refresher(belle_sip_client_transaction_t *t) {
	belle_sip_refresher_t* refresher = belle_sip_refresher_new(t);
	if (refresher) {
		belle_sip_refresher_start(refresher);
	}
	return refresher;
}

belle_sip_request_t* belle_sip_client_transaction_create_authenticated_request(belle_sip_client_transaction_t *t) {
	belle_sip_request_t* req=BELLE_SIP_REQUEST(belle_sip_object_clone(BELLE_SIP_OBJECT(belle_sip_transaction_get_request(BELLE_SIP_TRANSACTION(t)))));
	belle_sip_header_cseq_t* cseq=belle_sip_message_get_header_by_type(req,belle_sip_header_cseq_t);
	belle_sip_header_cseq_set_seq_number(cseq,belle_sip_header_cseq_get_seq_number(cseq)+1);
	if (belle_sip_transaction_get_state(BELLE_SIP_TRANSACTION(t)) != BELLE_SIP_TRANSACTION_COMPLETED
		&& belle_sip_transaction_get_state(BELLE_SIP_TRANSACTION(t)) != BELLE_SIP_TRANSACTION_TERMINATED) {
		belle_sip_error("Invalid state [%s] for transaction [%p], should be BELLE_SIP_TRANSACTION_COMPLETED|BELLE_SIP_TRANSACTION_TERMINATED"
					,belle_sip_transaction_state_to_string(belle_sip_transaction_get_state(BELLE_SIP_TRANSACTION(t)))
					,t);
		return NULL;
	}
	/*remove auth headers*/
	belle_sip_message_remove_header(BELLE_SIP_MESSAGE(req),BELLE_SIP_AUTHORIZATION);
	belle_sip_message_remove_header(BELLE_SIP_MESSAGE(req),BELLE_SIP_PROXY_AUTHORIZATION);

	/*put auth header*/
	belle_sip_provider_add_authorization(t->base.provider,req,t->base.last_response,NULL);
	return req;
}

