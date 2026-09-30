#ifndef CONSTANTS_H
#define CONSTANTS_H

typedef unsigned char   UINT_8;    /* Unsigned  8 bit integer */
typedef char            INT_8;     /* Signed    8 bit integer */
typedef unsigned short  UINT_16;   /* Unsigned 16 bit integer */
typedef short           INT_16;    /* Signed   16 bit integer */
typedef unsigned int    UINT_32;   /* Unsigned 32 bit integer */
typedef int             INT_32;    /* Signed   32 bit integer */
typedef unsigned long   UINT_64;   /* Unsigned 64 bit integer */
typedef long            INT_64;
typedef float           REAL_32;   /* IEEE     32 bit floating point */
typedef double          REAL_64;   /* IEEE     64 bit floating point */

int const NO_OF_MSG_IN_QUEUE = 3000;
int const MAX_MSG_SIZE = 8000;
int const MAX_SENT_MSG_DATA_SIZE = 2000;

int const MAX_NO_OF_SIGNALS = 200;
int const MAX_NO_OF_POINTS = 200;
int const MAX_NO_OF_SECTION = 200;
int const MAX_NO_OF_BERTH = 200;
int const MAX_NO_OF_LCGATE = 200;

#define     POINT_STRAIGHT                 1
#define     POINT_BRANCH                   2
#define     POINT_NONE                   3

#define     SECTION_OCCUPIED                 1
#define     SECTION_NOT_OCCUPIED                   2


#define     BERTH_OCCUPIED                 1
#define     BERTH_NOT_OCCUPIED                   2
#define     BERTH_NONE                  3

#define     LCGATE_OCCUPIED                 1
#define     LCGATE_NOT_OCCUPIED                   2

#define     MSG_TYPE_1                 1
#define     MSG_TYPE_2                   2
#define     MSG_TYPE_3                   3
#define     MSG_TYPE_4                   4
#define     MSG_TYPE_5                   5

#define     FLICKER_TYPE_OFF                 0
#define     FLICKER_TYPE_ON                   1


//SIGNAL UP DOWN
#define SIGNAL_UP   1
#define SIGNAL_DOWN 2
#define SIGNAL_NA 0

// MESSAGE IDS
#define BACKEND_DISPLAY_ACK_SIGNAL_STATUS 1
#define BACKEND_DISPLAY_ACK_ROUTE_STATUS 2
#define BACKEND_DISPLAY_ACK_POINT_STATUS 3
#define BACKEND_DISPLAY_ACK_SECTION_STATUS 4
#define BACKEND_DISPLAY_ACK_BERTH_STATUS 5
#define BACKEND_DISPLAY_ACK_LCGATE_STATUS 6


#define BACKEND_DISPLAY_ALL_SIGNAL_STATUS 101
#define BACKEND_DISPLAY_ALL_ROUTE_STATUS 102
#define BACKEND_DISPLAY_ALL_POINT_STATUS 103
#define BACKEND_DISPLAY_ALL_SECTION_STATUS 104
#define BACKEND_DISPLAY_ALL_BERTH_STATUS 105
#define BACKEND_DISPLAY_ALL_LCGATE_STATUS 106


#define DISPLAY_BACKEND_SIGNAL_STATUS 201
#define DISPLAY_BACKEND_ROUTE_STATUS 202
#define DISPLAY_BACKEND_POINT_STATUS 203
#define DISPLAY_BACKEND_SECTION_STATUS 204
#define DISPLAY_BACKEND_BERTH_STATUS 205
#define DISPLAY_BACKEND_LCGATE_STATUS 206
#define DISPLAY_BACKEND_SIGNAL_IB_SORT_STATUS 207


#define DISPLAY_BACKEND_REQ_ALL_STATUS 501
#define DISPLAY_BACKEND_MSGTYPE 502
#define DISPLAY_BACKEND_FLICKER 503
#define DISPLAY_BACKEND_RIU_STATUS 504







// --


class ConstantsDef{
public:
    static double xalpha;

    static ConstantsDef& getInstance(){
        //the only instance
        //guaranteed to be lazy initialized

        //guaranteed will be destroyed correctely
        static ConstantsDef instance;
        return instance;
    }

};

#endif // CONSTANTS_H
