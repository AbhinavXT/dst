#ifndef IRS_H
#define IRS_H
#include "Constants.h"


#define DOWNLINE 1
#define UPLINE 2

#define UPLEFT 0
#define UPRIGHT 1
#define DOWNLEFT 2
#define DOWNRIGHT 3


typedef UINT_8  SIGNAL_STATUS;
typedef UINT_8  ROUTE_INDICATOR;

typedef UINT_8  SIGNAL_IB_SORT;
#define  SIGNAL_IB_SORT_OFF    0
#define  SIGNAL_IB_SORT_ON     1


//SIGNAL TYPE
#define SIGNAL_VIRTUAL          1
#define SIGNAL_NORMAL           2
#define SIGNAL_OVERLAP          3
#define SIGNAL_SHUNTING         4
#define SIGNAL_EMG              5
#define SIGNAL_DEP_SHUNT        6

//SIGNAL DIRECTION
#define SIGNAL_RIGHT_END                1
#define SIGNAL_LEFT_END                 2
#define SIGNAL_RIGHT_MID                3
#define SIGNAL_LEFT_MID                 4
#define SIGNAL_RIGHT_END_LEFT_DIR       5
#define SIGNAL_LEFT_END_RIGHT_DIR       6

// SHUNT SIG DIRECTION
#define SHUNT_SIGNAL_UP_RIGHT           1
#define SHUNT_SIGNAL_DOWN_LEFT          2


#define     SIGNAL_STOP                     0
#define     SIGNAL_YELLOW                   2
//#define     SIGNAL_VOILET                   1
#define     SIGNAL_GREEN                    1
//#define     SIGNAL_FLICK_RED                3
//#define     SIGNAL_FLICK_GREEN              4
//#define     SIGNAL_FLICK_VIOLET             5
#define     SIGNAL_DOUBLE                   3
#define     SIGNAL_BLANK                    7

#define     ROUTE_INDICATOR_NONE              0
#define     ROUTE_INDICATOR_POS1              1
#define     ROUTE_INDICATOR_POS2               2
#define     ROUTE_INDICATOR_POS3               3
#define     ROUTE_INDICATOR_POS4               4
#define     ROUTE_INDICATOR_POS5               5
#define     ROUTE_INDICATOR_POS6               6

#define     SIGNAL_LIT_OFF               21
#define     SIGNAL_LIT_ON               22


#define virtualSignalWidth      1.5
#define signalWidth             3.7
#define smallLine               2*signalWidth  //6.0
#define smallColorfulRect       5//3
#define smallWhiteRect          1.3*smallLine//(smallLine+2)
#define bufferSpace             3
#define smallestBuffer          0.75
#define yposBufferFromTrackUp   2
#define yposBufferFromTrackDown 2

typedef UINT_8  POINT_STATUS; //1. straight, 2. branching off
typedef UINT_8 SECTION_STATUS; //1. Occupied 2. Not Occupied
typedef UINT_8 BERTH_STATUS; //1. Occupied 2. Not Occupied
typedef UINT_8 LCGATE_STATUS; //1. Occupied 2. Not Occupied
typedef UINT_8 MSG_TYPE; //1. TYPE1 2. TYPE2
typedef UINT_8 FLICKER_TYPE; //1. ON 0. OFF


// CSCI ID
typedef UINT_8 CSCI_ID;
typedef UINT_16 MESSAGE_ID;
typedef UINT_16 MESSAGE_LENGTH;
typedef UINT_16 NO_OF_PACKETS;
typedef UINT_16 PACKET_SEQ_NO;

#endif // IRS_H
