/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

DEFINE_int32(user_language, 1,
             "User language ID.\n"
             "  1=en  2=ja  3=de  4=fr  5=es  6=it  7=ko  8=zh\n"
             "  9=pt 11=pl 12=ru 13=sv 14=tr 15=nb 16=nl 17=zh",
             "XConfig");

DEFINE_int32(user_country, 103,
             "User country ID.\n"
             "   1=AE   2=AL   3=AM   4=AR   5=AT   6=AU   7=AZ   8=BE   9=BG\n"
             "  10=BH  11=BN  12=BO  13=BR  14=BY  15=BZ  16=CA  18=CH  19=CL\n"
             "  20=CN  21=CO  22=CR  23=CZ  24=DE  25=DK  26=DO  27=DZ  28=EC\n"
             "  29=EE  30=EG  31=ES  32=FI  33=FO  34=FR  35=GB  36=GE  37=GR\n"
             "  38=GT  39=HK  40=HN  41=HR  42=HU  43=ID  44=IE  45=IL  46=IN\n"
             "  47=IQ  48=IR  49=IS  50=IT  51=JM  52=JO  53=JP  54=KE  55=KG\n"
             "  56=KR  57=KW  58=KZ  59=LB  60=LI  61=LT  62=LU  63=LV  64=LY\n"
             "  65=MA  66=MC  67=MK  68=MN  69=MO  70=MV  71=MX  72=MY  73=NI\n"
             "  74=NL  75=NO  76=NZ  77=OM  78=PA  79=PE  80=PH  81=PK  82=PL\n"
             "  83=PR  84=PT  85=PY  86=QA  87=RO  88=RU  89=SA  90=SE  91=SG\n"
             "  92=SI  93=SK  95=SV  96=SY  97=TH  98=TN  99=TR 100=TT 101=TW\n"
             " 102=UA 103=US 104=UY 105=UZ 106=VE 107=VN 108=YE 109=ZA\n",
             "XConfig");

DEFINE_uint32(
    console_region, 65535,
    "Console region.\n"
    " 65535 = Region Free\n"
    "  2047 = Devkit\n"
    "   255 = NTSC-U\n"
    "   511 = NTSC-J\n"
    "   257 = NTSC-J (Japan)\n"
    "   258 = NTSC-J (China)\n"
    "   767 = PAL\n"
    "   513 = PAL (Australia)\n"
    "This config variable allows you to change the main region reported\n"
    "by the console. Only change this from the default rf (region free)\n"
    "if language settings in game are not working as expected.\n",
    "XConfig");

// https://github.com/EatonZ/DevTool/blob/master/src/DevTool/Classes/XConfig.cs#L639
DEFINE_uint32(
    audio_flag, 65536,
    "Audio Mode.\n"
    "          1 = Digital Stereo \n"
    "          2 = Analog Mono (defaults to stereo in versions beyond blades)\n"
    "          3 = Stereo Bypass (?) \n"
    "      65536 = Dolby Digital\n"
    "     131072 = WMA PRO\n"
    " 2147483648 = Low Latency (?) Defaults to digital stereo \n",
    "XConfig");

DEFINE_uint32(av_region, 0x00001000,
              "AV Region.\n"
              " 0x00001000 = USA/Canada \n"
              " 0x00400100 = NTSCM\n"
              " 0x00400200 = NTSCJ \n"
              " 0x00400400 = PAL60\n"
              " 0x00800300 = PAL50\n",
              "XConfig");

DEFINE_uint32(dvd_region, 0,
              "AV Region.\n"
              " 0 = North America \n"
              " 1 = Asia\n"
              " 2 = Europe \n"
              " 3 = Rest of the World \n"
              " 0x7F = Devkit\n",
              "XConfig");

DEFINE_bool(xconfig_initial_setup, false,
            "Boot dashboard to Out of Box Experience", "XConfig");

