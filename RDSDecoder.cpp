/* Arduino RDS/RBDS (IEC 62016/NRSC-4-B) Decoding Library
 * See the README file for author and licensing information. In case it's
 * missing from your distribution, use the one here as the authoritative
 * version: https://github.com/csdexter/RDSDecoder/blob/master/README
 *
 * This library is for decoding RDS/RBDS data streams (groups).
 * See the example sketches to learn how to use the library in your code.
 *
 * This is the main code file for the library.
 * See the header file for better function documentation.
 */

#include "RDSDecoder.h"
#include "RDSDecoder-private.h"

#include <string.h>

#if defined(__GNUC__)
# if defined(__i386__) || defined(__x86_64__)
#  define PROGMEM
#  define PGM_P char *
#  define strncpy_P strncpy
#  define lowByte(x) (uint8_t)((x) & 0xFF)
#  define highByte(x) (uint8_t)(((x) >> 8) & 0xFF)
#  define pgm_read_byte(x) (uint8_t)(*x)
#  define pgm_read_ptr(x) (void *)(*x)
# endif
#else
# warning Non-GNU compiler detected, you are on your own!
#endif

void RDSDecoder::registerCallback(byte type, TRDSCallback callback){
    if (type < sizeof(_callbacks) / sizeof(_callbacks[0]))
        _callbacks[type] = callback;
};

