/**
 * @file configuration_manager.c
 * 
 * @brief Implementation of configuration_manager_t.
 * 
 */

/*
 * Copyright (C) 2005 Jan Hutter, Martin Willi
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <stdlib.h>

#include "configuration_manager.h"

#include <types.h>
#include <daemon.h>
#include <utils/allocator.h>


typedef struct preshared_secret_entry_t preshared_secret_entry_t;

/**
 * A preshared secret entry combines an identifier and a 
 * preshared secret.
 */
struct preshared_secret_entry_t {

	/**
	 * Identification.
	 */
	identification_t *identification;
	
	/**
	 * Preshared secret as chunk_t. The NULL termination is not included.
	 */	
	chunk_t preshared_secret;
};


typedef struct rsa_private_key_entry_t rsa_private_key_entry_t;

/**
 * Entry for a rsa private key.
 */
struct rsa_private_key_entry_t {

	/**
	 * Identification.
	 */
	identification_t *identification;
	
	/**
	 * Private key.
	 */	
	rsa_private_key_t* private_key;
};

typedef struct rsa_public_key_entry_t rsa_public_key_entry_t;

/**
 * Entry for a rsa private key.
 */
struct rsa_public_key_entry_t {

	/**
	 * Identification.
	 */
	identification_t *identification;
	
	/**
	 * Private key.
	 */	
	rsa_public_key_t* public_key;
};

typedef struct configuration_entry_t configuration_entry_t;

/* A configuration entry combines a configuration name with a init and sa 
 * configuration represented as init_config_t and sa_config_t objects.
 * 
 * @b Constructors:
 *  - configuration_entry_create()
 */
struct configuration_entry_t {
	
	/**
	 * Configuration name.
	 * 
	 */
	char *name;
	
	/**
	 * Configuration for IKE_SA_INIT exchange.
	 */
	init_config_t *init_config;

	/**
	 * Configuration for all phases after IKE_SA_INIT exchange.
	 */
	sa_config_t *sa_config;
	
	/**
	 * Destroys a configuration_entry_t
	 * 
	 * @param this				calling object
	 */
	void (*destroy) (configuration_entry_t *this);
};

/**
 * Implementation of configuration_entry_t.destroy.
 */
static void configuration_entry_destroy (configuration_entry_t *this)
{
	allocator_free(this->name);
	allocator_free(this);
}

/**
 * @brief Creates a configuration_entry_t object.
 * 
 * @param name 			name of the configuration entry (gets copied)
 * @param init_config	object of type init_config_t
 * @param sa_config		object of type sa_config_t
 */
configuration_entry_t * configuration_entry_create(char * name, init_config_t * init_config, sa_config_t * sa_config)
{
	configuration_entry_t *entry = allocator_alloc_thing(configuration_entry_t);

	/* functions */
	entry->destroy = configuration_entry_destroy;

	/* private data */
	entry->init_config = init_config;
	entry->sa_config = sa_config;
	entry->name = allocator_alloc(strlen(name) + 1);
	strcpy(entry->name,name);
	return entry;
}

typedef struct private_configuration_manager_t private_configuration_manager_t;

/**
 * Private data of an configuration_manager_t object.
 */
struct private_configuration_manager_t {

	/**
	 * Public part of configuration_manager_t object.
	 */
	configuration_manager_t public;

	/**
	 * Holding all configurations.
	 */
	linked_list_t *configurations;

	/**
	 * Holding all managed init_configs.
	 */
	linked_list_t *init_configs;

	/**
	 * Holding all managed init_configs.
	 */
	linked_list_t *sa_configs;
	
	/**
	 * Holding all managed preshared secrets.
	 */
	linked_list_t *preshared_secrets;
	
	/**
	 * Holding all managed private secrets.
	 */
	linked_list_t *rsa_private_keys;
	
	/**
	 * Holding all managed public secrets.
	 */
	linked_list_t *rsa_public_keys;

	/**
	 * Assigned logger_t object.
	 */
	logger_t *logger;
	
	/**
	 * Max number of requests to be retransmitted.
	 * 0 for infinite.
	 */	
	u_int32_t max_retransmit_count;
	
	/**
	 * First retransmit timeout in ms.
	 */
	u_int32_t first_retransmit_timeout;
	
	/**
	 * Timeout in ms after that time a IKE_SA gets deleted.
	 */
	u_int32_t half_open_ike_sa_timeout;

	/**
	 * Adds a new IKE_SA configuration.
	 * 
	 * @param this				calling object
	 * @param name				name for the configuration
	 * @param init_config		init_config_t object
	 * @param sa_config			sa_config_t object
	 */
	void (*add_new_configuration) (private_configuration_manager_t *this, char *name, init_config_t *init_config, sa_config_t *sa_config);
	
	/**
	 * Adds a new preshared secret.
	 * 
	 * @param this				calling object
	 * @param type				type of identification
	 * @param id_string			identification as string
	 * @param preshared_secret	preshared secret as string
	 */
	void (*add_new_preshared_secret) (private_configuration_manager_t *this,id_type_t type, char *id_string, char *preshared_secret);
	
	/**
	 * Adds a new rsa private key.
	 * 
	 * @param this				calling object
	 * @param type				type of identification
	 * @param id_string			identification as string
	 * @param key_pos			location of key
	 * @param key_len			length of key
	 */
	void (*add_new_rsa_private_key) (private_configuration_manager_t *this,id_type_t type, char *id_string, u_int8_t *key_pos, size_t key_len);
	
