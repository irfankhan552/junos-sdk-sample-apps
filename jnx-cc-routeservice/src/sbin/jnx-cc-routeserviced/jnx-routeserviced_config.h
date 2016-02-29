/*
 * $Id: jnx-routeserviced_config.h 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */


#ifndef __JNX_ROUTESERVICED_CONFIG_H__
#define __JNX_ROUTESERVICED_CONFIG_H__

/**
 * Define for route data size
 */
#define RT_DATA_STR_SIZE 127

/**
 * Class definition with attributes for patricia tree storage
 */
typedef struct rs_config_data_s {
    patnode            routeserviced_patnode;
    char              *dest;
    char              *nhop;
    char              *ifname;
    int                req_status;
    unsigned int       req_ctx;
    unsigned int       prefixlen;
} rs_config_data_t;

/**
 * Class definition for configuration related attributes and methods
 */
class jnx_routeserviced_config {
    /*
     * A friend function which updates the private configuration
     * data structures
     */
    friend void config_data_update_status(jnx_routeserviced_config &cfg,
                                          unsigned int ctx, 
                                          unsigned int result);

    friend void send_pending_rt_add_requests(jnx_routeserviced_config &cfg);

    friend void mark_pending_rt_add_requests(jnx_routeserviced_config &cfg);

    public:
        /**
         * Destructor
         */
        ~jnx_routeserviced_config() {}

        static jnx_routeserviced_config &Instance(); 

        static int read(int check);
        void data_init(void);
        void data_delete_all(void);
        void data_add_entry(const char *dest, const char *nhop, 
                            const char *ifname, const char *plen);
        rs_config_data_t *data_first(void);
        rs_config_data_t *data_next(rs_config_data_t *data);

    private:
        /*
         * Hide the constructor, copy constructor and assignment operator
         * to disallow more than one instances of the class
         */

        /**
         * Private constructor
         */
        jnx_routeserviced_config() {}

        /**
         * Private copy constructor
         *
         * @param[in] old
         *     Reference to the object to be copied
         */
        jnx_routeserviced_config(const jnx_routeserviced_config &old);

        /**
         * Private assignment operator
         *
         * @param[in] old
         *     Reference to the object to be assigned 
         * 
         * @return
         *     Reference to the new object to be assigned 
         */
        const jnx_routeserviced_config &operator=(const 
                                                  jnx_routeserviced_config
                                                  &old);

        patroot   patricia_root;   /**< Patricia root node */
        uint32_t data_count;      /**< Count of entries stored in patricia */
        uint32_t ssd_req_count;   /**< Count of requests sent to SSD */

        static auto_ptr<jnx_routeserviced_config> unique_instance; /**< Unique
                                                                     class 
                                                                     instance */

        void data_delete_entry(rs_config_data_t *data);
        void data_update(unsigned int req_ctx, int result);

        /**
         * Frees the entry of route data
         *
         * @param[in] data
         *     Pointer to the node to be freed
         */
        inline void data_free(rs_config_data_t *data) {
            if (data == NULL) {
                return;
            }
            if (data->ifname) {
                free(data->ifname);
            }
            if (data->dest) {
                free(data->dest);
            }
            if (data->nhop) {
                free(data->nhop);
            }
            free(data);
        }
};

void config_data_update_status(jnx_routeserviced_config &cfg, 
                               unsigned int ctx,
                               unsigned int result);

#endif /* __JNX_ROUTESERVICED_CONFIG_H__ */