void RDSDecoder::decodeRDSGroup(word block[]){
    byte grouptype;
    word fourchars[2];

    _status.programIdentifierUS = block[0];
    grouptype = lowByte((block[1] & RDS_TYPE_MASK) >> RDS_TYPE_SHR);
    _status.TP = block[1] & RDS_TP;
    _status.PTY = lowByte((block[1] & RDS_PTY_MASK) >> RDS_PTY_SHR);

    switch(grouptype){
        case RDS_GROUP_0A:
        case RDS_GROUP_0B:
        case RDS_GROUP_15B:
            byte DIPSA;
            word twochars;

            _status.TA = block[1] & RDS_TA;
            _status.MS = block[1] & RDS_MS;
            DIPSA = lowByte(block[1] & RDS_DIPS_ADDRESS);
            if(block[1] & RDS_DI)
                _status.DICC |= (0x1 << (3 - DIPSA));
            else
                _status.DICC &= ~(0x1 << (3 - DIPSA));
            if(grouptype != RDS_GROUP_15B) {
                twochars = swab(block[3]);
                strncpy(&_status.programService[DIPSA * 2],
                        (char *)&twochars, 2);
            };
            if(grouptype == RDS_GROUP_0A) {
                if (_callbacks[RDS_CALLBACK_AF])
                    _callbacks[RDS_CALLBACK_AF](0x00, true, block[2], 0x00);
            }
            break;
        case RDS_GROUP_1A:
            _status.linkageActuator = block[2] & RDS_SLABEL_LA;
            switch((block[2] & RDS_SLABEL_MASK) >> RDS_SLABEL_SHR) {
                case RDS_SLABEL_TYPE_PAGINGECC:
                    _status.extendedCountryCode = lowByte(block[2]);
                    _status.pagingOperatorCode = highByte(block[2]) & 0x0F;
                    break;
                case RDS_SLABEL_TYPE_TMCID:
                    _status.tmcIdentification = block[2] & RDS_SLABEL_VALUE_MASK;
                    break;
                case RDS_SLABEL_TYPE_PAGINGID:
                    _status.pagingIdentification = block[2] & RDS_SLABEL_VALUE_MASK;
                    break;
                case RDS_SLABEL_TYPE_LANGUAGE:
                    _status.languageCode = lowByte(block[2]);
                    break;
            };
        case RDS_GROUP_1B:
            _status.programItemNumber = block[3];
            break;
        case RDS_GROUP_2A:
        case RDS_GROUP_2B:
            byte RTA, RTAW;

            if((block[1] & RDS_TEXTAB) != _rdstextab) {
                _rdstextab = !_rdstextab;
                memset(_status.radioText, ' ', sizeof(_status.radioText) - 1);
            }
            RTA = lowByte(block[1] & RDS_TEXT_ADDRESS);
            RTAW = (grouptype == RDS_GROUP_2A) ? 4 : 2;
            fourchars[0] = swab(block[(grouptype == RDS_GROUP_2A) ? 2 : 3]);
            if(grouptype == RDS_GROUP_2A)
                fourchars[1] = swab(block[3]);
            strncpy(&_status.radioText[RTA * RTAW], (char *)fourchars, RTAW);
            break;
        case RDS_GROUP_3A:
            switch(block[3]){
                case RDS_AID_DEFAULT:
                    if (block[1] & RDS_ODA_GROUP_MASK == RDS_GROUP_8A) {
                      _status.TMC.carriedInGroup = RDS_GROUP_8A;
                      _status.TMC.message = block[2];
                    };
                    break;
                case RDS_AID_IRDS:
                    _status.IRDS.carriedInGroup = block[1] & RDS_ODA_GROUP_MASK;
                    _status.IRDS.message = block[2];
                    break;
                case RDS_AID_TMC:
                    _status.TMC.carriedInGroup = block[1] & RDS_ODA_GROUP_MASK;
                    _status.TMC.message = block[2];
                    break;
                default:
                    if (_callbacks[RDS_CALLBACK_AID])
                        _callbacks[RDS_CALLBACK_AID](
                            block[1] & RDS_ODA_GROUP_MASK, true,
                            block[2], block[3]);
            };
            break;
        case RDS_GROUP_3B:
        case RDS_GROUP_4B:
        case RDS_GROUP_6A:
        case RDS_GROUP_6B:
        case RDS_GROUP_7B:
        case RDS_GROUP_8B:
        case RDS_GROUP_9B:
        case RDS_GROUP_10B:
        case RDS_GROUP_11A:
        case RDS_GROUP_11B:
        case RDS_GROUP_12A:
        case RDS_GROUP_12B:
        case RDS_GROUP_13B:
            if (_callbacks[RDS_CALLBACK_ODA])
                _callbacks[RDS_CALLBACK_ODA](
                    block[1] & RDS_ODA_GROUP_MASK, ! (grouptype & 0x01),
                    ((grouptype & 0x01) ? 0x00 : block[2]), block[3]);

            break;
        case RDS_GROUP_4A:
            unsigned long MJD, CT, ys;
            word yp;
            byte k, mp;

            CT = ((unsigned long)block[2] << 16) | block[3];
            //The standard mandates that CT must be all zeros if no time
            //information is being provided by the current station.
            if(!CT) break;

            _havect = true;
            MJD = (unsigned long)(block[1] & RDS_TIME_MJD1_MASK) <<
                  RDS_TIME_MJD1_SHL;
            MJD |= (CT & RDS_TIME_MJD2_MASK) >> RDS_TIME_MJD2_SHR;

            _time.tm_hour = (CT & RDS_TIME_HOUR_MASK) >> RDS_TIME_HOUR_SHR;
            _time.tm_tz = CT & RDS_TIME_TZ_MASK;
            if (CT & RDS_TIME_TZ_SIGN)
              _time.tm_tz = - _time.tm_tz;
            _time.tm_min = (CT & RDS_TIME_MINUTE_MASK) >> RDS_TIME_MINUTE_SHR;
            //Use integer arithmetic at all costs, Arduino lacks an FPU
            yp = (MJD * 10 - 150782) * 10 / 36525;
            ys = yp * 36525 / 100;
            mp = (MJD * 10 - 149561 - ys * 10) * 1000 / 306001;
            _time.tm_mday = MJD - 14956 - ys - mp * 306001 / 10000;
            k = (mp == 14 || mp == 15) ? 1 : 0;
            _time.tm_year = 1900 + yp + k;
            _time.tm_mon = mp - 1 - k * 12;
            _time.tm_wday = (MJD + 2) % 7 + 1;
            break;
        case RDS_GROUP_5A:
        case RDS_GROUP_5B:
            if (_callbacks[RDS_CALLBACK_TDC])
                _callbacks[RDS_CALLBACK_TDC](
                    block[1] & RDS_ODA_GROUP_MASK,
                    (grouptype == RDS_GROUP_5A),
                    ((grouptype == RDS_GROUP_5A) ? block[2] : 0x00),
                    block[3]);
            break;
        case RDS_GROUP_7A:
            //TODO: read the standard and do Radio Paging
            break;
        case RDS_GROUP_8A:
            //TODO: read the standard and do TMC listing
            break;
        case RDS_GROUP_9A:
            //NOTE: EWS is defined per-country which is a polite way of saying
            //      there is no standard and it's never going to work. Pity!
            break;
        case RDS_GROUP_10A:
            if((block[1] & RDS_PTYNAB) != _rdsptynab) {
                _rdsptynab = !_rdsptynab;
                memset(_status.programTypeName, ' ', 8);
            }
            fourchars[0] = swab(block[2]);
            fourchars[1] = swab(block[3]);
            strncpy(&_status.programTypeName[(block[1] & RDS_PTYN_ADDRESS) * 4],
                    (char *)&fourchars, 4);
            break;
        case RDS_GROUP_13A:
            //TODO: read the standard and do Enhanced Radio Paging
            break;
        case RDS_GROUP_14A:
            switch(block[1] & RDS_EON_MASK){
                case RDS_EON_TYPE_PS_SA0:
                case RDS_EON_TYPE_PS_SA1:
                case RDS_EON_TYPE_PS_SA2:
                case RDS_EON_TYPE_PS_SA3:
                    twochars = swab(block[2]);
                    strncpy(
                        &_status.EON.programService[(block[1] & RDS_EON_MASK) * 2],
                        (char *)&twochars, 2);
                    break;
                case RDS_EON_TYPE_AF:
                    if (_callbacks[RDS_CALLBACK_EON])
                        _callbacks[RDS_CALLBACK_EON](1, true, block[2], 0x00);
                    break;
                case RDS_EON_TYPE_MF_FM0:
                case RDS_EON_TYPE_MF_FM1:
                case RDS_EON_TYPE_MF_FM2:
                case RDS_EON_TYPE_MF_FM3:
                    if (_callbacks[RDS_CALLBACK_EON])
                        _callbacks[RDS_CALLBACK_EON](2, true, block[2], 0x00);
                    break;
                case RDS_EON_TYPE_MF_AM:
                    if (_callbacks[RDS_CALLBACK_EON])
                        _callbacks[RDS_CALLBACK_EON](3, true, block[2], 0x00);
                    break;
                case RDS_EON_TYPE_LINKAGE:
                    memcpy(&_status.EON.linkageInformation, &block[2],
                           sizeof(_status.EON.linkageInformation));
                    break;
                case RDS_EON_TYPE_PTYTA:
                    _status.EON.PTY = (block[2] & RDS_EON_PTY_MASK) >> RDS_EON_PTY_SHR;
                    _status.EON.TA = block[2] & RDS_EON_TA_A;
                    break;
                case RDS_EON_TYPE_PIN:
                    _status.EON.programItemNumber = block[2];
                    break;
            };
        case RDS_GROUP_14B:
            _status.EON.TP = block[1] & RDS_EON_TP;
            _status.EON.programIdentifier = block[3];
            if (grouptype == RDS_GROUP_14B)
                _status.EON.TA = block[1] & RDS_EON_TA_B;
               //TODO: implement PTY(ON): News/Weather/Alarm
            break;
        case RDS_GROUP_15A:
            //Withdrawn and currently unallocated, ignore
            break;
    }
}