	/**
	 * Adds a new rsa public key.
	 * 
	 * @param this				calling object
	 * @param type				type of identification
	 * @param id_string			identification as string
	 * @param key_pos			location of key
	 * @param key_len			length of key
	 */
	void (*add_new_rsa_public_key) (private_configuration_manager_t *this,id_type_t type, char *id_string, u_int8_t *key_pos, size_t key_len);
	
	/**
	 * Load default configuration.
	 * 
	 * @param this				calling object
	 */
	void (*load_default_config) (private_configuration_manager_t *this);
};


u_int8_t public_key_1[];
u_int8_t private_key_1[];
u_int8_t public_key_2[];
u_int8_t private_key_2[];

/**
 * Implementation of private_configuration_manager_t.load_default_config.
 */
static void load_default_config (private_configuration_manager_t *this)
{
	init_config_t *init_config;
	ike_proposal_t proposals;
	child_proposal_t *child_proposal;
	sa_config_t *sa_config;
	traffic_selector_t *ts;
	
	init_config = init_config_create("0.0.0.0","127.0.0.1",IKEV2_UDP_PORT,IKEV2_UDP_PORT);
	
	ts = traffic_selector_create_from_string(1, TS_IPV4_ADDR_RANGE, "0.0.0.0", 0, "255.255.255.255", 65535);
	
	proposals.encryption_algorithm = ENCR_AES_CBC;
	proposals.encryption_algorithm_key_length = 16;
	proposals.integrity_algorithm = AUTH_HMAC_MD5_96;
	proposals.integrity_algorithm_key_length = 16;
	proposals.pseudo_random_function = PRF_HMAC_MD5;
	proposals.pseudo_random_function_key_length = 16;
	proposals.diffie_hellman_group = MODP_1024_BIT;

	init_config->add_proposal(init_config,1,proposals);
								  
	sa_config = sa_config_create(ID_IPV4_ADDR, "127.0.0.1", 
								 ID_IPV4_ADDR, "127.0.0.1",
								 RSA_DIGITAL_SIGNATURE,
								 30000);

	sa_config->add_traffic_selector_initiator(sa_config,ts);
	sa_config->add_traffic_selector_responder(sa_config,ts);
	
	ts->destroy(ts);
	
	/* ah and esp prop */
	child_proposal = child_proposal_create(1);
	
	//child_proposal->add_algorithm(child_proposal, AH, INTEGRITY_ALGORITHM, AUTH_HMAC_SHA1_96, 20);
	//child_proposal->add_algorithm(child_proposal, AH, DIFFIE_HELLMAN_GROUP, MODP_1024_BIT, 0);
	//child_proposal->add_algorithm(child_proposal, AH, EXTENDED_SEQUENCE_NUMBERS, NO_EXT_SEQ_NUMBERS, 0);

	child_proposal->add_algorithm(child_proposal, ESP, ENCRYPTION_ALGORITHM, ENCR_AES_CBC, 16);
	child_proposal->add_algorithm(child_proposal, ESP, INTEGRITY_ALGORITHM, AUTH_HMAC_SHA1_96, 20);
	child_proposal->add_algorithm(child_proposal, ESP, DIFFIE_HELLMAN_GROUP, MODP_1024_BIT, 0);
	child_proposal->add_algorithm(child_proposal, ESP, EXTENDED_SEQUENCE_NUMBERS, NO_EXT_SEQ_NUMBERS, 0);
	
	sa_config->add_proposal(sa_config, child_proposal);

	this->add_new_configuration(this,"localhost",init_config,sa_config);
	

	//this->add_new_preshared_secret(this,ID_IPV4_ADDR, "192.168.1.2","verschluesselt");
	
	this->add_new_rsa_public_key(this,ID_IPV4_ADDR, "127.0.0.1", public_key_1, 256);
	//this->add_new_rsa_public_key(this,ID_IPV4_ADDR, "192.168.1.1", public_key_2, 256);
	this->add_new_rsa_private_key(this,ID_IPV4_ADDR, "127.0.0.1", private_key_1, 1024);
	//this->add_new_rsa_private_key(this,ID_IPV4_ADDR, "192.168.1.1", private_key_2, 1024);
}

/**
 * Implementation of configuration_manager_t.get_init_config_for_host.
 */
static status_t get_init_config_for_host (private_configuration_manager_t *this, host_t *my_host, host_t *other_host,init_config_t **init_config)
{
	iterator_t *iterator;
	status_t status = NOT_FOUND;
	
	iterator = this->configurations->create_iterator(this->configurations,TRUE);
	
	this->logger->log(this->logger, CONTROL|LEVEL1, "getting config for hosts %s - %s", 
						my_host->get_address(my_host), other_host->get_address(other_host));
	
	while (iterator->has_next(iterator))
	{
		configuration_entry_t *entry;
		host_t *config_my_host;
		host_t *config_other_host;
		
		iterator->current(iterator,(void **) &entry);

		config_my_host = entry->init_config->get_my_host(entry->init_config);
		config_other_host = entry->init_config->get_other_host(entry->init_config);

		/* first check if ip is equal */
		if(config_other_host->ip_is_equal(config_other_host,other_host))
		{
			this->logger->log(this->logger, CONTROL|LEVEL2, "config entry with remote host %s", 
						config_other_host->get_address(config_other_host));
			/* could be right one, check my_host for default route*/
			if (config_my_host->is_default_route(config_my_host))
			{
				*init_config = entry->init_config;
				status = SUCCESS;
				break;
			}
			/* check now if host informations are the same */
			else if (config_my_host->ip_is_equal(config_my_host,my_host))
			{
				*init_config = entry->init_config;
				status = SUCCESS;
				break;
			}
			
		}
		/* Then check for wildcard hosts!
		 * TODO
		 * actually its only checked if other host with default route can be found! */
		else if (config_other_host->is_default_route(config_other_host))
		{
			/* could be right one, check my_host for default route*/
			if (config_my_host->is_default_route(config_my_host))
			{
				*init_config = entry->init_config;
				status = SUCCESS;
				break;
			}
			/* check now if host informations are the same */
			else if (config_my_host->ip_is_equal(config_my_host,my_host))
			{
				*init_config = entry->init_config;
				status = SUCCESS;
				break;
			}
		}
	}
	
	iterator->destroy(iterator);
	
	return status;
}