namespace xe {
namespace kernel {
namespace xboxkrnl {

X_STATUS xeExGetXConfigSetting(uint16_t category, uint16_t setting,
                               void* buffer, uint16_t buffer_size,
                               uint16_t* required_size) {
  uint16_t setting_size = 0;
  uint32_t retail_flags = 0;
  alignas(uint32_t) uint8_t value[4];

  // TODO(benvanik): have real structs here that just get copied from.
  // https://free60project.github.io/wiki/XConfig.html
  // https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/Generic/ExConfig.h
  switch (category) {
    case 0x0002:
      // XCONFIG_SECURED_CATEGORY
      switch (setting) {
        case 0x0001:  // XCONFIG_SECURED_MAC_ADDRESS (6 bytes)
          setting_size = 6;
          xe::store_and_swap<uint8_t>(value,
                                      0x000000000001);  // Mac Address Here
          break;      // Possibly have it set on use
        case 0x0002:  // XCONFIG_SECURED_AV_REGION
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::av_region);
          break;
        case 0x0003:  // XCONFIG_SECURED_GAME_REGION
          setting_size = 2;
          xe::store_and_swap<uint16_t>(value, cvars::console_region);
          break;
        case 0x0004:  // XCONFIG_SECURED_DVD_REGION
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::dvd_region);
          break;
        case 0x0005:  // XCONFIG_SECURED_RESET_KEY
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0008:  // XCONFIG_SECURED_ONLINE_NETWORK_ID
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        default:
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    case 0x0003:
      // XCONFIG_USER_CATEGORY
      switch (setting) {
        case 0x0001:  // XCONFIG_USER_TIME_ZONE_BIAS
        case 0x0002:  // XCONFIG_USER_TIME_ZONE_STD_NAME
        case 0x0003:  // XCONFIG_USER_TIME_ZONE_DLT_NAME
        case 0x0004:  // XCONFIG_USER_TIME_ZONE_STD_DATE
        case 0x0005:  // XCONFIG_USER_TIME_ZONE_DLT_DATE
        case 0x0006:  // XCONFIG_USER_TIME_ZONE_STD_BIAS
        case 0x0007:  // XCONFIG_USER_TIME_ZONE_DLT_BIAS
          setting_size = 4;
          // TODO(benvanik): get this value.
          xe::store_and_swap<uint32_t>(value, 0);
          break;
        case 0x0008:  // XCONFIG_DEFAULT_PROFILE
          setting_size = 8;
          xe::store_and_swap<uint64_t>(value, 0);
          break;
        case 0x0009:  // XCONFIG_USER_LANGUAGE
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::user_language);
          break;
        case 0x000A:  // XCONFIG_USER_VIDEO_FLAGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0x00040000);
          break;
        case 0x000B:  // XCONFIG_USER_AUDIO_FLAGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::audio_flag);
          break;
        case 0x000C:             // XCONFIG_USER_RETAIL_FLAGS
          retail_flags |= 0x02;  // DST off
          retail_flags |= 0x04;  // network initialized?
          // flags |= 0x08;  // 24-hour clock
          retail_flags |= cvars::xconfig_initial_setup
                              ? 0
                              : 0x40;  // dashboard initial setup complete
          // retail_flags |= 0x00000800; // Start on IPTV, maybe swap to start
          // up option
          retail_flags |= 0x00001000;  // Enable IPTV UI
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, retail_flags);
          break;
        case 0x000D:  // XCONFIG_USER_DEVKIT_FLAGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0x00);
          break;
        case 0x000E:  // XCONFIG_USER_COUNTRY
          setting_size = 1;
          value[0] = static_cast<uint8_t>(cvars::user_country);
          break;
        case 0x000F:  // XCONFIG_USER_PC_FLAGS (parental control?)
          setting_size = 1;
          xe::store_and_swap<uint8_t>(value, 0);  // value[0]?
          break;
        case 0x0010:  // XCONFIG_USER_SMB_CONFIG (0x100 byte string)
                      // Just set the start of the buffer to 0 so that callers
                      // don't error from an un-inited buffer
          setting_size = 256;
          xe::store_and_swap<uint64_t>(value, 0);  // value[0]?
          break;
        case 0x0011:  // XCONFIG_USER_LIVE_PUID
          setting_size = 8;
          xe::store_and_swap<uint64_t>(value, 0x0009E2329D404916);  // value[0]?
          break;
        case 0x0013:  // XCONFIG_USER_AV_COMPOSITE_SCREENSZ
                      // setting_size = 4;
                      // xe::store_and_swap<uint16_t>(value, 0); // value[0]?
                      // break;
        case 0x0014:  // XCONFIG_USER_AV_COMPONENT_SCREENSZ
                      // setting_size = 4;
                      // xe::store_and_swap<uint16_t>(value, 0); // value[0]?
                      // break;
        case 0x0015:  // XCONFIG_USER_AV_VGA_SCREENSZ
                      // setting_size = 4;
                      // xe::store_and_swap<uint16_t>(value, 0); // value[0]?
                      // break;
        case 0x0016:  // XCONFIG_USER_PC_GAME
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0017:  // XCONFIG_USER_PC_PASSWORD
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0018:  // XCONFIG_USER_PC_MOVIE
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0019:  // XCONFIG_USER_PC_GAME_RATING
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x001A:  // XCONFIG_USER_PC_MOVIE_RATING
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x001B:  // XCONFIG_USER_PC_HINT
          setting_size = 1;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x001C:  // XCONFIG_USER_PC_HINT_ANSWER
          setting_size = 32;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x001D:  // XCONFIG_USER_PC_OVERRIDE
          setting_size = 32;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x001E:  // XCONFIG_USER_MUSIC_PLAYBACK_MODE
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x001F:  // XCONFIG_USER_MUSIC_VOLUME
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0020:  // XCONFIG_USER_MUSIC_FLAGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0021:  // XCONFIG_USER_ARCADE_FLAGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0022:  // XCONFIG_USER_PC_VERSION
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0023:  // XCONFIG_USER_PC_TV
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0024:  // XCONFIG_USER_PC_TV_RATING
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0025:  // XCONFIG_USER_PC_EXPLICIT_VIDEO
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0026:  // XCONFIG_USER_PC_EXPLICIT_VIDEO_RATING
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0027:  // XCONFIG_USER_PC_UNRATED_VIDEO
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0028:  // XCONFIG_USER_PC_UNRATED_VIDEO_RATING
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x0029:  // XCONFIG_USER_VIDEO_OUTPUT_BLACK_LEVELS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x002A:  // XCONFIG_USER_VIDEO_PLAYER_DISPLAY_MODE
          setting_size = 1;
          xe::store_and_swap<uint8_t>(value, 0);  // value[0]?
          break;
        case 0x002B:  // ALTERNATE_VIDEO_TIMING_ID
          setting_size = 1;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x002C:  // XCONFIG_USER_VIDEO_DRIVER_OPTIONS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x002D:  // XCONFIG_USER_MUSIC_UI_FLAGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);  // value[0]?
          break;
        case 0x002E:  // XCONFIG_USER_VIDEO_MEDIA_SOURCE_TYPE
          setting_size = 1;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x002F:  // XCONFIG_USER_MUSIC_MEDIA_SOURCE_TYPE
          setting_size = 1;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x0030:  // XCONFIG_USER_PHOTO_MEDIA_SOURCE_TYPE
          setting_size = 1;
          value[0] = static_cast<uint8_t>(0);
          break;
        default:
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    case 0x0006:
      // XCONFIG_MEDIA_CENTER_CATEGORY
      switch (setting) {
        case 0x0001:  // XCONFIG_MEDIA_CENTER_MEDIA_PLAYER
          setting_size = 10;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x0002:  // XCONFIG_MEDIA_CENTER_XESLED_VERSION
          setting_size = 10;
          xe::store_and_swap<uint8_t>(value, 0);  // value[0]?
          break;
        case 0x0003:  // XCONFIG_MEDIA_CENTER_XESLED_TRUST_SECRET
          setting_size = 20;
          xe::store_and_swap<uint8_t>(value, 0);  // value[0]?
          break;
        case 0x0004:  // XCONFIG_MEDIA_CENTER_XESLED_TRUST_CODE
          setting_size = 5;
          xe::store_and_swap<uint8_t>(value, 0);  // value[0]?
          break;
        case 0x0005:  // XCONFIG_MEDIA_CENTER_XESLED_HOST_ID
          setting_size = 20;
          xe::store_and_swap<uint8_t>(value, 0);  // value[0]?
          break;
        case 0x0006:  // XCONFIG_MEDIA_CENTER_XESLED_KEY
          setting_size = 1628;
          xe::store_and_swap<uint8_t>(value, 0);  // value[0]?
          break;
        case 0x0007:  // XCONFIG_MEDIA_CENTER_XESLED_HOST_MAC_ADDRESS
          setting_size = 6;
          xe::store_and_swap<uint8_t>(value, 0);  // value[0]?
          break;
        case 0x0008:  // XCONFIG_MEDIA_CENTER_SERVER_UUID
          setting_size = 16;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x0009:  // XCONFIG_MEDIA_CENTER_SERVER_NAME
          setting_size = 128;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x000A:  // XCONFIG_MEDIA_CENTER_SERVER_FLAG
          setting_size = 4;
          value[0] = static_cast<uint8_t>(0);
          break;
        default:
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    case 0x0007:
      // XCONFIG_CONSOLE_SETTINGS
      switch (setting) {
        case 0x0001:  // XCONFIG_CONSOLE_SCREENSAVER
          setting_size = 2;
          xe::store_and_swap<int16_t>(value, 0);
          break;
        case 0x0002:  // XCONFIG_CONSOLE_AUTO_SHUTDOWN
          setting_size = 2;
          xe::store_and_swap<int16_t>(value, 0);
          break;
        case 0x0003:  // XCONFIG_CONSOLE_WIRELESS_SETTINGS
          setting_size = 256;
          xe::store_and_swap<uint8_t>(value, 0);
          break;
        case 0x0004:  // XCONFIG_CONSOLE_CAMERA_SETTINGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);
          break;
        case 0x0005:  // XCONFIG_CONSOLE_PLAYTIMERDATA
          setting_size = 20;
          xe::store_and_swap<int64_t>(value, 0);
          break;
        case 0x0006:  // XCONFIG_CONSOLE_MEDIA_DISABLEAUTOLAUNCH
          setting_size = 2;
          xe::store_and_swap<int16_t>(value, 0);
          break;
        case 0x0007:  // XCONFIG_CONSOLE_KEYBOARD_LAYOUT
          setting_size = 2;
          xe::store_and_swap<int16_t>(value, 0);
          break;
        case 0x0008:  // XCONFIG_CONSOLE_PC_TITLE_EXEMPTIONS
                      // setting_size = 64;
                      // xe::store_and_swap<int16_t>(value, 0); ?
                      // break;
        case 0x0009:  //  XCONFIG_CONSOLE_NUI
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);
          break;
        case 0x000A:  //  XCONFIG_CONSOLE_VOICE
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);
          break;
        case 0x000B:  //  XCONFIG_CONSOLE_RETAIL_EX_FLAGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0);
          break;
        default:
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    case 0x0009:
      switch (setting) {
        case 0x0001:  // XCONFIG_IPTV_SERVICE_PROVIDER_NAME
          setting_size = 120;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x0002:  // XCONFIG_IPTV_PROVISIONING_SERVER_URL
          setting_size = 128;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x0003:  // XCONFIG_IPTV_SUPPORT_INFO
          setting_size = 128;
          value[0] = static_cast<uint8_t>(0);
          break;
        case 0x0004:  // XCONFIG_IPTV_BOOTSTRAP_SERVER_URL
          setting_size = 128;
          value[0] = static_cast<uint8_t>(0);
          break;
        default:
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    default:
      assert_unhandled_case(category);
      return X_STATUS_INVALID_PARAMETER_1;
  }

  if (buffer) {
    if (buffer_size < setting_size) {
      return X_STATUS_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, value, setting_size);
  } else {
    if (buffer_size) {
      return X_STATUS_INVALID_PARAMETER_3;
    }
  }

  if (required_size) {
    *required_size = setting_size;
  }

  return X_STATUS_SUCCESS;
}

dword_result_t ExGetXConfigSetting_entry(word_t category, word_t setting,
                                         lpvoid_t buffer_ptr,
                                         word_t buffer_size,
                                         lpword_t required_size_ptr) {
  uint16_t required_size = 0;
  X_STATUS result = xeExGetXConfigSetting(category, setting, buffer_ptr,
                                          buffer_size, &required_size);

  if (required_size_ptr) {
    *required_size_ptr = required_size;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(ExGetXConfigSetting, kModules, kImplemented);

dword_result_t ExSetXConfigSetting_entry(word_t category, word_t setting,
                                         lpvoid_t buffer_ptr,
                                         word_t buffer_size) {
  return 0;
}
DECLARE_XBOXKRNL_EXPORT1(ExSetXConfigSetting, kModules, kStub);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(XConfig);