void RDSDecoder::getRDSData(TRDSData* rdsdata){
    makePrintable(_status.programService);
    makePrintable(_status.programTypeName);
    makePrintable(_status.radioText);
    makePrintable(_status.EON.programService);

    *rdsdata = _status;
}

bool RDSDecoder::getRDSTime(TRDSTime* rdstime){
    if(_havect && rdstime) *rdstime = _time;

    return _havect;
}

void RDSDecoder::resetRDS(void){
    memset(&_status, 0x00, sizeof(_status));
    memset(_status.programService, ' ', sizeof(_status.programService) - 1);
    memset(_status.programTypeName, ' ', sizeof(_status.programTypeName) - 1);
    memset(_status.radioText, ' ', sizeof(_status.radioText) - 1);
    _rdstextab = false;
    _rdsptynab = false;
    _havect = false;
}

void RDSDecoder::makePrintable(char* str){
    for(byte i = 0; i < strlen(str); i++) {
        if(str[i] == 0x0D) {
            str[i] = '\0';
            break;
        }
        //TODO: implement codepages from standard and do full decoding.
        if(str[i] < 32 || str[i] > 126) str[i] = '?';
    }
}

const char PTY2Text_S_None[] PROGMEM = "None/Undefined";
const char PTY2Text_S_News[] PROGMEM = "News";
const char PTY2Text_S_Current[] PROGMEM = "Current affairs";
const char PTY2Text_S_Information[] PROGMEM = "Information";
const char PTY2Text_S_Sports[] PROGMEM = "Sports";
const char PTY2Text_S_Education[] PROGMEM = "Education";
const char PTY2Text_S_Drama[] PROGMEM = "Drama";
const char PTY2Text_S_Culture[] PROGMEM = "Culture";
const char PTY2Text_S_Science[] PROGMEM = "Science";
const char PTY2Text_S_Varied[] PROGMEM = "Varied";
const char PTY2Text_S_Pop[] PROGMEM = "Pop";
const char PTY2Text_S_Rock[] PROGMEM = "Rock";
const char PTY2Text_S_EasySoft[] PROGMEM = "Easy & soft";
const char PTY2Text_S_Classical[] PROGMEM = "Classical";
const char PTY2Text_S_Other[] PROGMEM = "Other music";
const char PTY2Text_S_Weather[] PROGMEM = "Weather";
const char PTY2Text_S_Finance[] PROGMEM = "Finance";
const char PTY2Text_S_Children[] PROGMEM = "Children's";
const char PTY2Text_S_Social[] PROGMEM = "Social affairs";
const char PTY2Text_S_Religion[] PROGMEM = "Religion";
const char PTY2Text_S_TalkPhone[] PROGMEM = "Talk & phone-in";
const char PTY2Text_S_Travel[] PROGMEM = "Travel";
const char PTY2Text_S_Leisure[] PROGMEM = "Leisure";
const char PTY2Text_S_Jazz[] PROGMEM = "Jazz";
const char PTY2Text_S_Country[] PROGMEM = "Country";
const char PTY2Text_S_National[] PROGMEM = "National";
const char PTY2Text_S_Oldies[] PROGMEM = "Oldies";
const char PTY2Text_S_Folk[] PROGMEM = "Folk";
const char PTY2Text_S_Documentary[] PROGMEM = "Documentary";
const char PTY2Text_S_EmergencyTest[] PROGMEM = "Emergency test";
const char PTY2Text_S_Emergency[] PROGMEM = "Emergency";
const char PTY2Text_S_Adult[] PROGMEM = "Adult hits";
const char PTY2Text_S_Top40[] PROGMEM = "Top 40";
const char PTY2Text_S_Nostalgia[] PROGMEM = "Nostalgia";
const char PTY2Text_S_RnB[] PROGMEM = "Rhythm and blues";
const char PTY2Text_S_Language[] PROGMEM = "Language";
const char PTY2Text_S_Personality[] PROGMEM = "Personality";
const char PTY2Text_S_Public[] PROGMEM = "Public";
const char PTY2Text_S_College[] PROGMEM = "College";
const char PTY2Text_S_Spanish[] PROGMEM = "Espanol";
const char PTY2Text_S_HipHop[] PROGMEM = "Hip hop";

