/*
 * $Id: jnx-routeserviced_ssd.h 346460 2009-11-14 05:06:47Z ssiano $
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

#ifndef __JNX_ROUTESERVICED_SSD_H__
#define __JNX_ROUTESERVICED_SSD_H__

/*
 * Gencfg Blog key for client-id 
*/
#define JNX_ROUTESERVICED_CLIENT_ID_BLOB_KEY  0x00000001

/*
 * Gencfg Blog ID for client-id 
 */
#define JNX_ROUTESERVICED_CLIENT_ID_BLOB_ID   0x00000001


/**
 * Class definition for SSD related attributes and methods
 */
class jnx_routeserviced_ssd {
    public:
        /**
         * Destructor
         */
        ~jnx_routeserviced_ssd() {}
        static jnx_routeserviced_ssd &Instance();

        int init(evContext ev_ctx);
        void set_reconnect_timer(void);
        void process_client_id_restore_msg(void);
        void add_route(char *destination, char *nhop, char *family,
                      unsigned int prefixlen, unsigned int req_ctx);
        void del_route(char *destination, char *nhop, char *family,
                      unsigned int prefixlen, unsigned int req_ctx);

        /**
         * Get server file descriptor
         */
        inline int get_server_fd() {return server_fd;}
        
        /**
         * Set server file descriptor
         */
        inline void set_server_fd(int fd) {server_fd = fd;}
        
        /**
         * Get my client id provided by the server
         */
        inline int get_client_id() {return client_id;}
        
        /**
         * Store my client provider by the server
         */
        inline void set_client_id(int cid) {client_id = cid;}
        
        /**
         * Get routing table id
         */
        inline int get_rtt_id() {return rtt_id;}
        
        /**
         * Store routing table id
         */
        inline void set_rtt_id(int rttid) {rtt_id = rttid;}

        /**
         * Get pending status in unsigned int
         */
        inline int get_pending_status() { 
            return static_cast<unsigned int>(RT_ADD_REQ_STAT_PENDING);
        }

        /**
         * Get success status in unsigned int
         */
        inline int get_success_status() { 
            return static_cast<unsigned int>(RT_ADD_REQ_STAT_SUCCESS);
        }

        /**
         * Get failure status in unsigned int
         */
        inline int get_failure_status() { 
            return static_cast<unsigned int>(RT_ADD_REQ_STAT_FAILURE);
        }

        /**
         * Set the application's state as disconnected with SSD
         */
        inline void set_state_restarting() {
            jnx_routeserviced_state = JNX_RS_STATE_RESTARTING;
        }

        /**
         * Set the application's state as disconnected with SSD
         */
        inline void set_state_ssd_disconnected() {
            jnx_routeserviced_state = JNX_RS_STATE_SSD_DISCONNECTED;
        }

        /**
         * Set the application's state as disconnected with SSD
         */
        inline void set_state_client_id_restored() {
            jnx_routeserviced_state = JNX_RS_STATE_SSD_CLIENT_ID_RESTORED;
        }

        void deque_route_add_req_by_ctx(unsigned int ctx);
        void deque_route_del_req_by_ctx(unsigned int ctx);

        /**
         * Request client-id from SSD
         */
        int request_client_id();

        void gencfg_update_client_id(int cid) {
            gencfg_delete_client_id(0);
            gencfg_store_client_id(cid);
        }

    private:
        /*
         * Hide the constructor, copy constructor and assignment operator
         * to disallow more than one instances of the class
         */
        /**
         * Private constructor
         */
        jnx_routeserviced_ssd() {}

        /**
         * Private copy constructor
         *
         * @param[in] old
         *     Reference to the object to be copied
         */
        jnx_routeserviced_ssd(const jnx_routeserviced_ssd &old);

        /**
         * Private assignment operator
         *
         * @param[in] old
         *     Reference to the object to be assigned 
         * 
         * @return
         *     Reference to the new object to be assigned 
         */
        const jnx_routeserviced_ssd &operator=(const
                                               jnx_routeserviced_ssd &old);

        evContext event_ctx;
        uint32_t rtt_id;            /**< Routing table instance id */
        int       client_id;         /**< My client id */
        int       server_fd;         /**< Server file descriptor */

        static auto_ptr<jnx_routeserviced_ssd> unique_instance; /**< Unique
                                                                   class 
                                                                   instance */

        /**
         * An enum definition for route addition requests status
         */
        typedef enum jnx_routeserviced_state_e {
            JNX_RS_STATE_RESTARTING,
            JNX_RS_STATE_SSD_DISCONNECTED,
            JNX_RS_STATE_SSD_CLIENT_ID_RESTORED
        } jnx_routeserviced_state_t;

        /**
         * An enum definition for route addition requests status
         */
        enum jnx_routeserviced_route_add_status {
            RT_ADD_REQ_STAT_PENDING,
            RT_ADD_REQ_STAT_SUCCESS,
            RT_ADD_REQ_STAT_FAILURE
        };

        /**
         * Class definition with route addition attributes
         */
        struct rs_route_add_req {
            struct ssd_route_parms rtp;             /**< Route parameters */
            ssd_sockaddr_un        route_addr;      /**< Destination address */
            ssd_sockaddr_un        route_nh;        /**< Next hop address for 
                                                      destination */
            ssd_sockaddr_un        route_local;     /**< Local address 
                                                      associated 
                                                      with interface */
            unsigned int           ctx;             /**< Context for the 
                                                      request */
            TAILQ_ENTRY(rs_route_add_req) entries;  /** < Tailq entry */
        };

        /**
         * Class definition with route deletion attributes
         */
        struct rs_route_del_req {
            struct ssd_rt_delete_parms rtp;             /**< Route parameters */
            ssd_sockaddr_un            route_addr;      /**< Destination 
                                                          address */
            unsigned int               ctx;             /**< Context for the 
                                                          request */
            TAILQ_ENTRY(rs_route_del_req) entries;      /**< Tailq entry */
        };

        /**
         * Head for the route addition request queue
         */
        TAILQ_HEAD(, rs_route_add_req) route_add_queue;
        /**
         * Head for the route deletion request queue
         */
        TAILQ_HEAD(, rs_route_del_req) route_del_queue;

        /**
         * Variable to keep track of the application's state
         */
         jnx_routeserviced_state_t jnx_routeserviced_state;

        int connect(evContext ev_ctx);
        static void reconnect(evContext ev_ctx, void *uap __unused,
                              struct timespec due __unused,
                              struct timespec inter __unused);
        const char* get_prefix_by_ifl (ifl_idx_t);
        int get_idx_by_iflname (const char *iflname, ifl_idx_t *idx);

        /**
         * Enques route addition requests in the tail queue
         */
        inline void enque_route_add_req(rs_route_add_req * add_req) {
            TAILQ_INSERT_TAIL(&route_add_queue, add_req, entries);
        }

        /**
         * Enques route deletion requests in the tail queue
         */
        inline void enque_route_del_req(rs_route_del_req * del_req) { 
            TAILQ_INSERT_TAIL(&route_del_queue, del_req, entries);
        }

        int gencfg_init(void);
        void gencfg_store_client_id(int id);
        int gencfg_get_client_id(int *id);
        static void gencfg_delete_client_id(int id __unused);
};

#endif /* ! __JNX_ROUTESERVICED_SSD_H__ */