/**
 * Implementation of configuration_manager_t.get_init_config_for_name.
 */
static status_t get_init_config_for_name (private_configuration_manager_t *this, char *name, init_config_t **init_config)
{
	iterator_t *iterator;
	status_t status = NOT_FOUND;
	
	iterator = this->configurations->create_iterator(this->configurations,TRUE);
	
	while (iterator->has_next(iterator))
	{
		configuration_entry_t *entry;
		iterator->current(iterator,(void **) &entry);

		if (strcmp(entry->name,name) == 0)
		{

			/* found configuration */
			*init_config = entry->init_config;
			status = SUCCESS;
			break;
		}
	}
	
	iterator->destroy(iterator);
	
	return status;
}
	
/**
 * Implementation of configuration_manager_t.get_sa_config_for_name.
 */
static status_t get_sa_config_for_name (private_configuration_manager_t *this, char *name, sa_config_t **sa_config)
{
	iterator_t *iterator;
	status_t status = NOT_FOUND;
	
	iterator = this->configurations->create_iterator(this->configurations,TRUE);
	
	while (iterator->has_next(iterator))
	{
		configuration_entry_t *entry;
		iterator->current(iterator,(void **) &entry);

		if (strcmp(entry->name,name) == 0)
		{
			/* found configuration */
			*sa_config = entry->sa_config;
			status = SUCCESS;
			break;
		}
	}
	
	iterator->destroy(iterator);
	
	return status;
}

/**
 * Implementation of configuration_manager_t.get_sa_config_for_init_config_and_id.
 */
static status_t get_sa_config_for_init_config_and_id (private_configuration_manager_t *this, init_config_t *init_config, identification_t *other_id, identification_t *my_id,sa_config_t **sa_config)
{	
	iterator_t *iterator;
	status_t status = NOT_FOUND;
	
	iterator = this->configurations->create_iterator(this->configurations,TRUE);
	
	while (iterator->has_next(iterator))
	{
		configuration_entry_t *entry;
		iterator->current(iterator,(void **) &entry);

		if (entry->init_config == init_config)
		{
			identification_t *config_my_id = entry->sa_config->get_my_id(entry->sa_config);
			identification_t *config_other_id = entry->sa_config->get_other_id(entry->sa_config);

			/* host informations seem to be the same */
			if (config_other_id->equals(config_other_id,other_id))
			{
				/* other ids seems to match */
				
				if (my_id == NULL)
				{
					/* first matching one is selected */
					
					/* TODO priorize found entries */
					*sa_config = entry->sa_config;
					status = SUCCESS;
					break;
				}

				if (config_my_id->equals(config_my_id,my_id))
				{
					*sa_config = entry->sa_config;
					status = SUCCESS;
					break;
				}

			}
		}
	}
	
	iterator->destroy(iterator);
	
	return status;
}

/**
 * Implementation of private_configuration_manager_t.add_new_configuration.
 */
static void add_new_configuration (private_configuration_manager_t *this, char *name, init_config_t *init_config, sa_config_t *sa_config)
{
	iterator_t *iterator;
	bool found;
	
	iterator = this->init_configs->create_iterator(this->init_configs,TRUE);
	found = FALSE;
	while (iterator->has_next(iterator))
	{
		init_config_t *found_init_config;
		iterator->current(iterator,(void **) &found_init_config);
		if (init_config == found_init_config)
		{
			found = TRUE;
			break;
		}
	}
	iterator->destroy(iterator);
	if (!found)
	{
		this->init_configs->insert_first(this->init_configs,init_config);
	}
	
	iterator = this->sa_configs->create_iterator(this->sa_configs,TRUE);
	found = FALSE;
	while (iterator->has_next(iterator))
	{
		sa_config_t *found_sa_config;
		iterator->current(iterator,(void **) &found_sa_config);
		if (sa_config == found_sa_config)
		{
			found = TRUE;
			break;
		}
	}
	iterator->destroy(iterator);
	if (!found)
	{
		this->sa_configs->insert_first(this->sa_configs,sa_config);
	}

	this->configurations->insert_last(this->configurations,configuration_entry_create(name,init_config,sa_config));
}

/**
 * Implementation of private_configuration_manager_t.add_new_preshared_secret.
 */
static void add_new_preshared_secret (private_configuration_manager_t *this,id_type_t type, char *id_string, char *preshared_secret)
{
	preshared_secret_entry_t *entry = allocator_alloc_thing(preshared_secret_entry_t);
	
	entry->identification = identification_create_from_string(type,id_string);
	entry->preshared_secret.len = strlen(preshared_secret) + 1;
	entry->preshared_secret.ptr = allocator_alloc(entry->preshared_secret.len);
	memcpy(entry->preshared_secret.ptr,preshared_secret,entry->preshared_secret.len);
	
	this->preshared_secrets->insert_last(this->preshared_secrets,entry);
}

