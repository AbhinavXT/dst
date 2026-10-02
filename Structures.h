#ifndef STRUCTURES_H
#define STRUCTURES_H
#include <QString>
#include <QLineF>
#include <QRectF>
#include <QPolygonF>
#include "IRS.h"
#include "Constants.h"



// Byte-exact wire layout for the structs that are memcpy'd from a datagram.
// The pop is immediately after STRUCT_MQBUF — this used to stay pushed for
// the whole file, which meant every UI struct below (STRUCT_DISPLAY_TRACK et
// al.) was packed too. Those hold QString, qreal, QLineF, QPolygonF: packing
// them gives you misaligned pointer members, which is undefined behaviour and
// an actual alignment fault on ARM targets. Only the wire structs need it.
#pragma pack(push, 1)
// The pragma alone packs these, on GCC, Clang and MSVC alike. They also
// carried __attribute__((packed)) until session 116 — GCC-only syntax that
// MSVC rejects outright, which is what the first Windows build since the
// library split (patch 88) stopped on. The static_assert below proves the
// 7-byte header on every compiler.


typedef struct
{
    UINT_8 source_id=0;
    UINT_8 destination_id=0;
    UINT_8 message_id=0;
    UINT_16 message_len=0;
    UINT_16 kvchId = 0;


}STRUCT_MESSAGE_HEADER;


typedef struct
{
    char stringData[1501];

}STRUCT_DLC_PAYLOAD;

typedef struct
{
    STRUCT_MESSAGE_HEADER msg_header;
    STRUCT_DLC_PAYLOAD payload;

}STRUCT_DLC_MESSAGE;

//typedef struct
//{
//    CSCI_ID source_id=0;
//    CSCI_ID destination_id=0;
//    MESSAGE_ID message_id=0;
//    MESSAGE_LENGTH message_len=0;
//    PACKET_SEQ_NO packet_seq_no=0;
//    NO_OF_PACKETS no_of_packets=0;

//}STRUCT_MESSAGE_HEADER;

typedef struct
{
    STRUCT_MESSAGE_HEADER   msg_header;
    char                    my_buf[MAX_MSG_SIZE];
}STRUCT_MQBUF;

#pragma pack(pop)   // end of wire structs — everything below is host-only

// The header is memcpy'd out of a datagram, so its size is load-bearing.
static_assert(sizeof(STRUCT_MESSAGE_HEADER) == 7,
              "STRUCT_MESSAGE_HEADER must stay 7 bytes: 3x uint8 + 2x uint16, "
              "no padding. If this fires, the pack pragma is not taking "
              "effect on this compiler.");

typedef struct
{
    int id;
    int stationId=3;
    qreal start_abs_pos;
    qreal end_abs_pos;
    int track_type;
    QString track_name;
    int TRACK_WIDTH = 16;
    QString getPk()
    {
        return QString("%1_%2").arg(id).arg(stationId);
    }
    qreal getStartPosX()
    {
        return start_abs_pos*ConstantsDef::xalpha;
    }
    qreal getStartPosY()
    {
        return track_type*150;
    }
    qreal getEndPosX()
    {
        return end_abs_pos*ConstantsDef::xalpha;
    }
    qreal getEndPosY()
    {
        return track_type*150;
    }
    QLineF getTrackLine()
    {
        return QLineF(getStartPosX(),getStartPosY(),getEndPosX(),getEndPosY());
    }
    QRectF getTrackRectangle()
    {
        return QRectF(getStartPosX(),getStartPosY()-TRACK_WIDTH/2,getEndPosX()-getStartPosX(),TRACK_WIDTH);
    }
    QRectF getTrackRectangleWithPadding()
    {
        return QRectF(getStartPosX(),getStartPosY()-TRACK_WIDTH/2,getEndPosX()-getStartPosX()-5,TRACK_WIDTH);
    }

}STRUCT_DISPLAY_TRACK;


typedef struct
{
    int id;
    int stationId = 3;
    int half = 0;
    qreal start_abs_pos;
    qreal end_abs_pos;
    QString start_track_id;
    QString end_track_id;
    QString point_name;
    POINT_STATUS point_status= POINT_STRAIGHT;

    int POINT_WIDTH = 16;
    QString getPk()
    {
        return QString("%1_%2").arg(id).arg(stationId);
    }
    qreal getStartPosX()
    {
        return start_abs_pos*ConstantsDef::xalpha;
    }
    qreal getEndPosX()
    {
        return end_abs_pos*ConstantsDef::xalpha;
    }



}STRUCT_DISPLAY_POINT;


typedef struct{
    int id;
    qreal signal_pos;
    int signal_type;
    QString track_id;
    QString point_id;
    QString signal_name;

    int direction;
    int signal_height=30;
    int orientation=0;
    QString stext1="";
    QString stext2="";
    int shunt = 0;
    QString signal_color_string="";
    int baseValue=1;
    int shift=0;
    int xmlypos=0;
    int xmlxpos=0;
    int showRouteIndicaterValue=0;
    int station_id = 3;
    UINT_8 litStatus = 0;
    int is_ib_sort = 0;
    int flag_ib_sort = 0;
    SIGNAL_STATUS signalColorStatus=SIGNAL_BLANK;
    ROUTE_INDICATOR isRouteIndicator=ROUTE_INDICATOR_NONE;
    bool shuntingSignal;
    QRectF rect;
    QPolygonF buttonPolygon;
    int track_width;
    int up_down;
    QRect buttonRect;

    QString getPk()
    {
        return QString("%1_%2").arg(id).arg(station_id);
    }
    qreal getPosX()
    {
        return signal_pos*ConstantsDef::xalpha;
    }

}STRUCT_DISPLAY_SIGNAL;