const char * const PTY2Text_EU[32] PROGMEM = {
    PTY2Text_S_None,
    PTY2Text_S_News,
    PTY2Text_S_Current,
    PTY2Text_S_Information,
    PTY2Text_S_Sports,
    PTY2Text_S_Education,
    PTY2Text_S_Drama,
    PTY2Text_S_Culture,
    PTY2Text_S_Science,
    PTY2Text_S_Varied,
    PTY2Text_S_Pop,
    PTY2Text_S_Rock,
    PTY2Text_S_EasySoft,
    PTY2Text_S_Classical,
    PTY2Text_S_Classical,
    PTY2Text_S_Other,
    PTY2Text_S_Weather,
    PTY2Text_S_Finance,
    PTY2Text_S_Children,
    PTY2Text_S_Social,
    PTY2Text_S_Religion,
    PTY2Text_S_TalkPhone,
    PTY2Text_S_Travel,
    PTY2Text_S_Leisure,
    PTY2Text_S_Jazz,
    PTY2Text_S_Country,
    PTY2Text_S_National,
    PTY2Text_S_Oldies,
    PTY2Text_S_Folk,
    PTY2Text_S_Documentary,
    PTY2Text_S_EmergencyTest,
    PTY2Text_S_Emergency};

const char * const PTY2Text_US[32] PROGMEM = {
    PTY2Text_S_None,
    PTY2Text_S_News,
    PTY2Text_S_Information,
    PTY2Text_S_Sports,
    PTY2Text_S_TalkPhone,
    PTY2Text_S_Rock,
    PTY2Text_S_Rock,
    PTY2Text_S_Adult,
    PTY2Text_S_Rock,
    PTY2Text_S_Top40,
    PTY2Text_S_Country,
    PTY2Text_S_Oldies,
    PTY2Text_S_EasySoft,
    PTY2Text_S_Nostalgia,
    PTY2Text_S_Jazz,
    PTY2Text_S_Classical,
    PTY2Text_S_RnB,
    PTY2Text_S_RnB,
    PTY2Text_S_Language,
    PTY2Text_S_Religion,
    PTY2Text_S_Religion,
    PTY2Text_S_Personality,
    PTY2Text_S_Public,
    PTY2Text_S_College,
    PTY2Text_S_Spanish,
    PTY2Text_S_Spanish,
    PTY2Text_S_HipHop,
    PTY2Text_S_None,
    PTY2Text_S_None,
    PTY2Text_S_Weather,
    PTY2Text_S_EmergencyTest,
    PTY2Text_S_Emergency};