/**
 * Implementation of private_configuration_manager_t.add_new_preshared_secret.
 */
static void add_new_rsa_public_key (private_configuration_manager_t *this, id_type_t type, char *id_string, u_int8_t* key_pos, size_t key_len)
{
	chunk_t key;
	key.ptr = key_pos;
	key.len = key_len;
	
	rsa_public_key_entry_t *entry = allocator_alloc_thing(rsa_public_key_entry_t);
	
	entry->identification = identification_create_from_string(type,id_string);
	entry->public_key = rsa_public_key_create();
	entry->public_key->set_key(entry->public_key, key);
	
	this->rsa_public_keys->insert_last(this->rsa_public_keys, entry);
}

/**
 * Implementation of private_configuration_manager_t.add_new_preshared_secret.
 */
static void add_new_rsa_private_key (private_configuration_manager_t *this, id_type_t type, char *id_string, u_int8_t* key_pos, size_t key_len)
{
	chunk_t key;
	key.ptr = key_pos;
	key.len = key_len;
	
	rsa_private_key_entry_t *entry = allocator_alloc_thing(rsa_private_key_entry_t);
	
	entry->identification = identification_create_from_string(type,id_string);
	entry->private_key = rsa_private_key_create();
	entry->private_key->set_key(entry->private_key, key);
	
	this->rsa_private_keys->insert_last(this->rsa_private_keys, entry);
}

/**
 * Implementation of configuration_manager_t.get_shared_secret.
 */
static status_t get_shared_secret(private_configuration_manager_t *this, identification_t *identification, chunk_t *preshared_secret)
{
	iterator_t *iterator;
	
	iterator = this->preshared_secrets->create_iterator(this->preshared_secrets,TRUE);
	while (iterator->has_next(iterator))
	{
		preshared_secret_entry_t *entry;
		iterator->current(iterator,(void **) &entry);
		if (entry->identification->equals(entry->identification,identification))
		{
			*preshared_secret = entry->preshared_secret;
			iterator->destroy(iterator);
			return SUCCESS;
		}
	}
	iterator->destroy(iterator);
	return NOT_FOUND;
}

/**
 * Implementation of configuration_manager_t.get_shared_secret.
 */
static status_t get_rsa_public_key(private_configuration_manager_t *this, identification_t *identification, rsa_public_key_t **public_key)
{
	iterator_t *iterator;
	
	iterator = this->rsa_public_keys->create_iterator(this->rsa_public_keys,TRUE);
	while (iterator->has_next(iterator))
	{
		rsa_public_key_entry_t *entry;
		iterator->current(iterator,(void **) &entry);
		if (entry->identification->equals(entry->identification,identification))
		{
			*public_key = entry->public_key;
			iterator->destroy(iterator);
			return SUCCESS;
		}
	}
	iterator->destroy(iterator);
	return NOT_FOUND;
}

/**
 * Implementation of configuration_manager_t.get_shared_secret.
 */
static status_t get_rsa_private_key(private_configuration_manager_t *this, identification_t *identification, rsa_private_key_t **private_key)
{
	iterator_t *iterator;
	
	iterator = this->rsa_private_keys->create_iterator(this->rsa_private_keys,TRUE);
	while (iterator->has_next(iterator))
	{
		rsa_private_key_entry_t *entry;
		iterator->current(iterator,(void **) &entry);
		if (entry->identification->equals(entry->identification,identification))
		{
			*private_key = entry->private_key;
			iterator->destroy(iterator);
			return SUCCESS;
		}
	}
	iterator->destroy(iterator);
	return NOT_FOUND;
}

/**
 * Implementation of configuration_manager_t.get_retransmit_timeout.
 */
static status_t get_retransmit_timeout (private_configuration_manager_t *this, u_int32_t retransmit_count, u_int32_t *timeout)
{
	int new_timeout = this->first_retransmit_timeout, i;
	if ((retransmit_count > this->max_retransmit_count) && (this->max_retransmit_count != 0))
	{
		return FAILED;
	}
	

	for (i = 0; i < retransmit_count; i++)
	{
		new_timeout *= 2;
	}
	
	*timeout = new_timeout;
	
	return SUCCESS;
}

/**
 * Implementation of configuration_manager_t.get_half_open_ike_sa_timeout.
 */
static u_int32_t get_half_open_ike_sa_timeout (private_configuration_manager_t *this)
{
	return this->half_open_ike_sa_timeout;
}

/**
 * Implementation of configuration_manager_t.destroy.
 */
