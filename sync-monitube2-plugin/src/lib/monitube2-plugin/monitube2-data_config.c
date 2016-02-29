/*
 * $Id: monitube2-data_config.c 434010 2011-04-01 14:30:24Z rkasyap $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2009, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file monitube2-data_config.c
 * @brief Relating to getting and setting the configuration data
 *
 *
 * These functions will store and provide access to the
 * configuration data which is essentially coming from the mgmt component.
 */

#include "monitube2-data_main.h"
#include "monitube2-data_config.h"


/*** Constants ***/

#define MONITUBE2_MGMT_STRLEN  256     ///< Application name length

#define RLOCK(l) INSIST_ERR(msp_rwlock_rlock(l) == MSP_OK) ///< Read lock

#define WLOCK(l) INSIST_ERR(msp_rwlock_wlock(l) == MSP_OK) ///< Write lock

#define RUNLOCK(l) INSIST_ERR(msp_rwlock_runlock(l) == MSP_OK) ///< Read lock

#define WUNLOCK(l) INSIST_ERR(msp_rwlock_wunlock(l) == MSP_OK) ///< Write lock

/*** Data structures ***/

/**
 * The id assigned by mspmand unique over all plugins
 */
extern int plugin_id;

/**
 * The main evContext on the ctrl plane side
 */
extern evContext * ctx;

/**
 * The policy database handle
 */
extern msp_policy_db_handle_t pdb_handle;

/**
 * The shared mem handle for the PDB
 */
extern void * pdb_shm_handle;

/**
 * A list item
 */
typedef struct list_item_s {
	void * data; ///< data
    TAILQ_ENTRY(list_item_s) entries; ///< ptrs to next/prev items
} list_item_t;


typedef TAILQ_HEAD(list_s, list_item_s) list_t; ///< list typedef


/**
 * The structure we use to bundle service-set info
 * 
 * We don't need the prefixes_lock during the delayed delete
 */
typedef struct ss_info_s {
    uint16_t     id;        ///< service set id number
    u_int32_t    svc_id;    ///< service id
    u_int32_t    gen_num;   ///< generation number
    radix_root_t prefixes;  ///< Radix tree for monitor lookups in data path
    msp_rwlock_t prefixes_lock; ///< Lock while using prefixes or its contents
    list_t       rules;     ///< list of rules (only used in ctrl path)
} ss_info_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each destination address/prefix
 */
typedef struct address_s {
    patnode    node;       ///< Tree node in addresses of rule_t
    in_addr_t  address;    ///< monitor name
    in_addr_t  mask;       ///< mask
} address_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each rule
 * 
 * Only redirect and rate could be used in the fast path. To change these or 
 * delete the rule one should have a write lock. The rest is only touched in the
 * ctrl path (and from one thread). In ctrl side we don't always grab the lock
 * when reading, b/c we don't care about reading while data side is reading.
 * We just want to block ctrl side writing while data side is reading. 
 */
typedef struct rule_s {
    patnode    node;                ///< Tree node in rules
    char       name[MONITUBE2_MGMT_STRLEN];  ///< rule name
    uint32_t   rate;                ///< monitoring rate
    in_addr_t  redirect;            ///< mirror to address
    patroot *  addresses;           ///< "from" dest addresses
    list_t     ssets;               ///< list of service sets using this rule
    
    msp_rwlock_t lock;              ///< Lock 
} rule_t;


/**
 * The structure we use to bundle the radix-tree node with a pointer to the
 * rule we need. Used in the data & ctrl paths with a lock in ss_info_t
 */
typedef struct prefix_s {
    rnode_t     rnode;        ///< Radix tree node in prefixes of a ss_info_t
    in_addr_t   prefix;       ///< prefix
    rule_t *    rule;         ///< pointer to a monitor
} prefix_t;


/**
 * The current cached time. Cache it to prevent many system time() calls by the
 * data threads.
 */
static atomic_uint_t current_time;


/**
 * The PATRICIA tree root for rules configuration. Only used in the ctrl path
 */
static patroot rules;


/*** STATIC/INTERNAL Functions ***/

/**
 * Generate the rule_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(rule_entry, rule_t, node)


/**
 * Generate the address_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(address_entry, address_t, node)


/**
 * Update cached time
 *
 * @param[in] ctx
 *     The event context for this application
 *
 * @param[in] uap
 *     The user data for this callback
 *
 * @param[in] due
 *     The absolute time when the event is due (now)
 *
 * @param[in] inter
 *     The period; when this will next be called
 */
