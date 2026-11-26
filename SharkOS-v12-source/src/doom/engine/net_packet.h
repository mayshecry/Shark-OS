  
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                              
  

#ifndef NET_PACKET_H
#define NET_PACKET_H

#include "net_defs.h"

net_packet_t *NET_NewPacket(int initial_size);
net_packet_t *NET_PacketDup(net_packet_t *packet);
void NET_FreePacket(net_packet_t *packet);

boolean NET_ReadInt8(net_packet_t *packet, unsigned int *data);
boolean NET_ReadInt16(net_packet_t *packet, unsigned int *data);
boolean NET_ReadInt32(net_packet_t *packet, unsigned int *data);

boolean NET_ReadSInt8(net_packet_t *packet, signed int *data);
boolean NET_ReadSInt16(net_packet_t *packet, signed int *data);
boolean NET_ReadSInt32(net_packet_t *packet, signed int *data);

char *NET_ReadString(net_packet_t *packet);

void NET_WriteInt8(net_packet_t *packet, unsigned int i);
void NET_WriteInt16(net_packet_t *packet, unsigned int i);
void NET_WriteInt32(net_packet_t *packet, unsigned int i);

void NET_WriteString(net_packet_t *packet, char *string);

#endif                           