static void destroy(private_configuration_manager_t *this)
{
	this->logger->log(this->logger,CONTROL | LEVEL1, "Going to destroy configuration manager ");

	this->logger->log(this->logger,CONTROL | LEVEL2, "Destroy configuration entries");
	while (this->configurations->get_count(this->configurations) > 0)
	{
		configuration_entry_t *entry;
		this->configurations->remove_first(this->configurations,(void **) &entry);
		entry->destroy(entry);
	}
	this->configurations->destroy(this->configurations);

	this->logger->log(this->logger,CONTROL | LEVEL2, "Destroy sa_config_t objects");	
	while (this->sa_configs->get_count(this->sa_configs) > 0)
	{
		sa_config_t *sa_config;
		this->sa_configs->remove_first(this->sa_configs,(void **) &sa_config);
		sa_config->destroy(sa_config);
	}

	this->sa_configs->destroy(this->sa_configs);
	
	this->logger->log(this->logger,CONTROL | LEVEL2, "Destroy init_config_t objects");
	while (this->init_configs->get_count(this->init_configs) > 0)
	{
		init_config_t *init_config;
		this->init_configs->remove_first(this->init_configs,(void **) &init_config);
		init_config->destroy(init_config);
	}
	this->init_configs->destroy(this->init_configs);
	
	while (this->preshared_secrets->get_count(this->preshared_secrets) > 0)
	{
		preshared_secret_entry_t *entry;
		this->preshared_secrets->remove_first(this->preshared_secrets,(void **) &entry);
		entry->identification->destroy(entry->identification);
		allocator_free_chunk(&(entry->preshared_secret));
		allocator_free(entry);
	}
	this->preshared_secrets->destroy(this->preshared_secrets);

	this->logger->log(this->logger,CONTROL | LEVEL2, "Destroy rsa private keys");	
	while (this->rsa_private_keys->get_count(this->rsa_private_keys) > 0)
	{
		rsa_private_key_entry_t *entry;
		this->rsa_private_keys->remove_first(this->rsa_private_keys,(void **) &entry);
		entry->identification->destroy(entry->identification);
		entry->private_key->destroy(entry->private_key);
		allocator_free(entry);
	}
	this->rsa_private_keys->destroy(this->rsa_private_keys);

	this->logger->log(this->logger,CONTROL | LEVEL2, "Destroy rsa public keys");
	while (this->rsa_public_keys->get_count(this->rsa_public_keys) > 0)
	{
		rsa_public_key_entry_t *entry;
		this->rsa_public_keys->remove_first(this->rsa_public_keys,(void **) &entry);
		entry->identification->destroy(entry->identification);
		entry->public_key->destroy(entry->public_key);
		allocator_free(entry);
	}
	this->rsa_public_keys->destroy(this->rsa_public_keys);
		
	this->logger->log(this->logger,CONTROL | LEVEL2, "Destroy assigned logger");
	charon->logger_manager->destroy_logger(charon->logger_manager,this->logger);
	allocator_free(this);
}

/*
 * Described in header-file
 */
configuration_manager_t *configuration_manager_create(u_int32_t first_retransmit_timeout,u_int32_t max_retransmit_count, u_int32_t half_open_ike_sa_timeout)
{
	private_configuration_manager_t *this = allocator_alloc_thing(private_configuration_manager_t);

	/* public functions */
	this->public.destroy = (void(*)(configuration_manager_t*))destroy;
	this->public.get_init_config_for_name = (status_t (*) (configuration_manager_t *, char *, init_config_t **)) get_init_config_for_name;
	this->public.get_init_config_for_host = (status_t (*) (configuration_manager_t *, host_t *, host_t *,init_config_t **)) get_init_config_for_host;
	this->public.get_sa_config_for_name =(status_t (*) (configuration_manager_t *, char *, sa_config_t **)) get_sa_config_for_name;
	this->public.get_sa_config_for_init_config_and_id =(status_t (*) (configuration_manager_t *, init_config_t *, identification_t *, identification_t *,sa_config_t **)) get_sa_config_for_init_config_and_id;
	this->public.get_retransmit_timeout = (status_t (*) (configuration_manager_t *, u_int32_t retransmit_count, u_int32_t *timeout))get_retransmit_timeout;
	this->public.get_half_open_ike_sa_timeout = (u_int32_t (*) (configuration_manager_t *)) get_half_open_ike_sa_timeout;
	this->public.get_shared_secret = (status_t (*) (configuration_manager_t *, identification_t *, chunk_t *))get_shared_secret;
	this->public.get_rsa_private_key = (status_t (*) (configuration_manager_t *, identification_t *, rsa_private_key_t**))get_rsa_private_key;
	this->public.get_rsa_public_key = (status_t (*) (configuration_manager_t *, identification_t *, rsa_public_key_t**))get_rsa_public_key;
	
	/* private functions */
	this->load_default_config = load_default_config;
	this->add_new_configuration = add_new_configuration;
	this->add_new_preshared_secret = add_new_preshared_secret;
	this->add_new_rsa_public_key = add_new_rsa_public_key;
	this->add_new_rsa_private_key = add_new_rsa_private_key;
	
	/* private variables */
	this->logger = charon->logger_manager->create_logger(charon->logger_manager,CONFIGURATION_MANAGER,NULL);
	this->configurations = linked_list_create();
	this->sa_configs = linked_list_create();
	this->init_configs = linked_list_create();
	this->preshared_secrets = linked_list_create();
	this->rsa_private_keys = linked_list_create();
	this->rsa_public_keys = linked_list_create();
	this->max_retransmit_count = max_retransmit_count;
	this->first_retransmit_timeout = first_retransmit_timeout;
	this->half_open_ike_sa_timeout = half_open_ike_sa_timeout;
	
	this->load_default_config(this);

	return (&this->public);
}