const byte PTY_EU2US[32] PROGMEM = {0, 1, 0, 2, 3, 23, 0, 0, 0, 0, 7, 5, 12, 15,
                                    15, 0, 29, 0, 0, 0, 20, 4, 0, 0, 14, 10, 0,
                                    11, 0, 0, 30, 31};
const byte PTY_US2EU[32] PROGMEM = {0, 1, 3, 4, 21, 11, 11, 10, 11, 10, 25, 27,
                                    12, 27, 24, 14, 15, 15, 0, 20, 20, 0, 0, 5,
                                    0, 0, 0, 0, 0, 16, 30, 31};

void RDSTranslator::getTextForPTY(byte PTY, byte locale, char* text,
                                    byte textsize){
    switch(locale){
        case RDS_LOCALE_US:
            strncpy_P(text, (PGM_P)(pgm_read_ptr(&PTY2Text_US[PTY])),
                      textsize);
            break;
        case RDS_LOCALE_EU:
            strncpy_P(text, (PGM_P)(pgm_read_ptr(&PTY2Text_EU[PTY])),
                    textsize);
            break;
    }
}

byte RDSTranslator::translatePTY(byte PTY, byte fromlocale, byte tolocale){
    if(fromlocale == tolocale) return PTY;
    else switch(fromlocale){
        case RDS_LOCALE_US:
            return pgm_read_byte(&PTY_US2EU[PTY]);
            break;
        case RDS_LOCALE_EU:
            return pgm_read_byte(&PTY_EU2US[PTY]);
            break;
    }

    //Never reached
    return 0;
}