static void
update_time(evContext ectx UNUSED,
            void * uap  UNUSED,
            struct timespec due UNUSED,
            struct timespec inter UNUSED)
{
    atomic_add_uint(1, &current_time);
}


/**
 * Count the number of bits in a uint32_t / in_addr_t
 *
 * @param[in] n
 *      The input (mask) that we want the number of bits for
 *
 * @return the number of 1 bits
 */
static uint8_t
bit_count(uint32_t n)
{
    register unsigned int tmp;

    tmp = n - ((n >> 1) & 033333333333)
            - ((n >> 2) & 011111111111);
    return ((tmp + (tmp >> 3)) & 030707070707) % 63;
}


/**
 * Callback to free policy after some delay
 *
 * @param[in] ctx
 *     The event context for this application
 *
 * @param[in] uap
 *     The user data for this callback
 *
 * @param[in] due
 *     The absolute time when the event is due (now)
 *
 * @param[in] inter
 *     The period; when this will next be called
 */
static void
free_policy(evContext ectx UNUSED,
            void * uap,
            struct timespec due UNUSED,
            struct timespec inter UNUSED)
{
    ss_info_t * ssi;
	msp_policy_db_svc_set_key_t * key= (msp_policy_db_svc_set_key_t *)uap;
	msp_policy_db_params_t policy_db_params;	
	list_item_t * item;
	prefix_t * p;
	rnode_t * r;
	
	bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
	policy_db_params.handle = pdb_handle;
	policy_db_params.plugin_id = plugin_id;
	strlcpy(policy_db_params.plugin_name, PLUGIN_NAME,
            sizeof(policy_db_params.plugin_name));
	policy_db_params.svc_set_id = key->svc_set_id;
	policy_db_params.svc_id = key->svc_id;
	policy_db_params.policy_op = MSP_POLICY_DB_POLICY_GET;

	if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
            CLOG(LOG_INFO, "%s: No PDB configuration found", __func__);
            return;
	}

	ssi = policy_db_params.op.get_params.policy;

    // Empty the rules list and the prefixes rtree
	// should be no readers of the prefixes by this delayed point
    
	while((r = radix_find_next(&ssi->prefixes, NULL)) != NULL) {
		radix_delete(&ssi->prefixes, r);
		p = (prefix_t *)((uint8_t *)r - offsetof(prefix_t, rnode));
		msp_shm_free(pdb_shm_handle, p);
	}
	
    while((item = TAILQ_FIRST(&ssi->rules)) != NULL) {
    	TAILQ_REMOVE(&ssi->rules, item, entries);
    	msp_shm_free(pdb_shm_handle, item);
    }
	
	msp_shm_free(pdb_shm_handle, ssi);
}

/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 * 
 * Should only be called on startup (one time)
 */
void
init_config(void)
{
    patricia_root_init(&rules, FALSE, MONITUBE2_MGMT_STRLEN, 0);

    current_time = (atomic_uint_t)(time(NULL) - 1);

    // cached system time
    if(evSetTimer(*ctx, update_time, NULL, evNowTime(), evConsTime(1,0), NULL)) {
        CLOG(LOG_EMERG, "%s: Failed to initialize an eventlib timer to generate "
            "the cached time", __func__);
    }
}


/**
 * Get the currently cached time
 *
 * @return
 *      Current time
 */
time_t
get_current_time(void)
{
    return (time_t)current_time;
}


/**
 * Clear the stored configuration data
 */
