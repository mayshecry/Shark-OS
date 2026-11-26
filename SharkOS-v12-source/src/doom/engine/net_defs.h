  
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                              
  

#ifndef NET_DEFS_H
#define NET_DEFS_H 

#include <stdio.h>

#include "doomtype.h"
#include "d_ticcmd.h"
#include "sha1.h"

                                                                        
                                                                       
                            

#define MAXNETNODES 16

                                                         
                                                                         
                                                            

#define NET_MAXPLAYERS 8

                                     

#define MAXPLAYERNAME 30

                                        

#define BACKUPTICS 128

typedef struct _net_module_s net_module_t;
typedef struct _net_packet_s net_packet_t;
typedef struct _net_addr_s net_addr_t;
typedef struct _net_context_s net_context_t;

struct _net_packet_s
{
    byte *data;
    size_t len;
    size_t alloced;
    unsigned int pos;
};

struct _net_module_s
{
                                                 

    boolean (*InitClient)(void);

                                                 

    boolean (*InitServer)(void);

                    

    void (*SendPacket)(net_addr_t *addr, net_packet_t *packet);

                                       
      
                                      

    boolean (*RecvPacket)(net_addr_t **addr, net_packet_t **packet);

                                      

    void (*AddrToString)(net_addr_t *addr, char *buffer, int buffer_len);

                                                 

    void (*FreeAddress)(net_addr_t *addr);

                                          

    net_addr_t *(*ResolveAddress)(char *addr);
};

             

struct _net_addr_s
{
    net_module_t *module;
    void *handle;
};

                                                                    

#define NET_MAGIC_NUMBER 3436803284U

                                                                     

#define NET_RELIABLE_PACKET (1 << 15)

               

typedef enum
{
    NET_PACKET_TYPE_SYN,
    NET_PACKET_TYPE_ACK,
    NET_PACKET_TYPE_REJECTED,
    NET_PACKET_TYPE_KEEPALIVE,
    NET_PACKET_TYPE_WAITING_DATA,
    NET_PACKET_TYPE_GAMESTART,
    NET_PACKET_TYPE_GAMEDATA,
    NET_PACKET_TYPE_GAMEDATA_ACK,
    NET_PACKET_TYPE_DISCONNECT,
    NET_PACKET_TYPE_DISCONNECT_ACK,
    NET_PACKET_TYPE_RELIABLE_ACK,
    NET_PACKET_TYPE_GAMEDATA_RESEND,
    NET_PACKET_TYPE_CONSOLE_MESSAGE,
    NET_PACKET_TYPE_QUERY,
    NET_PACKET_TYPE_QUERY_RESPONSE,
    NET_PACKET_TYPE_LAUNCH,
} net_packet_type_t;

typedef enum
{
    NET_MASTER_PACKET_TYPE_ADD,
    NET_MASTER_PACKET_TYPE_ADD_RESPONSE,
    NET_MASTER_PACKET_TYPE_QUERY,
    NET_MASTER_PACKET_TYPE_QUERY_RESPONSE,
    NET_MASTER_PACKET_TYPE_GET_METADATA,
    NET_MASTER_PACKET_TYPE_GET_METADATA_RESPONSE,
    NET_MASTER_PACKET_TYPE_SIGN_START,
    NET_MASTER_PACKET_TYPE_SIGN_START_RESPONSE,
    NET_MASTER_PACKET_TYPE_SIGN_END,
    NET_MASTER_PACKET_TYPE_SIGN_END_RESPONSE,
} net_master_packet_type_t;

                                                             

typedef struct
{
    int gamemode;
    int gamemission;
    int lowres_turn;
    int drone;
    int max_players;
    int is_freedoom;
    sha1_digest_t wad_sha1sum;
    sha1_digest_t deh_sha1sum;
    int player_class;
} net_connect_data_t;

                                                                     
                                                                

typedef struct
{
    int ticdup;
    int extratics;
    int deathmatch;
    int episode;
    int nomonsters;
    int fast_monsters;
    int respawn_monsters;
    int map;
    int skill;
    int gameversion;
    int lowres_turn;
    int new_sync;
    int timelimit;
    int loadgame;
    int random;                  

                                                                   
                     

    int num_players;
    int consoleplayer;

                            

    int player_classes[NET_MAXPLAYERS];

} net_gamesettings_t;

#define NET_TICDIFF_FORWARD      (1 << 0)
#define NET_TICDIFF_SIDE         (1 << 1)
#define NET_TICDIFF_TURN         (1 << 2)
#define NET_TICDIFF_BUTTONS      (1 << 3)
#define NET_TICDIFF_CONSISTANCY  (1 << 4)
#define NET_TICDIFF_CHATCHAR     (1 << 5)
#define NET_TICDIFF_RAVEN        (1 << 6)
#define NET_TICDIFF_STRIFE       (1 << 7)

typedef struct
{
    unsigned int diff;
    ticcmd_t cmd;
} net_ticdiff_t;

                                           

typedef struct 
{
    signed int latency;
    unsigned int seq;
    boolean playeringame[NET_MAXPLAYERS];
    net_ticdiff_t cmds[NET_MAXPLAYERS];
} net_full_ticcmd_t;

                                          

typedef struct
{
    char *version;
    int server_state;
    int num_players;
    int max_players;
    int gamemode;
    int gamemission;
    char *description;
} net_querydata_t;

                                                               

typedef struct
{
    int num_players;
    int num_drones;
    int ready_players;
    int max_players;
    int is_controller;
    int consoleplayer;
    char player_names[NET_MAXPLAYERS][MAXPLAYERNAME];
    char player_addrs[NET_MAXPLAYERS][MAXPLAYERNAME];
    sha1_digest_t wad_sha1sum;
    sha1_digest_t deh_sha1sum;
    int is_freedoom;
} net_waitdata_t;

#endif                         