typedef struct{
    int id;
    int stationId = 3;
    qreal start_abs_pos;
    qreal end_abs_pos;
    QString track_id;
    QString sectionName;
    SECTION_STATUS sectionStatus = SECTION_NOT_OCCUPIED;

    QString getPk()
    {
        return QString("%1_%2").arg(id).arg(stationId);
    }

    qreal getStartPosX()
    {
        return start_abs_pos*ConstantsDef::xalpha;
    }
    qreal getEndPosX()
    {
        return end_abs_pos*ConstantsDef::xalpha;
    }

}STRUCT_DISPLAY_SECTION;

typedef struct{
    int id;
    int stationId = 3;
    qreal start_abs_pos;
    QString track_id;
    QString berthName;
    BERTH_STATUS berthStatus = BERTH_NOT_OCCUPIED;

    QString getPk()
    {
        return QString("%1_%2").arg(id).arg(stationId);
    }

    qreal getPosX()
    {
        return start_abs_pos*ConstantsDef::xalpha;
    }


}STRUCT_DISPLAY_BERTH;

typedef struct{
    int id;
    int stationId=3;
    qreal start_abs_pos;
    QString track_id;
    QString lcgateString;
    LCGATE_STATUS lcGateStatus = LCGATE_NOT_OCCUPIED;

    QString getPk()
    {
        return QString("%1_%2").arg(id).arg(stationId);
    }

    qreal getPosX()
    {
        return start_abs_pos*ConstantsDef::xalpha;
    }


}STRUCT_DISPLAY_LCGATE;

typedef struct{
    int id;
    int stationId=3;
    qreal pos;
    QString track_id;
    QString name;

    QString getPk()
    {
        return QString("%1_%2").arg(id).arg(stationId);
    }

    qreal getPosX()
    {
        return pos*ConstantsDef::xalpha;
    }


}STRUCT_DISPLAY_RFID;


typedef struct _STRUCT_VDU_LCGATE_
{
    QString       crossing_id="";
    UINT_32       x_pos=0;
    UINT_32       y_pos=0;
    LCGATE_STATUS       status;

}STRUCT_VDU_LCGATE;

// MESSAGES STRUCTURES


typedef struct
{
    UINT_32 signal_id;
    UINT_16 station_id=3;
    SIGNAL_STATUS signal_status;
}STRUCT_SIGNAL;

typedef struct
{
    UINT_32 signal_id;
    UINT_16 station_id=3;
    SIGNAL_IB_SORT signal_ib_sort;
}STRUCT_SIGNAL_IB_SORT;

typedef struct
{
    UINT_32 signal_id;
    UINT_16 station_id=3;
    ROUTE_INDICATOR route_status;
}STRUCT_ROUTE;


typedef struct
{
    UINT_32 point_id;
    UINT_16 station_id=3;
    POINT_STATUS point_status;
}STRUCT_POINT;

typedef struct
{
    UINT_32 section_id;
    UINT_16 station_id=3;
    SECTION_STATUS section_status;
}STRUCT_SECTION;

typedef struct
{
    UINT_32 berth_id;
    UINT_16 station_id=3;
    BERTH_STATUS berth_status;
}STRUCT_BERTH_TRACK;

typedef struct
{
    UINT_32 lcgate_id;
    UINT_16 station_id=3;
    LCGATE_STATUS lcgate_status;
}STRUCT_LCGATE;

typedef struct
{
    UINT_32 no_of_signal;
    STRUCT_SIGNAL signal_data[MAX_NO_OF_SIGNALS];
}STRUCT_SIGNAL_LIST;

typedef struct
{
    UINT_32 no_of_signal;
    STRUCT_ROUTE route_data[MAX_NO_OF_SIGNALS];
}STRUCT_ROUTE_LIST;

typedef struct{
    UINT_32 no_of_points;
    STRUCT_POINT point_data[MAX_NO_OF_POINTS];
}STRUCT_POINT_LIST;

typedef struct
{
    UINT_32 no_of_section;
    STRUCT_SECTION section_data[MAX_NO_OF_SECTION];
}STRUCT_SECTION_LIST;

typedef struct
{
    UINT_32 no_of_berth;
    STRUCT_BERTH_TRACK berth_data[MAX_NO_OF_BERTH];
}STRUCT_BERTH_LIST;

typedef struct
{
    UINT_32 no_of_lcgate;
    STRUCT_LCGATE lcgate_data[MAX_NO_OF_BERTH];
}STRUCT_LCGATE_LIST;


typedef struct
{
    MSG_TYPE msgType = MSG_TYPE_1;
}STRUCT_MSGTYPE;

typedef struct
{
    FLICKER_TYPE flickerType = FLICKER_TYPE_OFF;
}STRUCT_FLICKER;

typedef struct
{
    UINT_8 init_req = 1;
}STRUCT_INIT;


typedef struct
{
    UINT_32 riuId;
    UINT_8 direction;
    UINT_16 stationId;

    UINT_8 linkStatus1;
    UINT_8 acInputHealth1;
    UINT_8 chargerHealth1;
    UINT_8 batteryHealth1;

    UINT_8 linkStatus2;
    UINT_8 acInputHealth2;
    UINT_8 chargerHealth2;
    UINT_8 batteryHealth2;
}STRUCT_RIU_STATUS;

// (pack pragma was popped right after STRUCT_MQBUF, above)



#endif // STRUCTURES_H