u_int8_t public_key_1[] = {
	0xD4,0x8D,0x40,0x8E,0xBD,0xFC,0x6D,0xE9,0xDB,0x1C,0xD2,0x21,0x19,0x37,0x6B,0xE2,
	0xDC,0xCE,0x74,0xA2,0x63,0xF6,0xD8,0x8D,0xAF,0x1C,0xC0,0xFF,0x07,0x3F,0xFB,0x52,
	0x59,0x45,0x01,0x10,0x35,0xA9,0xB8,0x16,0x69,0x31,0x19,0x4F,0xDD,0x66,0xAD,0xAC,
	0x80,0x11,0x33,0x38,0x5A,0x11,0xF9,0x33,0x3F,0xD2,0x41,0x4A,0x21,0x9B,0x54,0x44,
	0x00,0xB6,0x07,0x33,0x4A,0x5B,0x4E,0x09,0x7C,0x9D,0xB8,0xDE,0x6B,0xA2,0xB2,0x78,
	0x23,0x3D,0xF0,0xB7,0x37,0x2B,0x7A,0x71,0x50,0x6E,0xEA,0x93,0x3E,0xB5,0x2C,0xBD,
	0xD6,0x08,0x43,0x12,0x0A,0xE8,0x8D,0xE6,0x6C,0x24,0xCC,0x3F,0xF7,0x18,0x7E,0x87,
	0x59,0x0C,0xA9,0x5D,0x85,0xF8,0x6E,0x83,0xD8,0x18,0x77,0x07,0xB6,0x44,0x3C,0x8D,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01
};