void
clear_config(void)
{
    msp_policy_db_params_t policy_db_params;
	msp_policy_db_svc_set_key_t key, next_key;
	rule_t * rule;
	
	// First we delete the service set policies, then the rules
	
	bzero(&key, sizeof(msp_policy_db_svc_set_key_t));
	bzero(&next_key, sizeof(msp_policy_db_svc_set_key_t));
	bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
	
	policy_db_params.handle = pdb_handle;
	policy_db_params.plugin_id = plugin_id;
	strlcpy(policy_db_params.plugin_name, PLUGIN_NAME,
			sizeof(policy_db_params.plugin_name));
	
	while(msp_policy_db_get_next_svc_set_id(
			pdb_handle, &key, &next_key) == EOK) {
		
		policy_db_params.svc_set_id = next_key.svc_set_id;
		policy_db_params.svc_id = next_key.svc_id;
		policy_db_params.policy_op = MSP_POLICY_DB_POLICY_DEL;
		policy_db_params.op.del_params.gen_num = next_key.gen_num;

		// Delete the policy for the service set
		
		if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
			CLOG(LOG_INFO, "%s: PDB Delete failed!", __func__);
	        key.svc_set_id = next_key.svc_set_id;
	        key.svc_id = next_key.svc_id;
			continue;
		}
			
	        // delay the free of the policy in case someone did a GET op and 
		// is using it make some random delay between 5-15 secs so there's 
		// not too many frees at once (since it uses some locking in the shm
		// pool) in case we're clearing a lot
			
	        if (evSetTimer(*ctx, free_policy, &next_key, evAddTime(evNowTime(),
		    	evConsTime(5 + (rand() % 11), 0)), evConsTime(0, 0), NULL)) {

		        CLOG(LOG_EMERG, "%s: Failed to initialize a timer to free the "
		        		"policy (Error: %m)", __func__);
		}

		key.svc_set_id = next_key.svc_set_id;
		key.svc_id = next_key.svc_id;
	}

    while((rule = rule_entry(patricia_find_next(&rules, NULL)))) {
    	
    	// use delete_rule() which safely remove references from any
    	// service sets' prefixes to this rule
    	
    	if(delete_rule(rule->name)) {
    		CLOG(LOG_ERR, "%s: Cannot finish deleting the rules", __func__);
    		break; // on failure
    	}
    }
}


/**
 * Update/create a rule
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] rate
 *      monitoring rate
 *      
 * @param[in] redirect
 *       address for mirroring
 */
void
update_rule(char * name, uint32_t rate, in_addr_t redirect)
{
	rule_t * rule;
	
    rule = rule_entry(patricia_get(&rules, strlen(name) + 1, name));
    
    if(rule == NULL) {
        rule = msp_shm_alloc(pdb_shm_handle, sizeof(rule_t));
        INSIST_ERR(rule != NULL);
        strlcpy(rule->name, name, sizeof(rule->name));

        // Add to patricia tree
        patricia_node_init_length(&rule->node, strlen(name) + 1);

        // init addresses
        rule->addresses = patricia_root_init(NULL, FALSE, sizeof(in_addr_t), 0);
        TAILQ_INIT(&rule->ssets);
        INSIST_ERR(msp_rwlock_init(&rule->lock) == MSP_OK);
        
        rule->rate = rate;
        rule->redirect = redirect;
        
        if(!patricia_add(&rules, &rule->node)) {
            CLOG(LOG_ERR, "%s: Failed to add rule to configuration", __func__);
            free(rule);
        }

        return;        
    }
    
    WLOCK(&rule->lock);
    rule->rate = rate;
    rule->redirect = redirect;
    WUNLOCK(&rule->lock);
}


/**
 * Delete rule
 *
 * @param[in] name
 *      rule name
 *      
 * @return 0 on success; -1 o/w
 */
int
delete_rule(char * name)
{
	rule_t * rule;
	address_t * a;
    ss_info_t * ssi;
	rnode_t * rn;
	prefix_t * prefix;
	list_item_t * item;
	
    rule = rule_entry(patricia_get(&rules, strlen(name) + 1, name));
    
    if(rule == NULL) {
    	CLOG(LOG_ERR, "%s: Cannot find the rule %s", __func__, name);
    	return -1;
    }
    
	if(!patricia_delete(&rules, &rule->node)) {
		CLOG(LOG_ERR, "%s: Failed to delete rule from configuration", __func__);
		return -1;
	}
    
    WLOCK(&rule->lock); // wait for any readers to finish
	
	// Delete ssets and references from those sets to this rule
	
    while((item = TAILQ_FIRST(&rule->ssets)) != NULL) {
    	ssi = item->data; // this sset applies this rule
		TAILQ_REMOVE(&rule->ssets, item, entries);
		msp_shm_free(pdb_shm_handle, item);
		
		// remove reference to this rule
		
	    item = TAILQ_FIRST(&ssi->rules);
	    while(item != NULL) {
	        if (item->data == rule) {
	    		TAILQ_REMOVE(&ssi->rules, item, entries);
	    		msp_shm_free(pdb_shm_handle, item);
	        	break;
	        }
	        item = TAILQ_NEXT(item, entries);
	    }
	    
	    // remove any prefixes in this service set coming from this rule
	    
	    a = address_entry(patricia_find_next(rule->addresses, NULL));
	    while(a != NULL) {
	    	
	        rn = radix_get(&ssi->prefixes, bit_count(a->mask),
	        		(uint8_t *)&a->address);

	        if(rn != NULL) {
	            prefix = (prefix_t *)((uint8_t *)rn - offsetof(prefix_t,rnode));
	            
	        	if(prefix->rule == rule) {
	        		WLOCK(&ssi->prefixes_lock);
	        		
	        		radix_delete(&ssi->prefixes, rn);
	        		
	        		WUNLOCK(&ssi->prefixes_lock);
	        	}
	        }
	    	
	    	a = address_entry(patricia_find_next(rule->addresses, &a->node));
	    }
	    
    }
    
    // delete all addresses

    while((a = address_entry(patricia_find_next(rule->addresses, NULL)))) {
    	patricia_delete(rule->addresses, &a->node);
    	free(a);
    }
    
    patricia_root_delete(rule->addresses);

	// delete rule
    
	msp_shm_free(pdb_shm_handle, rule);
	return 0;
}