const char PI2CallSign_S[] PROGMEM = "KEXKFHKFIKGAKGOKGUKGWKGYKIDKITKJRKLOKLZ"
                                     "KMAKMJKNXKOA---------KQVKSLKUJKVIKWG---"
                                     "---KYW---WBZWDZWEW---WGLWGNWGR---WHAWHB"
                                     "WHKWHO---WIPWJRWKYWLSWLW------WOC---WOL"
                                     "WOR---------WWJWWL------------------KDB"
                                     "KGBKOYKPQKSDKUTKXLKXO---WBTWGHWGYWHPWIL"
                                     "WMCWMTWOIWOWWRRWSBWSMKBWKCYKDF------KHQ"
                                     "KOB---------------------WISWJWWJZ------"
                                     "---WRC";

bool RDSTranslator::decodeCallSign(word programIdentifier, char* callSign){
    if (!callSign) return false;

    if (programIdentifier < 0x1000 ||
        (programIdentifier > 0x9EFF && programIdentifier < 0xA100) ||
        programIdentifier > 0xAFAF)
        return false;
    else if (programIdentifier >= 0x9950 && programIdentifier <= 0x9EFF)
        if (programIdentifier <= 0x99B9) {
            programIdentifier -= 0x9950;
            char exists = char(pgm_read_byte(&PI2CallSign_S[programIdentifier * 3]));
            if (exists != '-') {
                strncpy_P(callSign, &PI2CallSign_S[programIdentifier * 3], 3);
                callSign[3] = '\0';
                return true;
            } else
                return false;
        } else
            return false;


    if (programIdentifier & 0xFFF0 == 0xAFA0)
        programIdentifier <<= 12;
    else if (programIdentifier & 0xFF00 == 0xAF00)
        programIdentifier <<= 8;
    else if (programIdentifier & 0xF000 == 0xA000)
        programIdentifier = (programIdentifier & 0x0F00 << 4) |
                            (programIdentifier & 0x00FF);

    if (programIdentifier >= 0x54A8) {
        callSign[0] = 'W';
        programIdentifier -= 0x54A8;
    } else if (programIdentifier >= 0x1000) {
        callSign[0] = 'K';
        programIdentifier -= 0x1000;
    } else
        return false;

    callSign[1] = char(programIdentifier / 676);
    programIdentifier -= byte(callSign[1]) * 676;
    callSign[1] += 'A';
    callSign[2] = char(programIdentifier / 26);
    programIdentifier -= byte(callSign[2] * 26);
    callSign[2] += 'A';
    callSign[3] = programIdentifier + 'A';
    callSign[4] = '\0';

    return true;
}

byte RDSTranslator::decodeTMCDistance(byte length) {
    if (length == 0) return 0xFF;
    else if (length > 0 && length <= 10) return length;
    else if (length > 10 && length <= 15) return 10 + (length - 10) * 2;
    else if (length > 15) return 20 + (length - 15) * 5;
}

void RDSTranslator::decodeTMCDuration(byte length, TRDSTime* tmctime) {
    if (!tmctime) return;
    else memset(tmctime, 0x00, sizeof(TRDSTime));

    if (length <= 95) {
        tmctime->tm_min = (length % 4) * 45;
        tmctime->tm_hour = length / 4;
    } else if (length > 95 && length <= 200) {
        tmctime->tm_hour = (length - 95) % 24;
        tmctime->tm_mday = (length - 95) / 24;
    } else if (length > 200 && length < 231) {
        tmctime->tm_mday = length - 200;
    } else if (length > 231) {
        // NOTE: according to RDS-TMC standard, this is expressed as half-month
        // intervals. Therefore, this function will output things like Feb 30th
        // with the understanding that the UI will render it appropriately.
        tmctime->tm_mday = ((length - 231) * 15) % 31;
        tmctime->tm_mon = ((length - 232) / 2) + 1;
    };
};

word RDSTranslator::decodeAFFrequency(byte AF, bool FM, byte locale) {
    if (FM) return (AF + 875) * 10;
    else if (AF < 16) return (AF - 1) * 9 + 153;
    else if (locale == RDS_LOCALE_US)
        return (AF - 16) * 10 + 530;
    else return (AF - 16) * 9 + 531;
};

int16_t RDSTranslator::decodeTZValue(int8_t tz) {
  return (tz / 2) * 60 + (tz % 2) * 30;
};