u_int8_t private_key_1[] = {
	0xD4,0x8D,0x40,0x8E,0xBD,0xFC,0x6D,0xE9,0xDB,0x1C,0xD2,0x21,0x19,0x37,0x6B,0xE2,
	0xDC,0xCE,0x74,0xA2,0x63,0xF6,0xD8,0x8D,0xAF,0x1C,0xC0,0xFF,0x07,0x3F,0xFB,0x52,
	0x59,0x45,0x01,0x10,0x35,0xA9,0xB8,0x16,0x69,0x31,0x19,0x4F,0xDD,0x66,0xAD,0xAC,
	0x80,0x11,0x33,0x38,0x5A,0x11,0xF9,0x33,0x3F,0xD2,0x41,0x4A,0x21,0x9B,0x54,0x44,
	0x00,0xB6,0x07,0x33,0x4A,0x5B,0x4E,0x09,0x7C,0x9D,0xB8,0xDE,0x6B,0xA2,0xB2,0x78,
	0x23,0x3D,0xF0,0xB7,0x37,0x2B,0x7A,0x71,0x50,0x6E,0xEA,0x93,0x3E,0xB5,0x2C,0xBD,
	0xD6,0x08,0x43,0x12,0x0A,0xE8,0x8D,0xE6,0x6C,0x24,0xCC,0x3F,0xF7,0x18,0x7E,0x87,
	0x59,0x0C,0xA9,0x5D,0x85,0xF8,0x6E,0x83,0xD8,0x18,0x77,0x07,0xB6,0x44,0x3C,0x8D,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0xEE,0xF2,0x37,0xF2,0x98,0xEB,0x33,0xC6,0x84,0xE8,0xB9,0xD1,0x18,0xB5,0x29,0x00,
	0xAC,0x6B,0x78,0xBC,0x9E,0xB6,0x01,0x21,0x29,0xEE,0x4A,0x99,0xFB,0x3D,0x07,0x23,
	0x77,0x84,0x93,0x4B,0x53,0x49,0xB0,0xA4,0x6F,0xB0,0xF5,0x50,0xDB,0x35,0xDD,0xDF,
	0x41,0x6F,0x7B,0xA9,0x88,0x3D,0x0B,0x1C,0x2E,0x2B,0x44,0x35,0x24,0x72,0x66,0xC1,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0xE3,0xB8,0xC8,0x30,0x67,0xD0,0x5D,0xF1,0x32,0x64,0xDC,0x4B,0xB3,0x7E,0xE3,0x1A,
	0xC5,0xBC,0xAC,0xC9,0x95,0x5C,0x96,0x0D,0x5A,0x52,0x90,0xE0,0x08,0x3F,0xA6,0x71,
	0xC7,0x18,0xC5,0x64,0xA2,0xE4,0xB8,0x43,0x5A,0x8A,0x7A,0x9B,0xDF,0xDA,0x81,0x85,
	0x6C,0x0F,0xA4,0xC9,0xAC,0x25,0x19,0x54,0xFE,0x75,0xAA,0x1D,0x22,0xB8,0xF4,0xCD,
	0x1A,0x91,0xC2,0xA3,0x65,0x3F,0xD7,0xFC,0x7E,0xE1,0x92,0x29,0xC5,0x85,0x6E,0x44,
	0xC8,0x4D,0xBD,0x7A,0x2C,0x2D,0x47,0xE2,0x24,0x24,0xDF,0xC2,0x31,0x65,0x8F,0xD4,
	0xBA,0x28,0x7C,0x4A,0xCA,0xAE,0x79,0xBE,0xC1,0x6C,0xFC,0x09,0x45,0xF7,0x87,0x17,
	0xB4,0x55,0x92,0x15,0xC5,0xFA,0x8F,0xB0,0x56,0x96,0xC1,0x87,0x12,0xFE,0xDF,0xF0,
	0x3A,0xE1,0xB1,0x83,0x19,0x74,0xF0,0x7D,0x37,0x41,0x3E,0x6A,0xFE,0x33,0x3E,0x74,
	0x01,0x45,0xE4,0x65,0xAE,0xC9,0xAE,0x64,0xE3,0xF1,0x90,0xFD,0x1A,0x30,0x44,0x82,
	0xEE,0x34,0x94,0xF2,0x68,0x3D,0x61,0x90,0xFB,0xEB,0xD8,0x18,0xE6,0x7C,0xEC,0x69,
	0x70,0xD0,0xEB,0x2F,0xC1,0x3D,0x9C,0x6A,0x4B,0x89,0x50,0x6B,0x3F,0xA5,0x38,0x41,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x65,0xEE,0x34,0x09,0xAC,0x4C,0x21,0x71,0x1D,0x3F,0x7E,0x0D,0x01,0xC2,0x3E,0x34,
	0x88,0x58,0xEC,0x4F,0x62,0x50,0xF7,0xD8,0x62,0xDF,0xC1,0x39,0x40,0xA0,0xBF,0x0B,
	0xD5,0x2F,0x5B,0xFA,0x35,0x14,0x69,0x63,0x2C,0x36,0x4B,0xDF,0xEB,0x33,0x66,0x6B,
	0x97,0xA9,0x6C,0x12,0x5D,0x08,0xD5,0x55,0x77,0x28,0x83,0xD7,0x3B,0xAE,0x05,0xC1,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x9F,0x96,0x17,0x75,0x14,0xCB,0xC9,0x8A,0x06,0xAE,0xF8,0x53,0x74,0xEF,0x2F,0x68,
	0xCB,0xBA,0x75,0xBC,0xAF,0x97,0xBA,0xF0,0x90,0xA3,0xDC,0x33,0xA4,0x94,0x36,0xA8,
	0xF5,0xC6,0x3E,0x4F,0x50,0x78,0xC9,0x49,0x2A,0x62,0x71,0x9A,0x5B,0x3E,0x5E,0x16,
	0x8A,0xAC,0x4B,0xE7,0xA9,0x64,0x36,0x64,0x82,0x0F,0x23,0xB0,0x57,0x6D,0x16,0xE1,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x25,0xF1,0x40,0x05,0x58,0x19,0x37,0x61,0x34,0x98,0xBB,0x29,0x1B,0x44,0x08,0x1A,
	0xD3,0x66,0x62,0x4C,0x9C,0x47,0xD2,0x91,0x60,0x46,0x6F,0x8E,0xA6,0xE7,0x80,0x7B,
	0x17,0x77,0x9A,0xB5,0x18,0x8A,0x15,0x8F,0x77,0xA1,0x55,0x3E,0x96,0x66,0x86,0x57,
	0x75,0x73,0xF5,0x57,0x50,0x28,0xEA,0x83,0x14,0xB1,0x55,0xA3,0x82,0xCD,0x36,0xF8
};
u_int8_t public_key_2[] = {
	0x88,0x3E,0xE2,0x2E,0x5D,0x01,0x13,0xDF,0x1D,0x8B,0xF4,0x39,0xCA,0xE6,0x3C,0xE1,
	0x46,0x8E,0xD4,0xF1,0x06,0x56,0x12,0x8D,0xCD,0x51,0xBD,0x32,0xF5,0x18,0x15,0x4D,
	0x0F,0x98,0xDF,0xFF,0xA5,0xA3,0xAB,0x39,0x43,0xC4,0xF6,0xAC,0x98,0x5C,0x84,0x63,
	0x8C,0x46,0x33,0xA2,0x23,0x8C,0xF0,0x4D,0xFE,0xE7,0xF3,0x38,0xC4,0x19,0x39,0xC4,
	0x90,0xF4,0xC8,0x0D,0xB0,0xFE,0x65,0x11,0x0B,0x41,0x73,0xBB,0x05,0xA6,0x4B,0xC5,
	0x27,0xA4,0x48,0x21,0xC5,0xAE,0x91,0x9C,0xD8,0x62,0x27,0xBE,0xDF,0xDA,0xC6,0x4E,
	0xC1,0x6E,0x5B,0x61,0x51,0xAA,0xC9,0x53,0xCD,0x02,0x5B,0xC5,0xEE,0xE9,0xC7,0x7B,
	0xB1,0x7E,0xD2,0xC2,0xFE,0x5F,0xD7,0x0F,0x75,0x2B,0xB9,0x49,0x5F,0x35,0xF1,0x83,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01
};
u_int8_t private_key_2[] = {
	0x88,0x3E,0xE2,0x2E,0x5D,0x01,0x13,0xDF,0x1D,0x8B,0xF4,0x39,0xCA,0xE6,0x3C,0xE1,
	0x46,0x8E,0xD4,0xF1,0x06,0x56,0x12,0x8D,0xCD,0x51,0xBD,0x32,0xF5,0x18,0x15,0x4D,
	0x0F,0x98,0xDF,0xFF,0xA5,0xA3,0xAB,0x39,0x43,0xC4,0xF6,0xAC,0x98,0x5C,0x84,0x63,
	0x8C,0x46,0x33,0xA2,0x23,0x8C,0xF0,0x4D,0xFE,0xE7,0xF3,0x38,0xC4,0x19,0x39,0xC4,
	0x90,0xF4,0xC8,0x0D,0xB0,0xFE,0x65,0x11,0x0B,0x41,0x73,0xBB,0x05,0xA6,0x4B,0xC5,
	0x27,0xA4,0x48,0x21,0xC5,0xAE,0x91,0x9C,0xD8,0x62,0x27,0xBE,0xDF,0xDA,0xC6,0x4E,
	0xC1,0x6E,0x5B,0x61,0x51,0xAA,0xC9,0x53,0xCD,0x02,0x5B,0xC5,0xEE,0xE9,0xC7,0x7B,
	0xB1,0x7E,0xD2,0xC2,0xFE,0x5F,0xD7,0x0F,0x75,0x2B,0xB9,0x49,0x5F,0x35,0xF1,0x83,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0xE8,0x37,0xB6,0x08,0xD8,0x9C,0x72,0xC5,0x34,0xDB,0x3A,0xA2,0xF9,0x24,0xE1,0x44,
	0x23,0x3B,0x72,0x70,0x5D,0xCC,0xC3,0xBA,0x3D,0xCE,0x82,0xAC,0x6A,0x71,0x72,0x90,
	0xC7,0x94,0xB3,0x8B,0x85,0xE0,0xEF,0x39,0xF0,0xE4,0x08,0x31,0xEA,0xE6,0x3B,0x7D,
	0xB0,0x36,0xFA,0x71,0x6E,0xA3,0xF9,0x4C,0x39,0x05,0x8C,0xB7,0x8C,0x99,0x94,0x85,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x96,0x32,0xF9,0xD9,0xA8,0xC0,0x84,0xFD,0xE5,0x6B,0xA6,0xC2,0x85,0x85,0x68,0x17,
	0x7E,0x98,0xD0,0x6A,0xDC,0xD8,0x4C,0x46,0xCB,0x6D,0x4C,0x25,0xE5,0xF9,0x58,0xB2,
	0x17,0xE4,0x20,0x8A,0x87,0x0D,0xD7,0x4C,0x79,0xA3,0xB3,0x69,0x98,0x7F,0x5D,0x08,
	0x33,0x5B,0xAD,0xA3,0x34,0xE8,0x55,0x5E,0x09,0x60,0x70,0xA8,0x11,0xFD,0x70,0x67,
	0x00,0xE1,0xA7,0x44,0xF5,0x85,0x14,0x43,0xD5,0x45,0x1A,0x87,0x65,0x30,0xA8,0x24,
	0x2C,0xF8,0xAF,0x97,0xFF,0x9A,0x7E,0xF4,0x3B,0xE7,0xD3,0x79,0x88,0xEC,0x66,0xF6,
	0xE0,0xAA,0xF4,0x88,0x0A,0xE2,0x4C,0x31,0x4A,0xA6,0xF3,0x91,0x9A,0x4A,0xBE,0xF0,
	0x85,0xEF,0xCE,0x55,0xB6,0x35,0x2B,0x38,0xD5,0xF5,0x5A,0x35,0x7B,0xCF,0x4D,0xF8,
	0x5D,0x1E,0x57,0x99,0xAF,0xED,0x33,0x6F,0xD5,0xA7,0x49,0x5B,0x14,0x4C,0x7D,0x17,
	0x81,0xAE,0x1E,0xDA,0x9D,0xFB,0xA9,0xC3,0x00,0x4C,0x17,0x37,0x30,0x96,0x60,0xE1,
	0x6A,0xCC,0xD3,0xDB,0x40,0xCE,0x96,0x96,0x0D,0x95,0x0D,0x84,0x38,0xBD,0xDA,0x2F,
	0xEC,0xED,0x22,0x39,0x8E,0x8C,0xDF,0xCD,0x07,0xCF,0x0F,0xB0,0x2B,0x76,0xDB,0xC1,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0xA5,0x37,0x9E,0x08,0x45,0x35,0x6A,0x62,0xEC,0xEC,0x5D,0x97,0xBE,0x73,0x82,0xE2,
	0x9B,0xBE,0x9B,0xF9,0x5E,0x83,0x65,0x6E,0x88,0xB2,0xF9,0x3D,0xFA,0xAD,0xA4,0xB9,
	0x65,0x86,0x63,0x08,0x0D,0xC4,0xAF,0xF0,0x25,0x77,0xD8,0x6C,0xCB,0x97,0xEB,0x13,
	0xCD,0xE0,0x0F,0xE7,0xCC,0xB4,0x55,0x96,0xE9,0xAB,0x0D,0x27,0x3A,0x9D,0xBA,0x91,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x44,0xA3,0x44,0xF4,0x47,0x9E,0xBA,0xE7,0xBF,0xF8,0xC2,0xFB,0x2F,0xC3,0x38,0x3F,
	0x4C,0x56,0x0F,0x20,0x56,0x8D,0xED,0xC5,0x88,0x5F,0x09,0x26,0x64,0x82,0xDF,0x1A,
	0x7B,0xBA,0x7F,0x78,0x6E,0xA1,0x4F,0x9B,0x1E,0x17,0x45,0xFC,0xE2,0x78,0x89,0x8E,
	0x1E,0xD2,0x2D,0x76,0x60,0xCE,0x2F,0x7C,0xCA,0xB2,0x2C,0xA9,0x51,0x97,0x4C,0xCF,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x01,0x40,0x4B,0x7D,0xAB,0x8A,0xB9,0x5E,0xEE,0xA1,0x81,0xED,0x27,0x89,0xF6,0x4C,
	0x59,0x8C,0x23,0x14,0x3B,0x1B,0xBA,0xC3,0xB2,0x00,0x9A,0x9E,0xDF,0x54,0x82,0xA7,
	0x3E,0xC9,0x23,0x85,0x4D,0xD3,0x80,0xA7,0x89,0x11,0xBA,0x76,0xF5,0xC1,0x55,0x37,
	0x0A,0x0D,0x8C,0x07,0x0A,0xC8,0xC5,0x11,0x74,0x9C,0xB6,0x80,0x3B,0x0A,0x9A,0xA2
};