/**
 * Add an address to the rule
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Mask to apply to @c addr
 */
void
add_address(char * name, in_addr_t addr, in_addr_t mask)
{
	rule_t * rule;
	address_t * address;
	ss_info_t * ssi;
	prefix_t * prefix;
	list_item_t * item;
	
    rule = rule_entry(patricia_get(&rules, strlen(name) + 1, name));
    
    if(rule == NULL) {
    	CLOG(LOG_ERR, "%s: Cannot find the rule %s", __func__, name);
    	return;
    }
    
    // address won't be SHM b/c it's never accessed in fast path
    address = calloc(1, sizeof(address_t));
    INSIST_ERR(address != NULL);

    address->address = addr;
    address->mask = mask;

    // Add to patricia tree
    
    patricia_node_init(&address->node);

    if(!patricia_add(rule->addresses, &address->node)) {
        CLOG(LOG_ERR, "%s: Cannot add address %08X / %08X as it conflicts with"
            " another address in the same rule (%s)", __func__, addr, mask, name);
        free(address);
    }
    
    // Ssets using this rule need their radix tree updated
    
    item = TAILQ_FIRST(&rule->ssets);
    while(item != NULL) {
    	ssi = item->data;
    	
        prefix = msp_shm_alloc(pdb_shm_handle, sizeof(prefix_t));
        INSIST_ERR(prefix != NULL);
        prefix->prefix = addr;
        prefix->rule = rule;

        radix_node_init(&prefix->rnode, bit_count(mask), 0);

        WLOCK(&ssi->prefixes_lock);
        
        if(radix_add(&ssi->prefixes, &prefix->rnode)) {
        	WUNLOCK(&ssi->prefixes_lock);
            msp_shm_free(pdb_shm_handle, prefix);
            CLOG(LOG_ERR, "%s: Failed to add prefix to rtree configuration."
            		" Could be due to a duplicate prefix.", __func__);
        } else {
        	WUNLOCK(&ssi->prefixes_lock);
        }
    }
}


/**
 * Delete an address from the rule
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Mask to apply to @c addr
 */
void
delete_address(char * name, in_addr_t addr, in_addr_t mask)
{
	rule_t * rule;
	address_t * address;
	rnode_t * rn;
	ss_info_t * ssi;
	prefix_t * prefix;
	list_item_t * item;
	
	
    rule = rule_entry(patricia_get(&rules, strlen(name) + 1, name));
    
    if(rule == NULL) {
    	CLOG(LOG_ERR, "%s: Cannot find the rule %s", __func__, name);
    	return;
    }

    address = address_entry(patricia_get(rule->addresses,
                    sizeof(in_addr_t), &addr));

    if(address == NULL) {
        CLOG(LOG_ERR, "%s: Could not find address to delete",__func__);
        return;
    }

    if(!patricia_delete(rule->addresses, &address->node)) {
        CLOG(LOG_ERR, "%s: Deleting address failed", __func__);
        return;
    }
    
    // Ssets using this rule need their radix tree updated
    
    item = TAILQ_FIRST(&rule->ssets);
    while(item != NULL) {
    	ssi = item->data;
    	
        rn = radix_get(&ssi->prefixes, bit_count(mask), (uint8_t *)&addr);

        if(rn == NULL) {
            CLOG(LOG_ERR, "%s: Failed to find prefix to remove from "
                    "rtree configuration", __func__);
        } else {
            prefix = (prefix_t *)((uint8_t *)rn - offsetof(prefix_t, rnode));
        	if(prefix->rule != rule) {
                CLOG(LOG_WARNING, "%s: Prefix in the rtree doesn't belong to "
					"this rule. Could be due to a duplicate prefix.", __func__);
        	} else {
        		WLOCK(&ssi->prefixes_lock);
        		
        		radix_delete(&ssi->prefixes, rn);
        		
        		WUNLOCK(&ssi->prefixes_lock);
        	}
        }
    }
    
    free(address);
}


/**
 * Apply rule on a service set
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] ssid
 *      service set id
 *
 * @param[in] gennum
 *      Service set generation number needed for PDB
 *      
 * @param[in] svcid
 *      Service ID needed for PDB
 */
void
apply_rule(char * name, uint16_t ssid, uint32_t gennum, uint32_t svcid)
{
    msp_policy_db_params_t policy_db_params;
    ss_info_t * ssi;
    rule_t * rule;
    list_item_t * item;

	if(svcid && svcid != MSP_SVC_ID_SERVICES) {
		CLOG(LOG_ERR, "%s: Found unexpected service id: %d", __func__, svcid);
	}
	
	bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
	
	// Get the current policy for the service set
	
	policy_db_params.handle = pdb_handle;
	policy_db_params.svc_set_id = ssid;
	policy_db_params.svc_id = svcid;
	policy_db_params.plugin_id = plugin_id;
	policy_db_params.policy_op = MSP_POLICY_DB_POLICY_GET;
	strlcpy(policy_db_params.plugin_name, PLUGIN_NAME,
			sizeof(policy_db_params.plugin_name));

	if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
	    CLOG(LOG_INFO, "%s: No PDB configuration found", __func__);
	    ssi = NULL;
	} else {
	    ssi = policy_db_params.op.get_params.policy;    
	}
	    
	if(ssi == NULL) {
		// Add to the PDB for this service set
		
		ssi = msp_shm_alloc(pdb_shm_handle, sizeof(ss_info_t));
		if(!ssi) {
			CLOG(LOG_ALERT, "%s: Cannot allocate into policy SHM...", __func__);
			return;
		}
		
		ssi->id = ssid;
		ssi->svc_id = svcid;
		ssi->gen_num = gennum;
		radix_init_root(&ssi->prefixes);
		TAILQ_INIT(&ssi->rules);
		INSIST_ERR(msp_rwlock_init(&ssi->prefixes_lock) == MSP_OK);
	    
		bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
		
		// Populate the policy database
		
		// General params
		policy_db_params.handle = pdb_handle;
		policy_db_params.svc_set_id = ssid;
		policy_db_params.svc_id = svcid;
		policy_db_params.plugin_id = plugin_id;
		policy_db_params.policy_op = MSP_POLICY_DB_POLICY_ADD;
		strlcpy(policy_db_params.plugin_name, PLUGIN_NAME,
				sizeof(policy_db_params.plugin_name));

		// Add-specific params
		policy_db_params.op.add_params.gen_num = gennum;
		policy_db_params.op.add_params.policy = ssi;
		
		if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
			CLOG(LOG_ERR, "%s: Policy DB Add failed!", __func__);
		}
	} else if (gennum != ssi->gen_num) {
		CLOG(LOG_ERR, "%s: Generation number %d is out of sync with what is"
				"currently in the policy DB %d for this service set %d",
				__func__, gennum, ssi->gen_num, ssid);
		return;
	}

	// Add the rule to the policy for this service set
	
	rule = rule_entry(patricia_get(&rules, strlen(name) + 1, name));
	if(rule == NULL) {
		CLOG(LOG_ERR, "%s: Cannot find the rule %s", __func__, name);
		return;
	}
	
    item = TAILQ_FIRST(&ssi->rules);
    while(item != NULL) {
        if (item->data == rule) {
            // already in list
            return;
        }
        item = TAILQ_NEXT(item, entries);
    }

    // it wasn't found so add it
    
	item = msp_shm_alloc(pdb_shm_handle, sizeof(list_item_t));        
	item->data = rule;
	
	TAILQ_INSERT_TAIL(&ssi->rules, item, entries);
	
	item = msp_shm_alloc(pdb_shm_handle, sizeof(list_item_t));        
	item->data = ssi;
	
	TAILQ_INSERT_TAIL(&rule->ssets, item, entries);
}


/**
 * Remove a rule from a service set
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] ssid
 *      service set id
 *
 * @param[in] gennum
 *      Service set generation number needed for PDB
 *      
 * @param[in] svcid
 *      Service ID needed for PDB
 */
void
remove_rule(char * name, uint16_t ssid, uint32_t gennum, uint32_t svcid)
{
    msp_policy_db_params_t policy_db_params;
    ss_info_t * ssi;
    rule_t * rule;
    list_item_t * item;
	
    rule = rule_entry(patricia_get(&rules, strlen(name) + 1, name));
    
    if(rule == NULL) {
    	CLOG(LOG_ERR, "%s: Cannot find the rule %s", __func__, name);
    	return;
    }
    
	bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
    
	// Get the current policy for the service set
	
	policy_db_params.handle = pdb_handle;
	policy_db_params.svc_set_id = ssid;
	policy_db_params.svc_id = svcid;
	policy_db_params.plugin_id = plugin_id;
	policy_db_params.policy_op = MSP_POLICY_DB_POLICY_GET;
	strlcpy(policy_db_params.plugin_name, PLUGIN_NAME,
			sizeof(policy_db_params.plugin_name));

	if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
		CLOG(LOG_WARNING, "%s: No PDB configuration found", __func__);
		return;
	}
	
	ssi = policy_db_params.op.get_params.policy;
	
	if(!ssi) {
		CLOG(LOG_ERR, "%s: No policy found for service set", __func__);
		return;
	}
	
	if(gennum != ssi->gen_num) {
		CLOG(LOG_ERR, "%s: Generation number %d is out of sync with what is"
				"currently in the policy DB %d for this service set %d",
				__func__, gennum, ssi->gen_num, ssid);
		return;
	}
	
	// Remove the rule from the rules list in ssi
	
    item = TAILQ_FIRST(&ssi->rules);
    while(item != NULL) {
        if (rule == item->data) {
        	TAILQ_REMOVE(&ssi->rules, item, entries);
        	msp_shm_free(pdb_shm_handle, item);
        	
        	// remove the reference of this ss in the ssets list of rule
        	
            item = TAILQ_FIRST(&rule->ssets);
            while(item != NULL) {
            	if(ssi == item->data) {
                	TAILQ_REMOVE(&rule->ssets, item, entries);
                	msp_shm_free(pdb_shm_handle, item);
            		break;
            	}
            	item = TAILQ_NEXT(item, entries);
            }
            break;
        }
        item = TAILQ_NEXT(item, entries);
    }
    
    if(TAILQ_FIRST(&ssi->rules) != NULL) // List is not empty
    	return;
    
    // List of rules is empty, so let's remove this set's policy
	// Delete the current policy for the service set
    
	policy_db_params.policy_op = MSP_POLICY_DB_POLICY_DEL;
	policy_db_params.op.del_params.gen_num = gennum;

	if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
		CLOG(LOG_ERR, "%s: PDB Delete failed!", __func__);
		return;
	}
	
    // delay the free of the policy in case someone did a GET op and is using it
	// make some random delay between 5-15 secs so there's not too many frees at
	// once (since it uses some locking in the shm pool) in case we're clearing
	// a lot
	
    if(evSetTimer(*ctx, free_policy, ssi, evAddTime(evNowTime(),
    		evConsTime(5 + (rand() % 11), 0)), evConsTime(0, 0), NULL)) {

        CLOG(LOG_EMERG, "%s: Failed to initialize a timer to free the policy "
            "(Error: %m)", __func__);
    }
}


/**
 * Delete service set
 *
 * @param[in] ssid
 *      service set id
 */
void
delete_serviceset(uint16_t ssid, uint32_t gennum, uint32_t svcid)
{
    msp_policy_db_params_t policy_db_params;
    ss_info_t * ssi;
    rule_t * rule;
    list_item_t * item, * item2;
	
	bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
    
	// Get the current policy for the service set
	
	policy_db_params.handle = pdb_handle;
	policy_db_params.svc_set_id = ssid;
	policy_db_params.svc_id = svcid;
	policy_db_params.plugin_id = plugin_id;
	policy_db_params.policy_op = MSP_POLICY_DB_POLICY_GET;
	strlcpy(policy_db_params.plugin_name, PLUGIN_NAME,
			sizeof(policy_db_params.plugin_name));

	if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
		CLOG(LOG_WARNING, "%s: No PDB configuration found", __func__);
		return;
	}
	
	ssi = policy_db_params.op.get_params.policy;
	
	if(!ssi) {
		CLOG(LOG_ERR, "%s: No policy for service set to delete", __func__);
		return;
	}
	
	if(gennum != ssi->gen_num) {
		CLOG(LOG_ERR, "%s: Generation number %d is out of sync with what is"
				"currently in the policy DB %d for this service set %d",
				__func__, gennum, ssi->gen_num, ssid);
		return;
	}
	
	// delete references from rules to this service set
	// we delete the reference to the rules from this sset in the delayed delete
	
    while((item = TAILQ_FIRST(&ssi->rules)) != NULL) {
        rule = item->data;
    	
        item2 = TAILQ_FIRST(&rule->ssets);
        while(item2 != NULL) {
        	if(ssi == item2->data) {
            	TAILQ_REMOVE(&rule->ssets, item2, entries);
            	msp_shm_free(pdb_shm_handle, item2);
        		break;
        	}
        	item2 = TAILQ_NEXT(item2, entries);
        }
    	item = TAILQ_NEXT(item, entries);
    }
	
	// Delete the current policy for the service set
	
	policy_db_params.policy_op = MSP_POLICY_DB_POLICY_DEL;
	policy_db_params.op.del_params.gen_num = gennum;

	if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
		CLOG(LOG_ERR, "%s: PDB Delete failed!", __func__);
		return;
	}
	
	
    // delay the free of the policy in case someone did a GET op and is using it
	// make some random delay between 5-15 secs so there's not too many frees at
	// once (since it uses some locking in the shm pool) in case we're clearing
	// a lot
	
    if(evSetTimer(*ctx, free_policy, ssi, evAddTime(evNowTime(),
    		evConsTime(5 + (rand() % 11), 0)), evConsTime(0, 0), NULL)) {

        CLOG(LOG_EMERG, "%s: Failed to initialize a timer to free the policy "
            "(Error: %m)", __func__);
    }
}


/**
 * Find the monitoring rate and redirect address a rule-match happen one exists.
 * This will be run in the fast path.
 *
 * @param[in] ssid
 *      The service set id
 * 
 * @param[in] svcid
 *      The service id
 *
 * @param[in] address
 *      The destination address
 *      
 * @param[out] rate
 *      If null on input, this is ignored. The monitoring rate; 0 if no rate 
 *
 * @param[out] redirect_addr
 *      If null on input, this is ignored. The mirror addr; 0 if no mirroring
 *
 * @return 0 if found a valid rule; or -1 if no match is found
 */
int
get_monitored_rule_info(uint16_t ssid,
		                uint32_t svcid,
		                in_addr_t address,
		                uint32_t * rate,
		                uint32_t * redirect_addr)
{
    msp_policy_db_params_t policy_db_params;
	rnode_t * rn;
    prefix_t * prefix;
    ss_info_t * ssi;

	bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
	
	// Get the current policy for the service set
	
	policy_db_params.handle = pdb_handle;
	policy_db_params.svc_set_id = ssid;
	policy_db_params.svc_id = svcid;
	policy_db_params.plugin_id = plugin_id;
	policy_db_params.policy_op = MSP_POLICY_DB_POLICY_GET;
	strlcpy(policy_db_params.plugin_name, PLUGIN_NAME,
			sizeof(policy_db_params.plugin_name));

	if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
		DLOG(LOG_ERR, "%s: PDB Get failed!", __func__);
		return -1;
	}
	
	ssi = policy_db_params.op.get_params.policy;
	
	if(!ssi)
		return -1;
	
	RLOCK(&ssi->prefixes_lock); // hold while using any prefix in prefixes
	
    rn = radix_lookup(&ssi->prefixes, (uint8_t *)&address);
    
    if(rn == NULL) {
        return -1;
    }

    prefix = (prefix_t *)((uint8_t *)rn - offsetof(prefix_t, rnode));

	RLOCK(&prefix->rule->lock);
	
    if(rate)
    	*rate = prefix->rule->rate;
    
    if(redirect_addr)
    	*redirect_addr = prefix->rule->redirect;
    
    RUNLOCK(&prefix->rule->lock);
    
    RUNLOCK(&ssi->prefixes_lock);
    
    return 0;
}

